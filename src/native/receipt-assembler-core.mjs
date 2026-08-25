/**
 * Shared receipt-driven prebuild assembler core.
 *
 * Internal module: not exported from the package exports map, no public API.
 * The candidate assembler (rename-directory-no-replace) and the stable
 * assembler (filesystem bound-read) are thin per-addon configurations on top
 * of this core; there is no second assembler implementation.
 *
 * The core owns the responsibilities the assemblers may not duplicate:
 *  - fixed argument parsing and the four-platform closure check;
 *  - platform receipt, source, build recipe, Node, N-API, exports, binary
 *    digest, size and mode validation;
 *  - stage generation and mechanical manifest / SBOM / release receipt
 *    derivation;
 *  - controlled replacement and failure recovery.
 *
 * Per-addon differences (receipt document shape, manifest kinds, binary
 * naming, NOTICE text) stay in each thin configuration. All input and stage
 * closure validation completes before the first target write.
 */
import { createHash } from "node:crypto";
import {
  chmodSync,
  copyFileSync,
  lstatSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  readdirSync,
  renameSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import path from "node:path";

export class UsageError extends Error {}
export const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
export const canonical = (value) => `${JSON.stringify(value, null, 2)}\n`;

const SHA256_PATTERN = /^[0-9a-f]{64}$/u;

export function parseArgs(keys, argv) {
  const allowed = new Set(["output-root", ...keys.flatMap((key) => [`${key}-receipt`, `${key}-binary`])]);
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

export function readJson(file) {
  const link = lstatSync(file);
  if (!link.isFile() || link.isSymbolicLink() || link.nlink !== 1) throw new Error(`receipt必须是单链接普通文件：${file}`);
  // One read: the same Buffer feeds both the strict UTF-8 JSON parse and the
  // SHA.  BOM bytes are not stripped by the decoder, so a BOM-prefixed
  // receipt fails JSON.parse instead of being silently accepted.
  const bytes = readFileSync(file);
  let document;
  try {
    document = JSON.parse(new TextDecoder("utf-8", { fatal: true, ignoreBOM: true }).decode(bytes));
  } catch (cause) {
    throw new Error(`receipt必须是严格UTF-8 JSON：${file}（${cause.message}）`);
  }
  return { document, sha256: sha256(bytes) };
}

export function verifyBinary(key, file, facts) {
  const link = lstatSync(file);
  if (!link.isFile() || link.isSymbolicLink() || link.nlink !== 1) throw new Error(`${key}: binary必须是单链接普通文件`);
  if ((link.mode & 0o777) !== facts.binaryMode) throw new Error(`${key}: binary mode与receipt不匹配`);
  if (facts.binarySize !== undefined && link.size !== facts.binarySize) throw new Error(`${key}: binary size与receipt不匹配`);
  if (sha256(readFileSync(file)) !== facts.binarySha) throw new Error(`${key}: binary SHA与receipt不匹配`);
}

/**
 * Controlled replacement of the managed closure members.
 *
 * Rollback only touches members that were actually installed from the stage
 * in this run or were actually moved into the backup: un-processed old
 * members are never removed. When a restore does not complete successfully
 * the backup is kept in place and the failure is rethrown (fail closed).
 */
export function replaceManaged(outputRoot, stage, names) {
  const backup = mkdtempSync(path.join(path.dirname(outputRoot), ".native-prebuild-backup-"));
  const moved = new Set();
  const installed = new Set();
  try {
    mkdirSync(outputRoot, { recursive: true });
    for (const name of names) {
      const target = path.join(outputRoot, name);
      try {
        lstatSync(target);
        const saved = path.join(backup, name);
        mkdirSync(path.dirname(saved), { recursive: true });
        renameSync(target, saved);
        moved.add(name);
      } catch (error) {
        if (error?.code !== "ENOENT") throw error;
      }
      renameSync(path.join(stage, name), target);
      installed.add(name);
    }
  } catch (error) {
    let restoreFailed = false;
    for (const name of [...names].reverse()) {
      if (!installed.has(name) && !moved.has(name)) continue;
      const target = path.join(outputRoot, name);
      if (installed.has(name)) {
        try { rmSync(target, { recursive: true, force: true }); } catch { restoreFailed = true; }
      }
      if (moved.has(name)) {
        try { renameSync(path.join(backup, name), target); } catch { restoreFailed = true; }
      }
    }
    if (!restoreFailed) rmSync(backup, { recursive: true, force: true });
    throw error;
  }
  rmSync(backup, { recursive: true, force: true });
}

function buildStage(config, outputRoot, inputs, sourceSha) {
  const stage = mkdtempSync(path.join(path.dirname(outputRoot), ".native-prebuild-stage-"));
  const entries = [];
  try {
    for (const input of inputs) {
      const filename = config.binaryName(input.key);
      const relative = `prebuilds/${input.key}/${filename}`;
      const destination = path.join(stage, relative);
      mkdirSync(path.dirname(destination), { recursive: true });
      copyFileSync(input.binaryPath, destination);
      chmodSync(destination, config.mode);
      entries.push({
        platformKey: input.key,
        os: config.platforms[input.key].os,
        arch: config.platforms[input.key].arch,
        libc: config.platforms[input.key].libc,
        binary: relative,
        mode: config.mode,
        sha256: input.binarySha,
        napi: Number(config.napi),
        exports: config.exports,
      });
    }
    const manifest = config.manifestDoc(entries);
    const manifestSha = sha256(Buffer.from(canonical(manifest)));
    const sbom = config.sbomDoc(sourceSha, entries);
    const releaseReceipt = config.releaseReceiptDoc({ inputs, sourceSha, manifestSha });
    writeFileSync(path.join(stage, "prebuild-manifest.json"), canonical(manifest));
    writeFileSync(path.join(stage, "prebuild-sbom.json"), canonical(sbom));
    writeFileSync(path.join(stage, "prebuild-release-receipt.json"), canonical(releaseReceipt));
    writeFileSync(path.join(stage, "NOTICE"), config.noticeText);
    const expected = new Set([
      "NOTICE",
      "prebuild-manifest.json",
      "prebuild-release-receipt.json",
      "prebuild-sbom.json",
      ...entries.map((entry) => entry.binary),
    ]);
    const actual = new Set();
    function collect(relative = "") {
      for (const name of readdirSync(path.join(stage, relative))) {
        const child = relative ? `${relative}/${name}` : name;
        const info = lstatSync(path.join(stage, child));
        if (info.isDirectory()) collect(child);
        else if (info.isFile() && !info.isSymbolicLink()) actual.add(child);
        else throw new Error(`stage contains a non-regular member: ${child}`);
      }
    }
    collect();
    if (JSON.stringify([...actual].sort()) !== JSON.stringify([...expected].sort())) throw new Error("stage closure does not equal the managed prebuild closure");
    for (const entry of entries) {
      if (sha256(readFileSync(path.join(stage, entry.binary))) !== entry.sha256) throw new Error(`stage binary digest drifted: ${entry.platformKey}`);
    }
    if (sha256(readFileSync(path.join(stage, "prebuild-manifest.json"))) !== manifestSha ||
        sha256(readFileSync(path.join(stage, "prebuild-sbom.json"))) !== sha256(Buffer.from(canonical(sbom))) ||
        sha256(readFileSync(path.join(stage, "prebuild-release-receipt.json"))) !== sha256(Buffer.from(canonical(releaseReceipt)))) {
      throw new Error("stage generated document digest drifted");
    }
    replaceManaged(outputRoot, stage, config.managedNames);
    return releaseReceipt;
  } finally {
    rmSync(stage, { recursive: true, force: true });
  }
}

/**
 * Runs one fixed four-platform assembly. Every receipt is parsed, its facts
 * extracted and cross-checked against the current source and the supplied
 * binary before `buildStage` performs the first target write.
 */
export function run(config, argv) {
  const args = parseArgs(config.keys, argv);
  const sourceSha = sha256(readFileSync(config.sourceFile));
  const inputs = [];
  for (const key of config.keys) {
    const receiptPath = path.resolve(args.get(`${key}-receipt`));
    const binaryPath = path.resolve(args.get(`${key}-binary`));
    const receipt = readJson(receiptPath);
    const facts = config.extractFacts(key, receipt.document, { sourceSha });
    if (facts.sourceSha !== sourceSha) throw new Error(`${key}: receipt source SHA与current native source不匹配`);
    verifyBinary(key, binaryPath, facts);
    inputs.push({ key, receiptSha: receipt.sha256, binaryPath, ...facts });
  }
  if (new Set(inputs.map((entry) => entry.sourceSha)).size !== 1) throw new Error("四份receipt的native source SHA不一致");
  const outputRoot = path.resolve(args.get("output-root"));
  return buildStage(config, outputRoot, inputs, sourceSha);
}

export function runCli(config, argv) {
  try {
    process.stdout.write(`${JSON.stringify(run(config, argv))}\n`);
  } catch (error) {
    process.stderr.write(`${error?.message ?? error}\n`);
    process.exitCode = error instanceof UsageError ? 2 : 1;
  }
}

export function assertSha256Hex(value, label) {
  if (typeof value !== "string" || !SHA256_PATTERN.test(value)) throw new Error(`${label} must be a lowercase sha256 hex digest`);
  return value;
}
