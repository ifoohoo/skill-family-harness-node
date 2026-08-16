<!-- release-skill:safe-first-command -->
<!-- release-skill:external-write-boundary -->
> 简体中文版：[README.zh-CN.md](./README.zh-CN.md)

# skill-family-harness-node

<!-- release-skill:release-version: 0.5.0 -->

The **single default Node implementation** of the Contracts mechanism protocol. This is a thin runtime: it only implements the mechanism protocol, introduces no business semantics, and does not provide a second-language implementation.

<!-- release-skill:managed:start id=latest-release -->
**0.5.0** (2026-08-16)

This release adds the declared read surface assertion (FND-ADR-010) and the structured surface scanner (FND-ADR-011) to the stable Harness surface, growing HARNESS_CAPABILITIES from 16 to 18 entries, and adds three dependency-reviewed runtime libraries.

**Added**

- Adds assertDeclaredReadSurface (FND-ADR-010): a no-execution, syntax-surface-only assertion that every node:fs named import inside a declared module set stays inside the consumer-declared read surface, with a closed violation vocabulary and a frozen declared-read-surface-result envelope.
- Adds scanSurfaceStructured (FND-ADR-011): the structured sibling of scanSurface. IP-shaped candidates enter a single standard parse entry (ipaddr.js) with consumer-declared CIDR approval and fail closed when unparseable; scoped and unscoped coordinates, registries and hosts need consumer-declared approval; format adapters (pnpm-lockfile via @pnpm/lockfile.fs with yaml AST comment regions, and tree-json) parse structurally with no position-level exemptions; binary and symlink policies fail closed. The closed nine-rule vocabulary travels in details.rule of the structured-scan-violation mechanism error.
- Adds three dependency-reviewed runtime libraries for the structured scanner, reviewed per FND-ADR-006 section-4 analogy (dependency-closure pre-review, executor self-review pending independent review): ipaddr.js 2.5.0 (MIT, zero dependencies), yaml 2.9.0 (ISC), and @pnpm/lockfile.fs 1001.1.35 (MIT, 18 transitive dependencies all inside the approved public coordinates).
- Grows HARNESS_CAPABILITIES from 16 to 18 entries for the two new mechanisms.

**Changed**

- Documents the relationship of the two policy documents to workspace-private leak policies: a workspace-private leak-policy.json instance document is not a subset, not isomorphic and not a migration target of the surface-scan-policy or structured-scan-policy schemas — the documents share rule vocabulary and fail-closed semantics by design, but their byte-level shapes are independent and must not be compared for compatibility. scanSurface is the execution-core generalization projection: the public, consumer-parameterized form of the same mechanism family, without any private identity, path, or approval-list interpretation of its own.
- Records the cost of the dependency-review decision honestly: the harness tarball grows with the @pnpm/lockfile.fs closure, the thin-runtime property changes from zero third-party runtime dependencies to three reviewed ones, and the pnpm-lockfile adapter writes one temporary lockfile copy under the OS temp directory (removed afterwards). The engineering-kit offline-consumer verification gates now derive the complete third-party production closure of the three Foundation packages mechanically (identity-deduplicated, npm: alias-aware, range-scoped override selectors) instead of a single-package closure, so the review decision is continuously verified against the real installed bytes.
- Keeps mechanism purity: no execution of scanned files, no model calls, no network; symlinked entries are never followed.

**Upgrade Notes**

Version 0.5.0 is the FND-ADR-010/011 harness line. Mechanism imports use the stable capability names published in HARNESS_CAPABILITIES; structured-scan policies must pass the structured-scan-policy contract validation of contract-spec 1.5.0.
<!-- release-skill:managed:end id=latest-release -->

## Problem It Solves

Contracts defines "what should be", and the Harness turns that into "can be safely reused" mechanisms at the Node runtime. If multiple skill-family projects each implement path containment, atomic writes, resource closure, report rendering, host integration, and the state base independently, you get inconsistent security boundaries and behavioral drift. The Harness consolidates these business-neutral mechanisms into one default implementation, which callers pick up as needed.

## Core Mental Model

