/**
 * skill-family-harness-node: the default Node mechanism runtime for Skill
 * Family engineering contracts.
 *
 * The harness is a thin runtime: it consumes skill-family-contracts (envelopes,
 * schemas, dialect routing, kernel protocol, stable error codes, fixtures)
 * and implements mechanism only — validator caching, contained filesystem
 * access, atomic writes, temporary workspaces, resource closure, and the
 * operation-request -> operation-result pipeline. It owns no business
 * semantics, no orchestration, no git, no network, and no second language.
 */

export const HARNESS_CAPABILITIES = Object.freeze([
  "schema-validation",
  "atomic-write",
  "path-containment",
  "temporary-workspace",
  "resource-closure",
  "operation-envelope",
]);

export const HARNESS_EXCLUSIONS = Object.freeze([
  "business-semantics",
  "workflow-orchestration",
  "git-writes",
  "model-calls",
  "release-state",
  "remote-network-access",
]);

export { HarnessError, HARNESS_ERROR_KINDS, mechanismError } from "./errors.mjs";

export { classifyPathInput, resolveContained, readFileContained } from "./paths.mjs";

export { writeFileAtomic } from "./atomic.mjs";

export {
  TemporaryWorkspace,
  createTemporaryWorkspace,
  withTemporaryWorkspace,
} from "./workspace.mjs";

export { digestBytes, computeResourceClosure, closureContains } from "./closure.mjs";

export {
  resolveSchemaContext,
  getValidator,
  validatorCacheSize,
  validateContractDocument,
} from "./validation.mjs";

export { parseRequest, processRequest } from "./request.mjs";
