#!/usr/bin/env node
import { createHash } from "node:crypto";
import {
  chmodSync,
  copyFileSync,
  lstatSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  realpathSync,
  renameSync,
  rmSync,
  statSync,
  writeFileSync,
} from "node:fs";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const ROOT = path.dirname(fileURLToPath(import.meta.url));
const SOURCE = path.join(ROOT, "addon", "rename_directory_no_replace.c");
const EXPECTED_SOURCE = "packages/skill-family-harness-node/candidate/rename-directory-no-replace/addon/rename_directory_no_replace.c";
const NODE_VERSION = "v22.23.2";
const NAPI = "10";
const MODE = 0o644;
const EXPORTS = Object.freeze(["closeParentDirectory", "openParentDirectory", "platform", "renameDirectoryNoReplace"]);
const KEYS = Object.freeze(["darwin-arm64", "darwin-x64", "linux-arm64-gnu", "linux-x64-gnu"]);
const CONFIG = Object.freeze({
  "darwin-arm64": { os: "darwin", arch: "arm64", libc: "none" },
  "darwin-x64": { os: "darwin", arch: "x64", libc: "none" },
  "linux-arm64-gnu": { os: "linux", arch: "arm64", libc: "glibc" },
  "linux-x64-gnu": { os: "linux", arch: "x64", libc: "glibc" },
});

class UsageError extends Error {}
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const canonical = (value) => `${JSON.stringify(value, null, 2)}\n`;

function parseArgs(argv) {
  const allowed = new Set(["output-root", ...KEYS.flatMap((key) => [`${key}-receipt`, `${key}-binary`])]);
  const values = new Map();
  for (let index = 0; index < argv.length; index += 2) {
    const flag = argv[index];
    const value = argv[index + 1];
    if (!flag?.startsWith("--") || value === undefined) throw new UsageError("每个固定选项都必须提供一个值");
    const name = flag.slice(2);
    if (!allowed.has(name)) throw new UsageError(`未知选项：${flag}`);
    if (values.has(name)) throw new UsageError(`重复选项：${flag}`);
    values.set(name, value);
  }
  for (const name of allowed) if (!values.has(name)) throw new UsageError(`缺少选项：--${name}`);
  return values;
}

function readJson(file) {
  const link = lstatSync(file);
  if (!link.isFile() || link.isSymbolicLink() || link.nlink !== 1) throw new Error(`receipt必须是单链接普通文件：${file}`);
  return { document: JSON.parse(readFileSync(file, "utf8")), sha256: sha256(readFileSync(file)) };
}

function receiptFacts(key, receipt) {
  const expected = CONFIG[key];
  if (key.startsWith("darwin-")) {
    const matrix = receipt.kind === "skill-family-foundation-native-platform-matrix-receipt";
    const single = receipt.kind === "skill-family-foundation-native-platform-receipt";
    if (!matrix && !single) throw new Error(`${key}: Darwin receipt kind错误`);
    const candidate = receipt.candidate;
    const sourceSha = matrix ? receipt.source?.nativeSource?.sha256 : receipt.source?.nativeSourceSha256;
    const node = matrix ? candidate?.node : receipt.runtime?.node;
    const napi = matrix ? candidate?.napi : receipt.runtime?.napi;
    const arch = matrix ? candidate?.arch : receipt.arch;
    if ((matrix && candidate?.platform !== expected.os) || (single && receipt.platform !== expected.os) ||
        arch !== expected.arch || node !== NODE_VERSION || String(napi) !== NAPI ||
        (single && (receipt.status !== "VERIFIED" || receipt.actualExecution !== true))) {
      throw new Error(`${key}: Darwin receipt平台或Node ABI不匹配`);
    }
    return { sourceSha, binarySha: candidate.sha256, binaryMode: Number.parseInt(candidate.mode, 8), binarySize: candidate.size };
  }
  if (receipt.kind !== "skill-family-foundation.w3n3-linux-native-platform-receipt" || receipt.status !== "PASS" ||
      receipt.platform !== `linux-${expected.arch}` || receipt.node?.version !== NODE_VERSION ||
      String(receipt.node?.napi) !== NAPI || !receipt.oracle?.results?.every((result) => result.status === "PASS")) {
    throw new Error(`${key}: Linux receipt平台、oracle或Node ABI不匹配`);
  }
  return {
    sourceSha: receipt.source?.sha256,
    binarySha: receipt.addon?.sha256,
    binaryMode: Number.parseInt(receipt.addon?.mode, 8),
    binarySize: undefined,
  };
}

function verifyBinary(key, file, facts) {
  const link = lstatSync(file);
  if (!link.isFile() || link.isSymbolicLink() || link.nlink !== 1) throw new Error(`${key}: binary必须是单链接普通文件`);
  if ((link.mode & 0o777) !== facts.binaryMode) throw new Error(`${key}: binary mode与receipt不匹配`);
  if (facts.binarySize !== undefined && link.size !== facts.binarySize) throw new Error(`${key}: binary size与receipt不匹配`);
  if (sha256(readFileSync(file)) !== facts.binarySha) throw new Error(`${key}: binary SHA与receipt不匹配`);
}