The Harness consumes `skill-family-contracts` (a workspace dependency), reusing its dialect-routed Ajv validator, Kernel Protocol, frozen error codes, and fixtures; it does not copy protocol definitions or re-interpret the Schema. It only implements mechanisms: Schema validation, atomic writes, path containment, temporary workspaces, resource closure, the operation-request → operation-result pipeline, and business-neutral event logging with derived snapshots. Explicitly excluded: business semantics, task orchestration, Git writes, model calls, remote networking, and publish state. See `HARNESS_EXCLUSIONS`.

## Installation and Minimal Example

```sh
npm install skill-family-harness-node@0.5.0
npm info skill-family-harness-node --help
```

The minimal example shows validating a contract document inside Node:

```js
// Run from an empty directory: npm install skill-family-harness-node@0.5.0
import { validateContractDocument } from "skill-family-harness-node";

const document = {
  schemaVersion: 1,
  kind: "skill-family.project-manifest",
  project: { id: "my-project", name: "My Project", description: "Example" },
  contracts: { version: "1.0.0", profile: "generic" },
  managedFiles: ["package.json"],
  updatedAt: "2026-01-01T00:00:00Z",
};

const result = validateContractDocument(document, {
  schemaId: "https://contracts.skill-family.example/v1/project-manifest.json",
});
if (!result.valid) console.error(result.errorCode);
```

The code above shows the basic `validateContractDocument` call; it reuses the Contracts validator and caches instances keyed by schema, without recompiling.

## Candidate Quickstart Profile

Use the candidate subpath to construct an observation-backed Task, wrap its terminal Result, and verify that both documents bind the exact observation bytes and correlation fields:

```js
import {
  createQuickstartTask,
  wrapQuickstartResult,
  verifyQuickstartExchange,
} from "skill-family-harness-node/candidate/quickstart-profile";
```

The v2 mechanism recomputes the bytes of every path-backed output and evidence Resource. It also rejects duplicate Resource ids, correlation drift, a changed Task digest, and incomplete or mismatched evidence bindings. It does not perform a domain audit, choose a method, retry work, or own lifecycle state.

The subpath is public but **not stable** and may change or be removed in a later minor release. Pin exactly `0.4.0` for v2; integrations that still produce candidate v1 exchanges must stay pinned to exactly `0.2.1`.

## Typical Use Cases

- Need to safely read/write contained paths inside Node: use path containment and atomic write.
- Need to normalize resources into a recomputable closure or generate a digest: use resource closure.
- Need to generate a human report from a machine result: use report model/render/binding/check.
- Need to persist an event log with derived snapshots: use state-store (event meaning is owned by the caller).

## Boundaries

- Consumes `skill-family-contracts`, reusing its dialect-routed Ajv validator, Kernel Protocol, frozen error codes, and fixtures; does not copy protocol definitions or re-interpret the Schema.
- Only implements mechanisms: Schema validation, atomic writes, path containment, temporary workspaces, resource closure, the operation-request → operation-result pipeline, and business-neutral event logging with derived snapshots.
- Explicitly excluded: business semantics, task orchestration, Git writes, model calls, remote networking, and publish state. See `HARNESS_EXCLUSIONS`.

## Public API

| Export | Responsibility |
| --- | --- |
| `HARNESS_CAPABILITIES` / `HARNESS_EXCLUSIONS` | Capability and exclusion lists (frozen constants). |
| `HarnessError` / `HARNESS_ERROR_KINDS` / `mechanismError` | Mechanism failures uniformly carry the registered error code `SFC2004`; `details.kind` gives a stable subcategory. |
| `validateContractDocument` / `getValidator` / `resolveSchemaContext` / `validatorCacheSize` | Routes and caches validators by Schema dialect; reuses Contracts' Ajv instances and dialect/policy semantics. |
| `classifyPathInput` / `resolveContained` / `readFileContained` | Path containment: intercepts path overruns, symlink escapes, and realpath escapes. |
| `writeFileAtomic` | Atomic write: leaves no half-written artifact on failure (temp file + fsync + rename). |
| `TemporaryWorkspace` / `createTemporaryWorkspace` / `withTemporaryWorkspace` | Auto-cleanup temporary workspace, cleaned up even on exception paths. |
| `digestBytes` / `computeResourceClosure` / `closureContains` | Resource closure and deterministic sha256 digest. |
| `parseRequest` / `processRequest` | Parse `operation-request`, output terminal `operation-result`. |
| `validateReportModel` / `renderReportMarkdown` / `buildBinding` / `checkReport` | Consume a Contracts-validated report model, deterministically render neutral Markdown, and verify source/result/report binding; does not interpret business output. |
| `normalizeAdapterSource` / `buildAdapterClosure` / `verifyAdapterBuildManifest` / `materializeAdapterBuild` | Generic text-source closure, full-manifest digest re-verification, and atomic landing of the target set; specific Profile/driver is not in the Harness. |
| `probeVersionVector` | A version-probe mechanism that disables spawn by default; when explicitly enabled, executes only absolute, symlink-free, audited vectors, using no PATH/shell. |
| `openStateStore` / `appendEvent` / `readEvents` / `verifyStateStore` / `closeStateStore` | Strict single-writer append-only event store; the event directory is the sole state authority, `chain-head.json` is only a cache. |
| `readSnapshot` / `writeSnapshot` / `rebuildSnapshot` | Atomic derived snapshots and full-event rebuild; a bad event cannot be masked by an old snapshot, and a bad snapshot can be ignored by rebuild. |
| `inspectStateStoreLock` / `recoverStateStoreLock` | Read-only lock diagnostics and explicit recovery; recovery must precisely match the observed owner + fencing. |

