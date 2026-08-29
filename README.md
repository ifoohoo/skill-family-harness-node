<!-- release-skill:safe-first-command -->
<!-- release-skill:external-write-boundary -->
> 简体中文版：[README.zh-CN.md](./README.zh-CN.md)

# skill-family-harness-node

<!-- release-skill:release-version: 0.14.0 -->

The **single default Node implementation** of the Contracts mechanism protocol. This is a thin runtime: it only implements the mechanism protocol, introduces no business semantics, and does not provide a second-language implementation.

<!-- release-skill:managed:start id=latest-release -->
**0.14.0** (2026-08-28)

Harness 0.14.0 adds an official atomic-write fake, a synchronized package version export, and canonical temporary workspace roots for safe raw sinks.

**Added**

- Adds createAtomicWriteFake({ vector }) with deterministic write, replace, and observation facts and no filesystem side effects.
- Exposes FOUNDATION_PACKAGE_VERSION for lockstep consumers.
- Returns canonical realpaths from TemporaryWorkspace.create() and fromBaseline(). A root returned by create() can be passed directly to superviseProcess rawSink while it remains fresh and empty. A materialized non-empty baseline root is rejected by rawSink freshness validation.

**Changed**

- Documents that the fake verifies adapter wiring, not the domain guarantee or real-host qualification.

**Upgrade Notes**

Pin Contracts, Harness, and Engineering Kit to 0.14.0 together. Use the official fake with the Contracts vectors; retain real filesystem and domain tests for production guarantees. Temporary workspace roots are canonical. A root returned by create() can be passed directly to superviseProcess rawSink while it remains fresh and empty. A materialized non-empty baseline root is rejected by rawSink freshness validation.
<!-- release-skill:managed:end id=latest-release -->

## Problem It Solves

Contracts defines "what should be", and the Harness turns that into "can be safely reused" mechanisms at the Node runtime. If multiple skill-family projects each implement path containment, atomic writes, resource closure, report rendering, host integration, and the state base independently, you get inconsistent security boundaries and behavioral drift. The Harness consolidates these business-neutral mechanisms into one default implementation, which callers pick up as needed.

## Core Mental Model

The Harness consumes `skill-family-contracts` (a workspace dependency), reusing its dialect-routed Ajv validator, Kernel Protocol, frozen error codes, and fixtures; it does not copy protocol definitions or re-interpret the Schema. It only implements mechanisms: Schema validation, atomic writes, path containment, temporary workspaces, resource closure, bounded process supervision, the operation-request → operation-result pipeline, and business-neutral event logging with derived snapshots. Explicitly excluded: business semantics, task orchestration, Git writes, model calls, remote networking, and publish state. See `HARNESS_EXCLUSIONS`.

## Installation and Minimal Example

Version 0.14.0 is a local candidate. Build all three tarballs into one temporary directory and install those exact files for a candidate check:

```sh
pack_dir="$(mktemp -d)"
(cd packages/skill-family-contracts && pnpm pack --pack-destination "$pack_dir")
(cd packages/skill-family-harness-node && pnpm pack --pack-destination "$pack_dir")
(cd packages/skill-family-engineering-kit && pnpm pack --pack-destination "$pack_dir")
mkdir "$pack_dir/consumer" && (cd "$pack_dir/consumer" && npm init -y)
(cd "$pack_dir/consumer" && npm install "$pack_dir/skill-family-contracts-0.14.0.tgz" "$pack_dir/skill-family-harness-node-0.14.0.tgz" "$pack_dir/skill-family-engineering-kit-0.14.0.tgz")
```

After publication, use the registry coordinate:

```sh
npm install skill-family-harness-node@0.14.0
npm info skill-family-harness-node --help
```

The minimal example shows validating a contract document inside Node:

```js
// Run from an installed consumer directory after publication.
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
} from "skill-family-harness-node/quickstart-profile";
```

The v2 mechanism recomputes the bytes of every path-backed output and evidence Resource. It also rejects duplicate Resource ids, correlation drift, a changed Task digest, and incomplete or mismatched evidence bindings. It does not perform a domain audit, choose a method, retry work, or own lifecycle state.

