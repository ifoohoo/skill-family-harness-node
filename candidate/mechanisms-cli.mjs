#!/usr/bin/env node
import { stdin, stdout, stderr } from "node:process";
import { realpathSync } from "node:fs";
import { fileURLToPath } from "node:url";
import {
  invokeFoundationMechanism,
  verifyManagedBundleIdentity,
} from "./quickstart-profile.mjs";
import { loadMechanismBatchPolicy } from "skill-family-contracts/quickstart-profile";

const CLI_NAME = "mechanisms-cli.mjs";

// Fixed capacity authority and serialization envelope for the bounded
// mechanism batch transport (FND-DES-022); both derive from the single
// Contracts authority rather than a second copy.
const MECHANISM_BATCH_POLICY = loadMechanismBatchPolicy();
const BATCH_OUTPUT_PREFIX = '{"results":[';
const BATCH_OUTPUT_SUFFIX = "]}\n";

async function readRequest(input) {
  const chunks = [];
  for await (const chunk of input) chunks.push(Buffer.from(chunk));
  const text = new TextDecoder("utf-8", { fatal: true }).decode(Buffer.concat(chunks));
  return JSON.parse(text);
}

function errorResponse(cause) {
  return {
    ok: false,
    error: {
      name: typeof cause?.name === "string" ? cause.name : "Error",
      message: cause?.message ?? String(cause),
      ...(typeof cause?.code === "string" ? { code: cause.code } : {}),
      ...(cause?.details !== undefined ? { details: cause.details } : {}),
    },
  };
}

/**
 * Fixed dispatch for the publish-fixed-set operation.
 *
 * This operation is fail-closed: if the native addon is unavailable or the
 * platform is unsupported, the operation returns a receipt with status=refused
 * and error code UNSUPPORTED_PLATFORM. No JS rename fallback is attempted.
 *
 * The operation takes a frozen sibling-directory source/target tuple and
 * returns a receipt. It does NOT
 * expose any module/function/pathToFileURL call surface.
 */
async function runPublishFixedSet(params, invoke) {
  if (
    params === null ||
    typeof params !== "object" ||
    Array.isArray(params) ||
    Object.keys(params).sort().join(",") !== "manifest,sourceRoot,targetParent,targetSegment" ||
    typeof params.sourceRoot !== "string" ||
    typeof params.targetParent !== "string" ||
    typeof params.targetSegment !== "string" ||
    params.manifest === null ||
    typeof params.manifest !== "object"
  ) {
    throw new TypeError("publish-fixed-set requires exactly sourceRoot, targetParent, targetSegment, and manifest");
  }

  let publishFixedSet;
  try {
    const mod = await import("./rename-directory-no-replace/rename-directory-no-replace.mjs");
    publishFixedSet = mod.publishFixedSet;
  } catch (cause) {
    const manifestDigest = await invoke({ operation: "digest-document", params: { document: params.manifest } });
    return {
      schemaVersion: 1,
      kind: "skill-family.fixed-set-publication-receipt",
      manifestDigest: manifestDigest.digest,
      targetRootBinding: params.manifest?.target?.rootBinding ?? {
        kind: "trusted-filesystem-root-binding", digestAlgorithm: "sha256",
        basis: "canonical-realpath-device-inode-type-mode-v1", digest: "0".repeat(64),
      },
      platform: "other",
      primitive: "none",
      status: "refused",
      targetState: "absent",
      commitState: "not-committed",
      verification: "not-run",
      durability: "not-attempted",
      error: {
        code: "UNSUPPORTED_PLATFORM",
        message: `publish-fixed-set module unavailable: ${cause?.message ?? "unknown"}`.slice(0, 300),
      },
    };
  }

  return publishFixedSet(params);
}

/** Fixed manifest construction for the same receipt-bound publication mechanism. */
async function runCreateFixedSetPublicationManifest(params) {
  if (
    params === null ||
    typeof params !== "object" ||
    Array.isArray(params) ||
    Object.keys(params).sort().join(",") !== "sourceRoot,targetParent,targetSegment" ||
    typeof params.sourceRoot !== "string" ||
    typeof params.targetParent !== "string" ||
    typeof params.targetSegment !== "string"
  ) {
    throw new TypeError(
      "create-fixed-set-publication-manifest requires exactly sourceRoot, targetParent, and targetSegment",
    );
  }
  const { createFixedSetPublicationManifest } = await import(
    "./rename-directory-no-replace/rename-directory-no-replace.mjs"
  );
  return createFixedSetPublicationManifest(params);
}