## State Store Lock and Recovery Boundaries

- The lock uses exclusive create; a second writer immediately receives `store-locked`; it does not queue, nor steals the lock by time, PID, or lease expiry.
- `inspectStateStoreLock` creates no file, only returns `owner`, monotonic `fencing`, `ageMs`, and an in-recovery flag. `ageMs` is for diagnostics only and never participates in correctness decisions.
- A crash-left lock can only be recovered by the caller, after confirming outside Foundation that the old writer has terminated, by calling `recoverStateStoreLock` while submitting the precisely matching `expectedOwner`, `expectedFencing`, and `confirmOwnerTerminated: true`. A mismatch or missing confirmation fails closed.
- Recovery produces a larger fencing. The old handle re-checks owner, fencing, and acquisition id on every append; final event publication uses a same-directory temporary regular file, fsync, and exclusive link, never overwriting an existing sequence.
- Append, snapshot, close, and recovery are serialized by a short-lived `writer-mutation.lock`; recovery cannot cross an authoritative write that already holds the mutation guard.
- If the recovering process itself crashes while holding `writer-recovery.lock`, the system stays in a diagnosable deadlock state and does not auto-delete that guard. It requires fresh external forensics and manual handling; the current API does not claim to solve the scenario where an untrusted caller falsely reports "old writer terminated".
- The state root, `events/`, `snapshots/`, events, and snapshots reject symlinks, hard links, FIFOs, devices, and other non-regular entries. Payload must be pure JSON, and `eventType + payloadSchemaVersion` must hit the Schema pair frozen by the caller at open/recover.

## Stable Error Codes

All reuse the Contracts frozen registry; no unregistered codes are added. Mechanism failures are uniformly `SFC2004` (EXECUTION_FAILED), and `details.kind` takes a stable value from `HARNESS_ERROR_KINDS`, such as `path-traversal`, `symlink-escape`, `realpath-escape`, `atomic-write-failed`, `missing-resource`, `workspace-disposed`.

Adding an entirely new SFC code is a Contracts change (the registry is inside the contracts package) and is outside this package's write set; therefore the combination "`SFC2004` + stable `details.kind`" keeps external semantics stable.

## Path Containment Model

`resolveContained(root, rel)` is the single entry point for all filesystem access, rejecting in order:

1. Input classification (`classifyPathInput`, a pure testable function): rejects absolute paths, Windows drive/UNC paths, backslash paths on POSIX, empty input, and NUL bytes.
2. Lexical containment: after `path.resolve`, leaving the root → `path-traversal`.
3. Symlink escape: the final component is a symlink (or dangling link) pointing outside the root → `symlink-escape`.
4. Realpath escape: any intermediate symlink chain's normalized result leaves the root → `realpath-escape`.

Comparison is based on the canonical root after `realpath`, avoiding misjudgment from system-level symlinks such as macOS `/var → /private/var`.

## Testing

`node --test` covers: full Contracts fixture replay, security negative cases, atomic-failure paths, temporary workspaces, closure determinism, report fact binding and Markdown injection, host manifest/path/command trust, and state-store crashes, concurrency, corruption, fencing, explicit recovery, symlinks, hard links, and FIFO negative cases.