The capability remains **candidate**. Pin all three Foundation packages exactly while evaluating it. Version 0.10.0 adds the canonical path above; the historical `/candidate/quickstart-profile` path remains a same-source migration alias. Migrate once to the canonical path. A later stable promotion will not require another import or same-byte Bundle rebuild. Integrations that still produce candidate v1 exchanges must stay pinned to exactly `0.2.1`.

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
| `FOUNDATION_PACKAGE_VERSION` | Exact Foundation package version used for lockstep checks. |
| `HarnessError` / `HARNESS_ERROR_KINDS` / `mechanismError` | Mechanism failures uniformly carry the registered error code `SFC2004`; `details.kind` gives a stable subcategory. |
| `validateContractDocument` / `getValidator` / `resolveSchemaContext` / `validatorCacheSize` | Routes and caches validators by Schema dialect; reuses Contracts' Ajv instances and dialect/policy semantics. |
| `classifyPathInput` / `resolveContained` / `readFileContained` | Path containment: intercepts path overruns, symlink escapes, and realpath escapes. |
| `writeFileAtomic` | Atomic write: leaves no half-written artifact on failure (temp file + fsync + rename). |
| `createAtomicWriteFake({ vector })` | Official no-filesystem fake for consumer contract tests; emits deterministic write/replace/observation facts. |
| `TemporaryWorkspace` / `createTemporaryWorkspace` / `withTemporaryWorkspace` | Auto-cleanup temporary workspace, cleaned up even on exception paths. |
| `digestBytes` / `computeResourceClosure` / `closureContains` | Resource closure and deterministic sha256 digest. |
| `superviseProcess` / `validateTimeoutPolicy` | The single bounded subprocess supervisor. Its 0.11.0 `rawSink` option writes raw stdout/stderr bytes only to a fresh canonical private root, then waits for child/stream close, queued writes, fsync, and handle close. The caller must exclusively control the sink namespace for the entire call; handle protection does not prove stable pathname/root identity. |
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

`node --test` covers: full Contracts fixture replay, security negative cases, atomic-failure paths, temporary workspaces, closure determinism, raw sink delayed-stream and failure paths, report fact binding and Markdown injection, host manifest/path/command trust, and state-store crashes, concurrency, corruption, fencing, explicit recovery, symlinks, hard links, and FIFO negative cases.

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
- You need host identity policy, host drivers, remote publication, uninstall deletion, a full Qoder driver, or binary adapter source (explicitly unsupported).
- You need domain audit semantics, retry orchestration, or a compatibility-frozen Quickstart API.

### Capability selection

- `foundation.harness.contract-validation`: contract validation and validator caching inside Node.
- `foundation.harness.path-containment`: path classification and contained resolution, rejecting the three escape types.
- `foundation.harness.atomic-write`: atomic write within contained paths, rolling back on failure.
- `foundation.harness.temporary-workspace`: auto-cleanup temporary workspace.
- `foundation.harness.resource-closure`: deterministic resource closure and sha256 digest.
- `foundation.harness.supervise-process`: one bounded subprocess supervisor; raw evidence capture remains mechanism-only and does not produce a receipt or domain verdict.
- `foundation.harness.request-processing`: operation-request → terminal operation-result.
- `foundation.harness.report`: report-model validation/render/binding/check.
- `foundation.harness.host-adapter`: adapter source closure/build/materialize, version probe, and read-only peer adapter verification.
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
- `verifyPeerAdapterDirectories` enumerates and reads two or more real peer roots, reuses bound-read/path containment/closure/manifest primitives, and fails closed on symlinks, escapes, byte drift, member drift, or incomplete mappings.

### Route elsewhere when

- Business state machine / terminal states: route to loop-agent.
- Host identity policy, host drivers, and lifecycle authorization belong to Engineering Kit; Harness supplies only the reusable bound-read, strict-publication, atomic-write, closure, and probe mechanisms.
- Domain audit semantics: route to a standalone audit consumer.

### Machine-readable sources

- Public capability catalog: [`capability-catalog.json`](https://ifoohoo.github.io/skill-family-engineering-kit/agents/capability-catalog.json) (`foundation.harness.*` entries).
- Package-local source: `src/*.mjs`.
- Package-local candidate source: `candidate/quickstart-profile.mjs`; canonical public import: `skill-family-harness-node/quickstart-profile`; historical migration alias: `skill-family-harness-node/candidate/quickstart-profile`.
<!-- agent-quick-reference:end -->

## Complete Plugin Candidate

The candidate observeFilesystemTree({ root, rootBinding }) reads complete tree facts. Existing superviseProcess accepts optional per-stream raw-byte caps. Observing a payload does not accept it.

Version 0.14.0 is a local source candidate and is not published. Consume the three locally verified tarballs; a version marker, unit test or successful install is not complete contract integration, migration completion, or real-host qualification.
