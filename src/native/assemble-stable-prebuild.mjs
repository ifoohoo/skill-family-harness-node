#!/usr/bin/env node
import { createHash } from "node:crypto";
import { chmodSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";
import { WINDOWS_NATIVE_EXPORTS } from "./loader.mjs";
import { assertSha256Hex, buildStage, readJson, runCli, verifyBinary } from "./receipt-assembler-core.mjs";

const ROOT = path.dirname(fileURLToPath(import.meta.url));
const SOURCE = path.join(ROOT, "bound_read.c");
const BINDING_GYP = path.join(ROOT, "binding.gyp");
const KEYS = Object.freeze(["darwin-arm64", "darwin-x64", "linux-arm64-gnu", "linux-x64-gnu"]);
const PLATFORMS = Object.freeze({
  "darwin-arm64": { os: "darwin", arch: "arm64", libc: "none" },
  "darwin-x64": { os: "darwin", arch: "x64", libc: "none" },
  "linux-arm64-gnu": { os: "linux", arch: "arm64", libc: "glibc" },
  "linux-x64-gnu": { os: "linux", arch: "x64", libc: "glibc" },
});
const EXPORTS = Object.freeze(["closeParentDirectory", "exchangeDirectories", "observeFilesystemTreeNative", "openParentDirectory", "platform", "readFileBoundNative", "renameDirectoryNoReplace"]);
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const bindingGypSha256 = sha256(readFileSync(BINDING_GYP));

function extractFacts(key, receipt, { sourceSha }) {
  const expected = PLATFORMS[key];
  if (receipt?.kind !== "skill-family.filesystem-bound-read-platform-receipt" || receipt.status !== "VERIFIED" ||
      receipt.actualExecution !== true || receipt.platformKey !== key || receipt.node?.version !== "v22.23.2" ||
      Number(receipt.node?.napi) !== 10 || receipt.platform?.os !== expected.os || receipt.platform?.arch !== expected.arch ||
      receipt.platform?.libc !== expected.libc || receipt.source?.sha256 !== sourceSha ||
      receipt.buildRecipe?.sha256 !== bindingGypSha256 || receipt.addon?.mode !== "0644" ||
      receipt.addon?.size === undefined || JSON.stringify(receipt.addon?.exports) !== JSON.stringify(EXPORTS) ||
      !Array.isArray(receipt.oracle?.results) || receipt.oracle.results.length === 0 || !receipt.oracle.results.every((result) => result.status === "PASS")) {
    throw new Error(`${key}: stable receipt does not prove the fixed platform execution contract`);
  }
  return { sourceSha, binarySha: assertSha256Hex(receipt.addon.sha256, `${key} addon sha256`), binarySize: receipt.addon.size, binaryMode: 0o644 };
}

function flagValue(argv, name) {
  const index = argv.indexOf(name);
  if (index < 0 || argv[index + 1] === undefined || argv[index + 1].startsWith("--")) {
    throw new Error(`${name} requires one value`);
  }
  return argv[index + 1];
}

function writeVerifiedWindowsPrebuild(argv) {
  const receiptPath = flagValue(argv, "--windows-receipt");
  const binaryPath = flagValue(argv, "--windows-binary");
  const outputRoot = path.resolve(flagValue(argv, "--output-root"));
  const sourcePath = path.join(ROOT, "windows_process.c");
  const sourceSha = sha256(readFileSync(sourcePath));
  const receiptInput = readJson(receiptPath);
  const receipt = receiptInput.document;
  const expectedExports = ["platform", ...WINDOWS_NATIVE_EXPORTS].sort();
  if (receipt?.kind !== "skill-family.filesystem-bound-read-platform-receipt" || receipt.actualExecution !== true ||
      receipt.platformKey !== "win32-x64" || receipt.status !== "VERIFIED" || receipt.source?.sha256 !== sourceSha ||
      receipt.node?.version !== "v22.22.2" || receipt.node?.napi !== 10 || receipt.platform?.os !== "win32" || receipt.platform?.arch !== "x64" || receipt.addon?.mode !== "0644" ||
      receipt.buildRecipe?.sha256 !== bindingGypSha256 || !/^[0-9a-f]{64}$/u.test(receipt.buildCommandSha256 ?? "") ||
      JSON.stringify(receipt.addon?.exports) !== JSON.stringify(expectedExports) ||
      !Array.isArray(receipt.oracle?.results) || receipt.oracle.results.length === 0 || !receipt.oracle.results.every(item => item.status === "PASS")) {
    throw new Error("Windows receipt does not bind an actual verified native execution to the current source and build recipe");
  }
  verifyBinary("win32-x64", binaryPath, { binaryMode: 0o644, binarySize: receipt.addon.size, binarySha: receipt.addon.sha256 });
  const binary = readFileSync(binaryPath);
  if (sha256(binary) !== receipt.addon.sha256 || binary.length !== receipt.addon.size) throw new Error("Windows binary does not match its execution receipt");
  // Retain the previously verified POSIX distribution and its historical build
  // facts. Adding a separate Windows source does not pretend to rebuild it.
  const manifest = readJson(path.join(ROOT, "prebuild-manifest.json"));
  const sbom = readJson(path.join(ROOT, "prebuild-sbom.json")).document;
  const release = readJson(path.join(ROOT, "prebuild-release-receipt.json")).document;
  const posixEntries = manifest.document.entries.filter(entry => KEYS.includes(entry.platformKey));
  if (JSON.stringify(posixEntries.map(entry => entry.platformKey)) !== JSON.stringify(KEYS) ||
      release.manifestSha256 !== manifest.sha256 || release.source.sha256 !== sha256(readFileSync(SOURCE)) ||
      sbom.source.sha256 !== release.source.sha256) throw new Error("Existing POSIX release closure or source binding is not intact");
  const inputs = posixEntries.map(entry => {
    const existingBinary = path.join(ROOT, entry.binary);
    if (entry.mode !== 0o644) throw new Error(`Existing POSIX distribution mode is not intact: ${entry.platformKey}`);
    verifyBinary(entry.platformKey, existingBinary, { binaryMode: entry.mode, binarySha: entry.sha256 });
    const bytes = readFileSync(existingBinary);
    const proof = release.inputs.find(item => item.platformKey === entry.platformKey);
    const resource = sbom.files.find(item => item.platformKey === entry.platformKey);
    if (entry.sha256 !== sha256(bytes) || proof?.binarySha256 !== entry.sha256 || resource?.sha256 !== entry.sha256 ||
        JSON.stringify(entry.exports) !== JSON.stringify(EXPORTS)) throw new Error(`Existing POSIX binary closure is not intact: ${entry.platformKey}`);
    return { key: entry.platformKey, binaryPath: existingBinary, binarySha: entry.sha256, receiptSha: proof.platformReceiptSha256, exports: entry.exports };
  });
  inputs.push({ key: "win32-x64", binaryPath, binarySha: receipt.addon.sha256, receiptSha: receiptInput.sha256, exports: expectedExports });
  const windowsSource = { path: "packages/skill-family-harness-node/src/native/windows_process.c", sha256: sourceSha };
  const windowsBuild = { path: "packages/skill-family-harness-node/src/native/binding.gyp", sha256: bindingGypSha256, commandSha256: receipt.buildCommandSha256 };
  return buildStage({
    platforms: { ...PLATFORMS, "win32-x64": { os: "win32", arch: "x64", libc: "none" } }, mode: 0o644, napi: 10, exports: EXPORTS,
    binaryName: key => `bound_read.${key}.node`,
    manifestDoc: entries => ({ ...manifest.document, entries }),
    sbomDoc: (_source, entries) => ({ ...sbom, platformSources: { ...(sbom.platformSources ?? {}), "win32-x64": windowsSource },
      platformBuildRecipes: { ...(sbom.platformBuildRecipes ?? {}), "win32-x64": windowsBuild },
      files: entries.map(entry => ({ platformKey: entry.platformKey, binary: entry.binary, sha256: entry.sha256, license: "Apache-2.0" })) }),
    releaseReceiptDoc: ({ manifestSha }) => ({ ...release, manifestSha256: manifestSha,
      platformSources: { ...(release.platformSources ?? {}), "win32-x64": windowsSource },
      platformBuildRecipes: { ...(release.platformBuildRecipes ?? {}), "win32-x64": windowsBuild },
      inputs: inputs.map(item => ({ platformKey: item.key, platformReceiptSha256: item.receiptSha, binarySha256: item.binarySha })) }),
    noticeText: "Skill Family Foundation filesystem bound-read stable prebuilds\nGenerated from verified platform receipts; POSIX release facts retained.\n",
    managedNames: ["prebuilds", "prebuild-manifest.json", "prebuild-sbom.json", "prebuild-release-receipt.json", "NOTICE"],
  }, outputRoot, inputs, release.source.sha256);
}

const argv = process.argv.slice(2);
if (argv.includes("--windows-receipt")) {
  writeVerifiedWindowsPrebuild(argv);
} else {
  runCli({
    keys: KEYS, platforms: PLATFORMS, sourceFile: SOURCE, mode: 0o644, napi: 10, exports: EXPORTS,
    binaryName: (key) => `bound_read.${key}.node`, extractFacts,
    manifestDoc: (entries) => ({ schemaVersion: 1, kind: "skill-family.filesystem-bound-read-prebuild-manifest", status: "stable", entries }),
    sbomDoc: (sourceSha, entries) => ({ schemaVersion: 1, kind: "skill-family.filesystem-bound-read-prebuild-sbom", status: "stable", source: { path: "packages/skill-family-harness-node/src/native/bound_read.c", sha256: sourceSha }, buildRecipe: { path: "packages/skill-family-harness-node/src/native/binding.gyp", sha256: bindingGypSha256 }, files: entries.map(({ platformKey, binary, sha256: digest }) => ({ platformKey, binary, sha256: digest, license: "Apache-2.0" })) }),
    releaseReceiptDoc: ({ inputs, sourceSha, manifestSha }) => ({ schemaVersion: 1, kind: "skill-family.filesystem-bound-read-prebuild-release-receipt", status: "stable", node: { version: "v22.23.2", napi: 10 }, source: { path: "packages/skill-family-harness-node/src/native/bound_read.c", sha256: sourceSha }, buildRecipe: { path: "packages/skill-family-harness-node/src/native/binding.gyp", sha256: bindingGypSha256 }, manifestSha256: manifestSha, inputs: inputs.map(({ key, receiptSha, binarySha }) => ({ platformKey: key, platformReceiptSha256: receiptSha, binarySha256: binarySha })) }),
    noticeText: "Skill Family Foundation filesystem bound-read stable prebuilds\nGenerated only from four verified platform receipts.\n",
    managedNames: ["prebuilds", "prebuild-manifest.json", "prebuild-sbom.json", "prebuild-release-receipt.json", "NOTICE"],
  }, argv);
}
