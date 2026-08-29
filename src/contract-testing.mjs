import { createHash } from "node:crypto";
import path from "node:path";
import {
  ContractsError,
  assertRegisteredErrorCode,
  verifyConsumerContractVector,
} from "skill-family-contracts";
import { HARNESS_ERROR_KINDS, mechanismError } from "./errors.mjs";
import { FOUNDATION_PACKAGE_VERSION } from "./version.mjs";

const ATOMIC_WRITE_CAPABILITY = "foundation.harness.atomic-write";
const ATOMIC_WRITE_VECTOR_SET = "foundation.harness.atomic-write.consumer-v1";
const ATOMIC_WRITE_CONTRACT = "skill-family-harness-node:writeFileAtomic";

function deepFreeze(value, seen = new Set()) {
  if (value === null || typeof value !== "object" || seen.has(value)) return value;
  seen.add(value);
  for (const child of Object.values(value)) deepFreeze(child, seen);
  return Object.freeze(value);
}

function sameValue(left, right, seen = new Set()) {
  if (Object.is(left, right)) return true;
  if (left === null || right === null || typeof left !== typeof right) return false;
  if (typeof left !== "object") return false;
  if (Buffer.isBuffer(left) || Buffer.isBuffer(right)) {
    return Buffer.isBuffer(left) && Buffer.isBuffer(right) && left.equals(right);
  }
  if (Array.isArray(left) || Array.isArray(right)) {
    return Array.isArray(left) && Array.isArray(right) && left.length === right.length &&
      left.every((value, index) => sameValue(value, right[index], seen));
  }
  if (seen.has(left)) return true;
  seen.add(left);
  const leftKeys = Object.keys(left).sort();
  const rightKeys = Object.keys(right).sort();
  return leftKeys.length === rightKeys.length &&
    leftKeys.every((key, index) => key === rightKeys[index] && sameValue(left[key], right[key], seen));
}

function dataSummary(data) {
  if (typeof data === "string") return data;
  if (Buffer.isBuffer(data)) {
    return {
      kind: "Buffer",
      bytes: data.byteLength,
      sha256: createHash("sha256").update(data).digest("hex"),
    };
  }
  if (data instanceof ArrayBuffer || ArrayBuffer.isView(data)) {
    const bytes = data instanceof ArrayBuffer
      ? Buffer.from(data)
      : Buffer.from(data.buffer, data.byteOffset, data.byteLength);
    return {
      kind: data.constructor?.name ?? "ArrayBufferView",
      bytes: bytes.byteLength,
      sha256: createHash("sha256").update(bytes).digest("hex"),
    };
  }
  return undefined;
}

function cloneForObservation(value, seen = new Map()) {
  if (value === null || typeof value !== "object") return value;
  if (Buffer.isBuffer(value)) return dataSummary(value);
  if (seen.has(value)) return "[Circular]";
  seen.set(value, true);
  if (Array.isArray(value)) return value.map((child) => cloneForObservation(child, seen));
  return Object.fromEntries(Object.keys(value).sort().map((key) => [key, cloneForObservation(value[key], seen)]));
}

function contractMismatch(validation) {
  const code = validation?.mismatchCode ?? "SFC1001";
  return new ContractsError(code, "atomic-write fake requires an official verified consumer contract vector", {
    vectorId: validation?.vectorId ?? null,
    observed: validation,
  });
}

function invalidRequest(message, details) {
  return mechanismError(HARNESS_ERROR_KINDS.ATOMIC_WRITE_FAILED, message, details);
}

function requestMatches(vector, root, relPath, data, mode) {
  const expected = vector.request;
  return typeof root === "string" && root.length > 0 && path.isAbsolute(root) &&
    path.normalize(root) === root &&
    relPath === expected.relPath && sameValue(data, expected.data) &&
    sameValue({ mode }, expected.options);
}

/**
 * Creates the atomic-write fake published for the exact Foundation vector set.
 * The fake is deliberately single-use and performs no filesystem operation.
 */
export function createAtomicWriteFake({ vector } = {}) {
  let vectorSnapshot;
  try {
    vectorSnapshot = structuredClone(vector);
  } catch (cause) {
    throw new ContractsError("SFC1001", "atomic-write fake requires a structured consumer contract vector", {
      cause: cause?.name ?? "DataCloneError",
    });
  }
  const validation = verifyConsumerContractVector(vectorSnapshot, {
    capabilityId: ATOMIC_WRITE_CAPABILITY,
    foundationVersion: FOUNDATION_PACKAGE_VERSION,
    vectorSetId: ATOMIC_WRITE_VECTOR_SET,
  });
  if (!validation.ok) throw contractMismatch(validation);
  if (vectorSnapshot.contractId !== ATOMIC_WRITE_CONTRACT || vectorSnapshot.strategy !== "official-fake") {
    throw contractMismatch({ ...validation, mismatchCode: "SFC1013" });
  }
  deepFreeze(vectorSnapshot);

  const identity = deepFreeze({
    capabilityId: ATOMIC_WRITE_CAPABILITY,
    vectorSetId: ATOMIC_WRITE_VECTOR_SET,
    foundationVersion: FOUNDATION_PACKAGE_VERSION,
  });
  let consumed = false;
  let state = { callCount: 0, request: null, outcome: "indeterminate" };

  function observation() {
    return deepFreeze(cloneForObservation(state));
  }

  async function writeFileAtomic(root, relPath, data, { mode = 0o644 } = {}) {
    if (consumed) {
      throw invalidRequest("atomic-write fake vector was already consumed", { vectorId: vectorSnapshot.vectorId });
    }
    if (!requestMatches(vectorSnapshot, root, relPath, data, mode)) {
      throw invalidRequest("atomic-write fake received a request different from its verified vector", {
        vectorId: vectorSnapshot.vectorId,
      });
    }
    consumed = true;
    state = {
      callCount: 1,
      request: {
        root: cloneForObservation(root),
        relPath: cloneForObservation(relPath),
        data: cloneForObservation(data),
        options: cloneForObservation({ mode }),
      },
      outcome: vectorSnapshot.expected.outcome,
    };

    if (vectorSnapshot.expected.outcome === "return" || vectorSnapshot.expected.outcome === "indeterminate") {
      const value = path.join(root, vectorSnapshot.request.relPath);
      if (!path.isAbsolute(value) || value === root) {
        state = { ...state, outcome: "throw", errorCode: "SFC2004" };
        throw invalidRequest("atomic-write fake could not project an absolute target", { vectorId: vectorSnapshot.vectorId });
      }
      state = { ...state, outcome: "return", value };
      return value;
    }

    const errorCode = vectorSnapshot.expected.errorCode ?? "SFC2004";
    const errorKind = vectorSnapshot.expected.errorKind;
    assertRegisteredErrorCode(errorCode);
    state = { ...state, errorCode, errorKind };
    if (errorCode === "SFC2004") {
      if (errorKind === HARNESS_ERROR_KINDS.PATH_TRAVERSAL) {
        throw mechanismError(errorKind, "atomic-write fake projected the vector failure", { vectorId: vectorSnapshot.vectorId });
      }
      throw invalidRequest("atomic-write fake projected the vector failure", { vectorId: vectorSnapshot.vectorId });
    }
    throw new ContractsError(errorCode, "atomic-write fake projected the vector failure", { vectorId: vectorSnapshot.vectorId });
  }

  return Object.freeze({ identity, writeFileAtomic, observation });
}