/**
 * One-request/one-response JSON transport for the fixed Foundation mechanism
 * bridge. The offline Bundle rewrites the import above to its managed runner,
 * which injects the Bundle-owned schema validator. No request can name a
 * module, export, or arbitrary function.
 */
export async function runMechanismCli({
  input = stdin,
  output = stdout,
  error = stderr,
  invoke = invokeFoundationMechanism,
} = {}) {
  try {
    const request = await readRequest(input);
    let result;
    if (request?.operation === "self-check") {
      result = await runSelfCheck(request);
    } else if (request?.operation === "create-fixed-set-publication-manifest") {
      result = await runCreateFixedSetPublicationManifest(request.params);
    } else if (request?.operation === "publish-fixed-set") {
      result = await runPublishFixedSet(request.params, invoke);
    } else {
      result = await invoke(request);
    }
    output.write(`${JSON.stringify(result)}\n`);
    return 0;
  } catch (cause) {
    error.write(`${JSON.stringify(errorResponse(cause))}\n`);
    return 2;
  }
}

async function runSelfCheck(request) {
  if (
    request === null ||
    typeof request !== "object" ||
    Array.isArray(request) ||
    Object.keys(request).sort().join(",") !== "operation,params" ||
    request.params === null ||
    typeof request.params !== "object" ||
    Array.isArray(request.params) ||
    Object.keys(request.params).length !== 0
  ) {
    throw new TypeError("mechanisms CLI self-check requires exactly operation and empty params");
  }
  return verifyManagedBundleIdentity({ cliUrl: import.meta.url, cliName: CLI_NAME });
}

function batchRefusal(kind, message) {
  const cause = new TypeError(message);
  cause.details = { kind };
  return cause;
}

/**
 * Bounded raw-byte read through explicit listeners (never `for await`, whose
 * early break would destroy the caller stream). The current chunk is kept only
 * after the running total plus its bytes is confirmed within the limit, so an
 * over-limit chunk is never copied into the accumulation cache. Every exit
 * path releases this function's listeners; refusal never terminates the
 * caller process.
 */
function readBoundedBatchInput(input, byteLimit) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let received = 0;
    const cleanup = () => {
      input.removeListener("data", onData);
      input.removeListener("end", onEnd);
      input.removeListener("error", onError);
    };
    const onData = (chunk) => {
      const bytes = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
      const next = received + bytes.length;
      if (next > byteLimit) {
        cleanup();
        reject(
          batchRefusal(
            "batch-input-limit",
            `mechanism batch input exceeds ${byteLimit} raw UTF-8 bytes`,
          ),
        );
        return;
      }
      received = next;
      chunks.push(bytes);
    };
    const onEnd = () => {
      cleanup();
      resolve(Buffer.concat(chunks));
    };
    const onError = (cause) => {
      cleanup();
      reject(cause);
    };
    input.on("data", onData);
    input.on("end", onEnd);
    input.on("error", onError);
  });
}

/**
 * Whole-batch shape precheck (FND-DES-022 section 3): the outer layer of every
 * item is checked before the first item executes. Structure failures never
 * reach the mechanism dispatcher and never fabricate per-item results.
 */
function assertMechanismBatchShape(request) {
  if (
    request === null ||
    typeof request !== "object" ||
    Array.isArray(request) ||
    Object.keys(request).sort().join(",") !== "inputs,operation" ||
    request.operation !== "canonical-json" ||
    !Array.isArray(request.inputs) ||
    request.inputs.length === 0
  ) {
    throw batchRefusal(
      "batch-structure-invalid",
      "mechanism batch request must carry exactly operation canonical-json and a non-empty inputs array",
    );
  }
  for (const item of request.inputs) {
    if (
      item === null ||
      typeof item !== "object" ||
      Array.isArray(item) ||
      Object.keys(item).sort().join(",") !== "document"
    ) {
      throw batchRefusal(
        "batch-structure-invalid",
        "each mechanism batch input must be an object carrying exactly document",
      );
    }
  }
}

/**
 * Waits until the writable accepted the whole payload, propagating both
 * synchronous write throws and asynchronous stream errors.
 */
