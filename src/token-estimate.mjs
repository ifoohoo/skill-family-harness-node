/**
 * Deterministic token upper-bound estimation (FND-ADR-009).
 *
 * estimateTokenUpperBound is a pure function: no model call, no tokenizer
 * dependency, no network, no persistent state import. It returns the exact
 * UTF-8 byte count of the input text as a conservative upper bound for
 * byte-level BPE tokenizers. The result document follows the frozen
 * token-estimate-result contract and carries no timestamp.
 *
 * Scope guarantee: content text only. Model control tokens and non-text
 * content are not covered, and the bias direction (never underestimating a
 * byte-level tokenizer's byte budget) is declared in the contract.
 */

const GUARANTEES = Object.freeze([
  "covers-content-text-only",
  "no-model-calls",
  "no-tokenizer-dependency",
  "no-network-access",
  "deterministic-byte-count",
]);

export function estimateTokenUpperBound(text) {
  if (typeof text !== "string") {
    throw new TypeError("estimateTokenUpperBound: text must be a string");
  }
  const inputBytes = Buffer.byteLength(text, "utf8");
  return Object.freeze({
    schemaVersion: 1,
    kind: "skill-family.token-estimate-result",
    algorithm: "utf8-byte-count",
    unit: "bytes",
    inputBytes,
    upperBound: inputBytes,
    precision: "exact",
    scope: "content-text-only",
    guarantees: GUARANTEES,
  });
}
