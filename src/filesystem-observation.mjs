import path from "node:path";
import { digestDocument } from "skill-family-contracts";
import { digestBytes } from "./closure.mjs";
import { HARNESS_ERROR_KINDS, mechanismError } from "./errors.mjs";
import { assertRelativePath, createFilesystemRootBinding } from "./bound-read.mjs";
import { loadNativeBoundReadAddon } from "./native/loader.mjs";

const SHA256_PATTERN = /^[0-9a-f]{64}$/u;

/**
 * Observe every directory and regular-file member of one already bound tree.
 * Native code opens each member relative to the root and supplies its bytes
 * and descriptor facts; this layer only derives the public contract shape.
 */
export async function observeFilesystemTree({ root, rootBinding } = {}) {
  if (typeof root !== "string" || !path.isAbsolute(root) || path.normalize(root) !== root) {
    throw new TypeError("observeFilesystemTree: root must be a normalized absolute path");
  }
  if (!rootBinding || typeof rootBinding !== "object" ||
      typeof rootBinding.digest !== "string" || !SHA256_PATTERN.test(rootBinding.digest)) {
    throw new TypeError("observeFilesystemTree: rootBinding is required");
  }
  const startBinding = await createFilesystemRootBinding(root);
  if (startBinding.digest !== rootBinding.digest) {
    throw mechanismError(HARNESS_ERROR_KINDS.UNSAFE_STATE_ENTRY, "approved root binding does not match the current root", {
      boundReadDisposition: "boundary-indeterminate",
    });
  }
  const { addon } = await loadNativeBoundReadAddon();
  if (typeof addon.observeFilesystemTreeNative !== "function") {
    throw mechanismError(HARNESS_ERROR_KINDS.UNSUPPORTED_PLATFORM, "native filesystem observation is unavailable in this prebuild");
  }
  let native;
  try {
    native = addon.observeFilesystemTreeNative(root);
  } catch (cause) {
    throw mechanismError(HARNESS_ERROR_KINDS.READ_FAILED, "filesystem tree observation failed", {
      cause: cause?.message ?? "unknown",
    });
  }
  const endBinding = await createFilesystemRootBinding(root);
  if (endBinding.digest !== rootBinding.digest || native.rootDevice === undefined || native.rootInode === undefined ||
      Number(native.rootMode) !== Number.parseInt(native.rootMode, 10)) {
    throw mechanismError(HARNESS_ERROR_KINDS.UNSAFE_STATE_ENTRY, "root identity changed during filesystem tree observation", {
      boundReadDisposition: "boundary-indeterminate",
    });
  }
  const paths = new Set();
  const members = Array.from(native.members ?? [], (member) => {
    assertRelativePath(member.path);
    const alias = member.path.toLowerCase();
    if (paths.has(alias)) throw mechanismError(HARNESS_ERROR_KINDS.INVALID_PATH, "filesystem tree contains duplicate or case-alias member paths");
    paths.add(alias);
    if (member.type === "directory") {
      return { path: member.path, type: "directory", statMode: Number(member.statMode) };
    }
    const bytes = Buffer.from(member.content);
    return {
      path: member.path,
      type: "file",
      sha256: digestBytes(bytes),
      bytes: bytes.length,
      statMode: Number(member.statMode),
      contentBase64: bytes.toString("base64"),
    };
  }).sort((left, right) => left.path < right.path ? -1 : left.path > right.path ? 1 : 0);
  return Object.freeze({
    schemaVersion: 1,
    kind: "skill-family.filesystem-tree-observation",
    rootBinding,
    members: Object.freeze(members.map((member) => Object.freeze(member))),
    membersDigest: digestDocument(members),
  });
}