function writeAll(stream, text) {
  return new Promise((resolve, reject) => {
    const onError = (cause) => {
      stream.removeListener("drain", onDrain);
      reject(cause);
    };
    const onDrain = () => {
      stream.removeListener("error", onError);
      resolve();
    };
    stream.on("error", onError);
    try {
      if (stream.write(text)) {
        stream.removeListener("error", onError);
        resolve();
        return;
      }
      stream.once("drain", onDrain);
    } catch (cause) {
      stream.removeListener("error", onError);
      reject(cause);
    }
  });
}

/**
 * Bounded, same-operation, ordered mechanism batch transport (FND-DES-022).
 *
 * The request stream is read once under the raw-byte budget, strictly decoded
 * as UTF-8, parsed once, shape-prechecked, then executed item by item through
 * the fixed single-request dispatcher — no second algorithm is introduced.
 * Every item's serialized response bytes are accumulated with the envelope
 * commas, prefix, and mandatory closing newline; nothing is written to the
 * output until the complete response is confirmed within the output budget.
 * Item failures follow the original CLI error projection into that item's
 * `response` and continue; whole-batch refusals and transport failures write
 * one batch error object to the error channel and never touch the output.
 */
export async function runMechanismCliBatch({
  input = stdin,
  output = stdout,
  error = stderr,
  invoke = invokeFoundationMechanism,
} = {}) {
  try {
    const bytes = await readBoundedBatchInput(input, MECHANISM_BATCH_POLICY.inputByteLimit);
    const text = new TextDecoder("utf-8", { fatal: true }).decode(bytes);
    const request = JSON.parse(text);
    assertMechanismBatchShape(request);
    const inputs = request.inputs;
    if (inputs.length > MECHANISM_BATCH_POLICY.itemLimit) {
      throw batchRefusal(
        "batch-item-limit",
        `mechanism batch exceeds the ${MECHANISM_BATCH_POLICY.itemLimit} item limit`,
      );
    }
    const fragments = [];
    let outputBytes = Buffer.byteLength(BATCH_OUTPUT_PREFIX, "utf8");
    let anyItemFailure = false;
    for (let inputIndex = 0; inputIndex < inputs.length; inputIndex += 1) {
      const item = inputs[inputIndex];
      let exitCode = 0;
      let response;
      try {
        response = await invoke({ operation: "canonical-json", params: item });
      } catch (cause) {
        response = errorResponse(cause);
        exitCode = 2;
      }
      const fragment = JSON.stringify({ inputIndex, exitCode, response });
      if (inputIndex > 0) outputBytes += 1; // separating comma between items
      outputBytes += Buffer.byteLength(fragment, "utf8");
      // The closing bytes are mandatory, so the check includes them before the
      // next item may execute; an overrun never delivers a partial result.
      if (outputBytes + Buffer.byteLength(BATCH_OUTPUT_SUFFIX, "utf8") > MECHANISM_BATCH_POLICY.outputByteLimit) {
        throw batchRefusal(
          "batch-output-limit",
          `mechanism batch response exceeds the ${MECHANISM_BATCH_POLICY.outputByteLimit} byte limit`,
        );
      }
      fragments.push(fragment);
      if (exitCode === 2) anyItemFailure = true;
    }
    await writeAll(
      output,
      `${BATCH_OUTPUT_PREFIX}${fragments.join(",")}${BATCH_OUTPUT_SUFFIX}`,
    );
    return anyItemFailure ? 2 : 0;
  } catch (cause) {
    try {
      await writeAll(error, `${JSON.stringify(errorResponse(cause))}\n`);
    } catch {
      // The error channel itself failed; the batch still exits failed.
    }
    return 2;
  }
}

if (process.argv[1] && realpathSync(fileURLToPath(import.meta.url)) === realpathSync(process.argv[1])) {
  const args = process.argv.slice(2);
  if (args[0] === "--batch") {
    if (args.length > 1) {
      // Refuse before reading any input: the batch mode takes no positional
      // arguments, and the old single-request entry must stay unchanged.
      stderr.write(
        `${JSON.stringify(errorResponse(new TypeError("mechanisms CLI --batch accepts no additional arguments")))}\n`,
      );
      process.exitCode = 2;
    } else {
      process.exitCode = await runMechanismCliBatch();
    }
  } else {
    process.exitCode = await runMechanismCli();
  }
}