## Troubleshooting

Mechanism failures uniformly throw `SFC2004` (EXECUTION_FAILED), with `details.kind` giving a stable subcategory (e.g., `path-traversal`, `atomic-write-failed`). If it fails, check that the root path is correct and the target file is not locked.

## Further Documentation

- Architecture boundaries and routing: [Architecture](https://ifoohoo.github.io/skill-family-engineering-kit/architecture/), [Agent architecture routing](https://ifoohoo.github.io/skill-family-engineering-kit/agents/architecture-routing/)
- Capability catalog: [capability-catalog.json](https://ifoohoo.github.io/skill-family-engineering-kit/agents/capability-catalog.json)
- Side-effect matrix: [Failure and side-effect matrix](https://ifoohoo.github.io/skill-family-engineering-kit/reference/failure-and-side-effect-matrix/)

<!-- agent-quick-reference:start -->
## Agent Quick Reference

### Use when

- You need to validate contracts inside Node, safely read/write contained paths, compute resource closures, or render deterministic reports.
- You need to persist an event log with derived snapshots, or normalize host adapter sources.
- You need to trial the non-stable Quickstart exchange and verify its observation/task/result binding.

### Do not use when

- You need to put file-selection business rules into the Foundation (business rules are owned by the caller).
- You need host apply/install/update/uninstall, a full Qoder driver, or binary adapter source (explicitly unsupported).
- You need domain audit semantics, retry orchestration, or a compatibility-frozen Quickstart API.

### Capability selection

- `foundation.harness.contract-validation`: contract validation and validator caching inside Node.
- `foundation.harness.path-containment`: path classification and contained resolution, rejecting the three escape types.
- `foundation.harness.atomic-write`: atomic write within contained paths, rolling back on failure.
- `foundation.harness.temporary-workspace`: auto-cleanup temporary workspace.
- `foundation.harness.resource-closure`: deterministic resource closure and sha256 digest.
- `foundation.harness.request-processing`: operation-request → terminal operation-result.
- `foundation.harness.report`: report-model validation/render/binding/check.
- `foundation.harness.host-adapter`: adapter source closure/build/materialize and version probe.
- `foundation.harness.state-store`: append-only events, hash chain, snapshots, and lock recovery.
- `foundation.harness.errors`: mechanism error types and stable error classes.
- `foundation.harness.quickstart-profile-candidate`: exact-version observation/task/result construction and binding verification.

### Required inputs

- The contained root directory (the boundary for path containment).
- The document, resource, or event payload to validate/write.

### Outputs and evidence

- Validation result, contained absolute path, atomically written file, closure digest, terminal result, report text, events/snapshots.
- Evidence: `packages/skill-family-harness-node/test/validation.test.mjs`, `atomic.test.mjs`, `containment.test.mjs`, `closure.test.mjs`, `report.test.mjs`, `state-store.test.mjs`.

### Side effects

- Read-only filesystem access (path/atomic/workspace/state-store read and write within contained paths).
- `HARNESS_EXCLUSIONS` explicitly excludes release-state, remote-network-access, business-semantics, workflow-orchestration, model-calls, git-writes.

### Failure semantics

- Mechanism failures are uniformly `SFC2004`, with `details.kind` as a stable subcategory (e.g., `path-traversal`, `atomic-write-failed`).
- Residual state after failure: atomic write rolls back the temp file; a broken state-store hash chain throws, and old snapshots can be ignored by rebuild.

### Architectural invariants

- Event meaning and reducer transitions remain consumer-owned; state-store only provides the base.
- Only text adapter source (utf8) is supported; binary projection is not supported.

### Route elsewhere when

- Business state machine / terminal states: route to loop-agent.
- Host apply: explicitly unsupported.
- Domain audit semantics: route to a standalone audit consumer.

### Machine-readable sources

- Public capability catalog: [`capability-catalog.json`](https://ifoohoo.github.io/skill-family-engineering-kit/agents/capability-catalog.json) (`foundation.harness.*` entries).
- Package-local source: `src/*.mjs`.
- Package-local candidate source: `candidate/quickstart-profile.mjs`; public import: `skill-family-harness-node/candidate/quickstart-profile`.
<!-- agent-quick-reference:end -->