function replaceManaged(outputRoot, stage, names) {
  const backup = mkdtempSync(path.join(path.dirname(outputRoot), ".native-prebuild-backup-"));
  const moved = [];
  try {
    mkdirSync(outputRoot, { recursive: true });
    for (const name of names) {
      const target = path.join(outputRoot, name);
      try {
        lstatSync(target);
        const saved = path.join(backup, name);
        mkdirSync(path.dirname(saved), { recursive: true });
        renameSync(target, saved);
        moved.push(name);
      } catch (error) {
        if (error?.code !== "ENOENT") throw error;
      }
      renameSync(path.join(stage, name), target);
    }
  } catch (error) {
    for (const name of [...names].reverse()) {
      const target = path.join(outputRoot, name);
      try { rmSync(target, { recursive: true, force: true }); } catch {}
      if (moved.includes(name)) renameSync(path.join(backup, name), target);
    }
    throw error;
  } finally {
    rmSync(backup, { recursive: true, force: true });
  }
}

export function assemble(argv) {
  const args = parseArgs(argv);
  const sourceSha = sha256(readFileSync(SOURCE));
  const inputs = [];
  for (const key of KEYS) {
    const receiptPath = path.resolve(args.get(`${key}-receipt`));
    const binaryPath = path.resolve(args.get(`${key}-binary`));
    const receipt = readJson(receiptPath);
    const facts = receiptFacts(key, receipt.document);
    if (facts.sourceSha !== sourceSha) throw new Error(`${key}: receipt source SHA与current native source不匹配`);
    verifyBinary(key, binaryPath, facts);
    inputs.push({ key, receiptSha: receipt.sha256, binaryPath, ...facts });
  }
  if (new Set(inputs.map((entry) => entry.sourceSha)).size !== 1) throw new Error("四份receipt的native source SHA不一致");

  const outputRoot = path.resolve(args.get("output-root"));
  const stage = mkdtempSync(path.join(path.dirname(outputRoot), ".native-prebuild-stage-"));
  const entries = [];
  try {
    for (const input of inputs) {
      const filename = `rename_directory_no_replace.${input.key}.node`;
      const relative = `prebuilds/${input.key}/${filename}`;
      const destination = path.join(stage, relative);
      mkdirSync(path.dirname(destination), { recursive: true });
      copyFileSync(input.binaryPath, destination);
      chmodSync(destination, MODE);
      entries.push({ platformKey: input.key, ...CONFIG[input.key], binary: relative, mode: MODE, sha256: input.binarySha, napi: Number(NAPI), exports: EXPORTS });
    }
    const manifest = { schemaVersion: 2, kind: "skill-family.rename-directory-no-replace-prebuild-manifest", status: "candidate", entries };
    const sbom = {
      schemaVersion: 1,
      kind: "skill-family.rename-directory-no-replace-prebuild-sbom",
      status: "candidate",
      source: { path: EXPECTED_SOURCE, sha256: sourceSha },
      files: entries.map(({ platformKey, binary, sha256: digest }) => ({ platformKey, binary, sha256: digest, license: "Apache-2.0" })),
    };
    const releaseReceipt = {
      schemaVersion: 1,
      kind: "skill-family.rename-directory-no-replace-prebuild-release-receipt",
      status: "candidate",
      node: { version: NODE_VERSION, napi: Number(NAPI) },
      source: { path: EXPECTED_SOURCE, sha256: sourceSha },
      manifestSha256: sha256(Buffer.from(canonical(manifest))),
      inputs: inputs.map(({ key, receiptSha, binarySha }) => ({ platformKey: key, platformReceiptSha256: receiptSha, binarySha256: binarySha })),
    };
    writeFileSync(path.join(stage, "prebuild-manifest.json"), canonical(manifest));
    writeFileSync(path.join(stage, "prebuild-sbom.json"), canonical(sbom));
    writeFileSync(path.join(stage, "prebuild-release-receipt.json"), canonical(releaseReceipt));
    writeFileSync(path.join(stage, "NOTICE"), "Skill Family Foundation rename-directory-no-replace candidate prebuilds\nCompiled from the Apache-2.0 source identified in prebuild-sbom.json.\nNo fallback implementation or stable-registry activation is included.\n");
    const managed = ["prebuilds", "prebuild-manifest.json", "prebuild-sbom.json", "prebuild-release-receipt.json", "NOTICE"];
    replaceManaged(outputRoot, stage, managed);
    return releaseReceipt;
  } finally {
    rmSync(stage, { recursive: true, force: true });
  }
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  try {
    process.stdout.write(`${JSON.stringify(assemble(process.argv.slice(2)))}\n`);
  } catch (error) {
    process.stderr.write(`${error?.message ?? error}\n`);
    process.exitCode = error instanceof UsageError ? 2 : 1;
  }
}
