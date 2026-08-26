#!/usr/bin/env node
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";
import { assertSha256Hex, runCli } from "./receipt-assembler-core.mjs";

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
const EXPORTS = Object.freeze(["closeParentDirectory", "observeFilesystemTreeNative", "openParentDirectory", "platform", "readFileBoundNative", "renameDirectoryNoReplace"]);
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

runCli({
  keys: KEYS, platforms: PLATFORMS, sourceFile: SOURCE, mode: 0o644, napi: 10, exports: EXPORTS,
  binaryName: (key) => `bound_read.${key}.node`, extractFacts,
  manifestDoc: (entries) => ({ schemaVersion: 1, kind: "skill-family.filesystem-bound-read-prebuild-manifest", status: "stable", entries }),
  sbomDoc: (sourceSha, entries) => ({ schemaVersion: 1, kind: "skill-family.filesystem-bound-read-prebuild-sbom", status: "stable", source: { path: "packages/skill-family-harness-node/src/native/bound_read.c", sha256: sourceSha }, buildRecipe: { path: "packages/skill-family-harness-node/src/native/binding.gyp", sha256: bindingGypSha256 }, files: entries.map(({ platformKey, binary, sha256: digest }) => ({ platformKey, binary, sha256: digest, license: "Apache-2.0" })) }),
  releaseReceiptDoc: ({ inputs, sourceSha, manifestSha }) => ({ schemaVersion: 1, kind: "skill-family.filesystem-bound-read-prebuild-release-receipt", status: "stable", node: { version: "v22.23.2", napi: 10 }, source: { path: "packages/skill-family-harness-node/src/native/bound_read.c", sha256: sourceSha }, buildRecipe: { path: "packages/skill-family-harness-node/src/native/binding.gyp", sha256: bindingGypSha256 }, manifestSha256: manifestSha, inputs: inputs.map(({ key, receiptSha, binarySha }) => ({ platformKey: key, platformReceiptSha256: receiptSha, binarySha256: binarySha })) }),
  noticeText: "Skill Family Foundation filesystem bound-read stable prebuilds\nGenerated only from four verified platform receipts.\n",
  managedNames: ["prebuilds", "prebuild-manifest.json", "prebuild-sbom.json", "prebuild-release-receipt.json", "NOTICE"],
}, process.argv.slice(2));
