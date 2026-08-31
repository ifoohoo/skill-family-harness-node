# Changelog

<!-- release-skill:changelog:start version=0.16.0 locale=en baseline=sha256:27fd0f5623d5c93b3076af93231101956233f179dd0cab3d1c74dae891668ea8 -->
## [0.16.0] - 2026-08-31

Harness 0.16.0 clarifies the existing rawSink boundary and keeps output redaction outside Foundation.

### Changed

- Documents rawSink as a protected 0600 raw stdout/stderr sink in a fresh canonical temporary root, not a redaction API.
- Explicitly rejects rawStreamSink, transform sinks, generic sanitizers, second runners, and new stdout callbacks for a single-consumer redaction request.

### Upgrade Notes

Consumers that require zero plaintext on the filesystem must own the threat model and implementation; this release adds no redaction or zero-plaintext guarantee.
<!-- release-skill:changelog:end version=0.16.0 locale=en -->


<!-- release-skill:changelog:start version=0.15.0 locale=en baseline=sha256:8c81395a147ed1e995e21ae0074f66d434b87070ea33350fa3e97cbcb5f2fa8e -->
## [0.15.0] - 2026-08-29

Harness 0.15.0 adds executable identity observation and a best-effort Node tree record mode.

### Added

- Records symlink target bytes without following links on a stable isolated tree.
- Rechecks the original script after observing its interpreter and orders paths by Unicode code point.

### Changed

- Keeps omitted and reject symlink policy on the 0.14 native path.

### Upgrade Notes

Record mode is candidate best effort; it does not provide a transaction snapshot or hostile concurrent-writer safety.
<!-- release-skill:changelog:end version=0.15.0 locale=en -->


<!-- release-skill:changelog:start version=0.14.0 locale=en baseline=sha256:23ac2de57433409f1dafac90d7bdd130fd10bd399fd188d7d848a49320b86f30 -->
## [0.14.0] - 2026-08-28

Harness 0.14.0 adds an official atomic-write fake, a synchronized package version export, and canonical temporary workspace roots for safe raw sinks.

### Added

- Adds createAtomicWriteFake({ vector }) with deterministic write, replace, and observation facts and no filesystem side effects.
- Exposes FOUNDATION_PACKAGE_VERSION for lockstep consumers.
- Returns canonical realpaths from TemporaryWorkspace.create() and fromBaseline(). A root returned by create() can be passed directly to superviseProcess rawSink while it remains fresh and empty. A materialized non-empty baseline root is rejected by rawSink freshness validation.

### Changed

- Documents that the fake verifies adapter wiring, not the domain guarantee or real-host qualification.

### Upgrade Notes

Pin Contracts, Harness, and Engineering Kit to 0.14.0 together. Use the official fake with the Contracts vectors; retain real filesystem and domain tests for production guarantees. Temporary workspace roots are canonical. A root returned by create() can be passed directly to superviseProcess rawSink while it remains fresh and empty. A materialized non-empty baseline root is rejected by rawSink freshness validation.
<!-- release-skill:changelog:end version=0.14.0 locale=en -->


<!-- release-skill:changelog:start version=0.13.0 locale=en baseline=sha256:ba0667eb4b4805aafc2423cd79c04df3c835b8ae9bb07391867175f6ad96c36b -->
## [0.13.0] - 2026-08-26

Harness 0.13.0 is a source candidate for complete bound-tree observations and optional subprocess output limits.

### Added

- Adds observeFilesystemTree({ root, rootBinding }) as a public candidate entry for fresh private tree facts, including file bytes and POSIX mode observations.

### Changed

- Extends superviseProcess with independent stdout/stderr byte limits using the existing termination path. Equal-to-limit output is allowed; uncapped behavior remains compatible.

### Upgrade Notes

Pin Contracts and Harness together. Tree observation does not apply payload acceptance policy or promise a transaction snapshot. Candidate preparation is not publication.
<!-- release-skill:changelog:end version=0.13.0 locale=en -->


<!-- release-skill:changelog:start version=0.12.0 locale=en baseline=sha256:0074df3a947706f6cd6d8593968a348cd4d3f38d8f2bf75f15a04c8183d76618 -->
## [0.12.0] - 2026-08-26

Harness 0.12.0 joins the lockstep Foundation release with its existing process and filesystem mechanisms unchanged.

### Changed

- Pins skill-family-contracts to 0.12.0 for the five-host verification extension implemented by Engineering Kit.

### Upgrade Notes

No new Harness API or runtime behavior is introduced. Existing process supervision, raw-byte sinks, bound reads, and digest mechanisms remain the shared implementation; host-specific protocol rules belong to Engineering Kit.
<!-- release-skill:changelog:end version=0.12.0 locale=en -->


<!-- release-skill:changelog:start version=0.11.0 locale=en baseline=sha256:e20f505e42ba45865bfee38a6c83671006bf20148d5d4e421ff7f443bcce260b -->
## [0.11.0] - 2026-08-25

Harness 0.11.0 adds raw-byte subprocess sinks and exposes the bound-read root identity needed by host verification.

### Added

- Extends superviseProcess with an exclusive, no-follow raw stdout/stderr sink that waits for stream close, queued writes, fsync, and close.
- Keeps the existing bound-read mechanism as the only root and member read authority.

### Changed

- Carries the previously prepared host Profile closure into the lockstep 0.11.0 family release.

### Upgrade Notes

The raw sink is mechanism-only; it does not create a second process runner, receipt state machine, or host-specific policy. The caller must exclusively control the sink namespace for the whole call; handle protection does not prove stable pathname or root identity.
<!-- release-skill:changelog:end version=0.11.0 locale=en -->


<!-- release-skill:changelog:start version=0.10.0 locale=en baseline=sha256:64a325d3e51604a8436b33df5a7f617f9aab1f1e02adb32ccb58316aa5c5eab1 -->
## [0.10.0] - 2026-08-24

Harness 0.10.0 adds canonical entrypoints, reuses existing host mechanisms, and adds read-only peer adapter verification from real directories.

### Added

- Adds skill-family-harness-node/quickstart-profile and skill-family-harness-node/rename-directory-no-replace canonical exports.
- Reuses filesystem-root binding, strict no-replace publication, atomic replacement, and existing build digests for the Kit's local host install/update path.
- Adds `verifyPeerAdapterDirectories`, which re-enumerates peer roots and verifies common closure, byte digests, standard manifests, and complete logical mappings without writing them.

### Changed

- Keeps each historical candidate export as a same-source migration alias and leaves the mechanism registry unchanged.
- Keeps validate-many-by-schema-id and its error semantics unchanged while managed Bundles accept historical and canonical Schema IDs through the same validator.

### Upgrade Notes

Update all three exact pins to 0.10.0 and migrate historical candidate imports and Schema IDs once to canonical identities. The low-level no-replace primitive remains distinct from the stable fixed-set-publication API; choose the contract that matches the use case.
<!-- release-skill:changelog:end version=0.10.0 locale=en -->


<!-- release-skill:changelog:start version=0.9.0 locale=en baseline=sha256:e5f4e3bb6ab343e4ccfb7dcdbba8b9ab4576b4cf285c30bb3597d577be5eb391 -->
## [0.9.0] - 2026-08-24

Harness 0.9.0 adds stable identity-bound reads and fixed-set no-replace publication with a fixed four-platform native closure.

### Added

- Adds createFilesystemRootBinding and readFileBound with handle-relative no-follow acquisition and optional byte digest guarding.
- Adds the stable fixed-set-publication subpath with native no-replace publication and terminal indeterminate receipts.
- Adds the candidate validate-many-by-schema-id mechanism to the existing Quickstart dispatcher.

### Changed

- Keeps protected deletion excluded and preserves the existing 21-item capability registry.

### Upgrade Notes

Pin Contracts, Harness, and Engineering Kit to exactly 0.9.0. Batch validation and the Quickstart Bundle remain candidate; filesystem binding and fixed-set publication are stable.
<!-- release-skill:changelog:end version=0.9.0 locale=en -->


<!-- release-skill:changelog:start version=0.8.4 locale=en baseline=sha256:dfb053d7719d3c7cb5b7d8dbe7a2349597696614651ef4ae04451c34cd7bbc02 -->
## [0.8.4] - 2026-08-24

Lockstep Foundation 0.8.4 version update with no new Harness mechanism or public API.

### Changed

- Moves the package version to 0.8.4 together with Contracts and Engineering Kit.
- Keeps all 21 Harness capabilities and public exports unchanged; source-authority receipt validation belongs to Contracts.

### Upgrade Notes

Consumers must pin all three Foundation packages to exactly 0.8.4. No Harness API migration is required.
<!-- release-skill:changelog:end version=0.8.4 locale=en -->


<!-- release-skill:changelog:start version=0.8.3 locale=en baseline=sha256:449943bedf3698888fec4dcb09a0460f337b9406d5258946a3d3ffcd23143b8d -->
## [0.8.3] - 2026-08-23

Path containment now tolerates one exact anchor-removal race while preserving fail-closed escape checks.

### Changed

- A second ENOENT and every non-ENOENT failure remain closed; symlink swaps, out-of-root targets, and all existing containment checks remain rejected.
- Keeps the hook private to the paths module test surface and adds no public export, general retry policy, lock layer, ledger, or runner.
- Moves the package version to 0.8.3 together with Contracts and Engineering Kit.

### Fixed

- When realpath of the selected existing anchor fails with ENOENT because another process removed it, resolveContained recomputes the deepest existing ancestor exactly once.

### Upgrade Notes

Consumers must pin all three Foundation packages to exactly 0.8.3. Concurrent lock acquisition can now survive the current owner removing the selected lock-file anchor; no Harness API migration is required.
<!-- release-skill:changelog:end version=0.8.3 locale=en -->


<!-- release-skill:changelog:start version=0.8.2 locale=en baseline=sha256:af051a6925c7d1f46bef6fbe367aceea8685b4a916629af4e69334e73014e3a7 -->
## [0.8.2] - 2026-08-23

The fixed candidate mechanism bridge now exposes the existing strict reader through read-file-strict.

### Added

- Adds read-file-strict to invokeFoundationMechanism with closed root, path, encoding, and expectedSha256 parameters.
- Returns UTF-8 content as a string and binary content in the standard JSON-safe Buffer shape.

### Changed

- Delegates containment, file reading, digest verification, and failure classification directly to readFileStrict; no second read algorithm is introduced.
- Moves the package version to 0.8.2 together with Contracts and Engineering Kit.

### Upgrade Notes

Candidate consumers must pin all three Foundation packages to exactly 0.8.2 and rebuild their managed Bundle. Direct calls retain SFC2004 details.kind failures; the JSON CLI continues to promise only exit code 0 for success and 2 for rejection.
<!-- release-skill:changelog:end version=0.8.2 locale=en -->


<!-- release-skill:changelog:start version=0.8.1 locale=en baseline=sha256:4cd9f23da3561184cef4ebadb7e758a22d377d069742bbe7c77166d6c1b3bd14 -->
## [0.8.1] - 2026-08-22

Harness now preserves its own report renderer version when a host bundles it into a single-file adapter.

### Changed

- Moves the package version to 0.8.1 together with Contracts and Engineering Kit.
- Keeps all 21 Harness capabilities, report contracts, and public exports unchanged.

### Fixed

- Replaces the report renderer's runtime import.meta.url package-manifest lookup with a static JSON import, so esbuild inlines the Harness version instead of resolving the host adapter's package metadata.
- Adds source and third-party single-file bundle regression tests for REPORT_RENDERER_VERSION.

### Upgrade Notes

Host adapters that bundle the report renderer must pin Harness 0.8.1. The report API and renderer identity remain unchanged, so no API migration is required from 0.8.0.
<!-- release-skill:changelog:end version=0.8.1 locale=en -->


<!-- release-skill:changelog:start version=0.8.0 locale=en baseline=sha256:85322b5fb8d4e3a2177fbe9e37427235256d530bf604d0b5cdb95cf2ad11cc50 -->
## [0.8.0] - 2026-08-21

Lockstep version bump with Foundation 0.8.0; the Harness mechanism surface is unchanged.

### Changed

- Contracts 1.7.0 adds the Project Profile contract, but no Harness capability or exported mechanism changes.

### Upgrade Notes

Consumers keep their Harness mechanism pins. Project Profile verification belongs to Engineering Kit/Profile SPI, not to a new Harness mechanism.
<!-- release-skill:changelog:end version=0.8.0 locale=en -->


<!-- release-skill:changelog:start version=0.7.0 locale=en baseline=sha256:062a81c93d213982e9d6beda1e574959ba98b18170f0c703efb0dd71573a3e77 -->
## [0.7.0] - 2026-08-21

Lockstep version bump with the Foundation 0.7.0 line; the thin mechanism runtime is unchanged.

### Changed

- No capability change - HARNESS_CAPABILITIES stays at 21 items and every exported mechanism (atomic contained writes, path containment, strict authority read, resource closure, digests, bounded subprocess supervision, URL credential redaction) keeps its 0.6.0 contract; the package version moves in lockstep with the Foundation line because the three leaf packages share one public version coordinate.

### Upgrade Notes

Version 0.7.0 carries no harness surface change. Consumers keep their existing pins; the harness computeResourceClosure resource closure remains distinct from the Kit plan closure introduced by engineering-kit 0.7.0 and the two are not interchangeable.
<!-- release-skill:changelog:end version=0.7.0 locale=en -->


<!-- release-skill:changelog:start version=0.6.0 locale=en baseline=sha256:c67e713aebe53382c651655a2be9e78ace8da6a1b7852c1e6acadcb383ff2422 -->
## [0.6.0] - 2026-08-21

This release adds bounded subprocess supervision (FND-ADR-012), completes the Foundation strict authority read path with contained nested directory preparation (FG-1), adds pre-persistence URL credential redaction (FG-2), and grows HARNESS_CAPABILITIES from 18 to 21.

### Added

- Adds superviseProcess with validateTimeoutPolicy, WATCHDOG_REASONS, TERMINATION_REASONS, PROCESS_STATUSES and ENVELOPE_GUARANTEES (FND-ADR-012) - one bounded spawn, liveness by explicit events, consumer-supplied timeout policy, SIGTERM then grace then SIGKILL against the process group, and a single closed-enum termination envelope; the mechanism never restarts the supervised process and never holds timeout values.
- Adds readFileStrict (FG-1), the read-side twin of the strict write path - containment first, symlink refusal with O_NOFOLLOW, regular-file identity re-assertion on the opened handle (dev/ino), and a sha256 digest receipt of the exact bytes read; an optional expectedSha256 content guard fails closed before any delivery.
- Adds the createParents option to publishFileExclusive (FG-1) - the missing portion of the parent chain is prepared as real directories inside the containment layer and every intermediate entry is re-verified as a real directory; symlink components are still refused and no entry is ever replaced.
- Adds redactUrlCredentials with REDACTED_URL_PLACEHOLDER (FG-2) - strips the userinfo component from any URL before the value reaches disk or logs; unparseable input degrades to an opaque placeholder and never leaks to the output.
- Re-exports the contracts-owned token estimate consumption contract (consumeTokenEstimate, consumeTokenEstimateStrict and companions) next to estimateTokens, and carries the authoritative token estimator estimateTokens with the skill-family-token-estimate CLI (audit remediation C1).

### Changed

- Grows HARNESS_CAPABILITIES from 18 to 21 (adds supervise-process, strict-read and url-credential-redaction); the strict write path keeps its no-replace, byte-verified receipt semantics unchanged.
- Keeps business semantics, retry/restart policy, budget thresholds, and the decision of which values are URLs under consumer ownership; the harness owns mechanism only.

### Upgrade Notes

Version 0.6.0 is the Foundation capability completion line. The createParents option of publishFileExclusive is a Foundation-side profile behavior change under the 2026-08-19 discipline; consumers needing contained nested publication must pin exactly 0.6.0.
<!-- release-skill:changelog:end version=0.6.0 locale=en -->


<!-- release-skill:changelog:start version=0.5.0 locale=en baseline=sha256:03eca87a8c9bea814e741518168696f17c869bdb71baf2046cef4c3596d9ebf3 -->
## [0.5.0] - 2026-08-16

This release adds the declared read surface assertion (FND-ADR-010) and the structured surface scanner (FND-ADR-011) to the stable Harness surface, growing HARNESS_CAPABILITIES from 16 to 18 entries, and adds three dependency-reviewed runtime libraries.

### Added

- Adds assertDeclaredReadSurface (FND-ADR-010): a no-execution, syntax-surface-only assertion that every node:fs named import inside a declared module set stays inside the consumer-declared read surface, with a closed violation vocabulary and a frozen declared-read-surface-result envelope.
- Adds scanSurfaceStructured (FND-ADR-011): the structured sibling of scanSurface. IP-shaped candidates enter a single standard parse entry (ipaddr.js) with consumer-declared CIDR approval and fail closed when unparseable; scoped and unscoped coordinates, registries and hosts need consumer-declared approval; format adapters (pnpm-lockfile via @pnpm/lockfile.fs with yaml AST comment regions, and tree-json) parse structurally with no position-level exemptions; binary and symlink policies fail closed. The closed nine-rule vocabulary travels in details.rule of the structured-scan-violation mechanism error.
- Adds three dependency-reviewed runtime libraries for the structured scanner, reviewed per FND-ADR-006 section-4 analogy (dependency-closure pre-review, executor self-review pending independent review): ipaddr.js 2.5.0 (MIT, zero dependencies), yaml 2.9.0 (ISC), and @pnpm/lockfile.fs 1001.1.35 (MIT, 18 transitive dependencies all inside the approved public coordinates).
- Grows HARNESS_CAPABILITIES from 16 to 18 entries for the two new mechanisms.

### Changed

- Documents the relationship of the two policy documents to workspace-private leak policies: a workspace-private leak-policy.json instance document is not a subset, not isomorphic and not a migration target of the surface-scan-policy or structured-scan-policy schemas — the documents share rule vocabulary and fail-closed semantics by design, but their byte-level shapes are independent and must not be compared for compatibility. scanSurface is the execution-core generalization projection: the public, consumer-parameterized form of the same mechanism family, without any private identity, path, or approval-list interpretation of its own.
- Records the cost of the dependency-review decision honestly: the harness tarball grows with the @pnpm/lockfile.fs closure, the thin-runtime property changes from zero third-party runtime dependencies to three reviewed ones, and the pnpm-lockfile adapter writes one temporary lockfile copy under the OS temp directory (removed afterwards). The engineering-kit offline-consumer verification gates now derive the complete third-party production closure of the three Foundation packages mechanically (identity-deduplicated, npm: alias-aware, range-scoped override selectors) instead of a single-package closure, so the review decision is continuously verified against the real installed bytes.
- Keeps mechanism purity: no execution of scanned files, no model calls, no network; symlinked entries are never followed.

### Upgrade Notes

Version 0.5.0 is the FND-ADR-010/011 harness line. Mechanism imports use the stable capability names published in HARNESS_CAPABILITIES; structured-scan policies must pass the structured-scan-policy contract validation of contract-spec 1.5.0.
<!-- release-skill:changelog:end version=0.5.0 locale=en -->


<!-- release-skill:changelog:start version=0.4.0 locale=en baseline=sha256:72d3fe048ced8eb44a84fabe6f1fdd2eb0c938b4f6bb98740474f72aec04d873 -->
## [0.4.0] - 2026-08-16

This release adds five finite-closed-semantics mechanisms and a token-lock primitive to the stable Harness surface (FND-ADR-009), grows HARNESS_CAPABILITIES from 9 to 16 entries, and ships the rename-directory-no-replace native-prebuild candidate.

### Added

- Adds five finite-closed-semantics mechanisms (FND-ADR-009), namely frozen-baseline materialization with contentGuard, the generic read chokepoint, strategy-driven surface scanning, deterministic token upper-bound estimation, and the generic upper-bound guard.
- Adds the token-lock primitive for exclusive token-file publication and digest verification.
- Grows HARNESS_CAPABILITIES from 9 to 16 entries for the new mechanisms.
- Adds the rename-directory-no-replace candidate, a native-prebuild addon proving no-replace directory renames on darwin-arm64, darwin-x64, linux-arm64-gnu, and linux-x64-gnu, with a release receipt and SBOM.

### Changed

- Keeps the Quickstart Profile v2 Task/Result exchange verification delivered in 0.3.0, including real-byte recomputation for path-backed outputs and evidence, duplicate Resource id rejection, and exact evidence binding verification.
- Leaves method selection, retry policy, and domain result interpretation to the consumer.

### Upgrade Notes

Version 0.4.0 is released on npm and the public mirror. Pin the candidate subpath to exactly 0.4.0; mechanism imports use the stable capability names published in HARNESS_CAPABILITIES.
<!-- release-skill:changelog:end version=0.4.0 locale=en -->


<!-- release-skill:changelog:start version=0.3.0 locale=en baseline=sha256:7afef97792b4714abbd0412dcb7ea76ca66260ef9f8dd2191131b7c3f8539813 -->
## [0.3.0] - 2026-08-12

This source candidate updates the Quickstart Profile harness to verify v2 Task and Result exchanges without taking ownership of consumer semantics.

### Added

- Recomputes real bytes for path-backed outputs and evidence, and rejects duplicate Resource ids across observations, outputs, and evidence.
- Verifies operation identity, Task digest, every run/stage/attempt field, and the exact evidence binding set.

### Changed

- Replaces the incompatible 0.2.1 candidate surface; consumers that still require v1 must remain pinned to exactly 0.2.1.
- Leaves method selection, retry policy, and domain result interpretation to the consumer.

### Upgrade Notes

Version 0.3.0 is a local, unpublished source candidate. Pin the candidate subpath to an exact package version and update v1 exchange producers before adopting it.
<!-- release-skill:changelog:end version=0.3.0 locale=en -->


<!-- release-skill:changelog:start version=0.2.1 locale=en baseline=sha256:acd7d4e02eb309b149a31f4b88a8163c69ae094a53591f173c20c407e8ff4ed0 -->
## [0.2.1] - 2026-08-10

This release adds candidate Quickstart Profile exchange helpers and makes the package release documentation available in English and Simplified Chinese.

### Added

- Adds candidate helpers that create and revalidate observation Resources, build Tasks, wrap Results, and fail closed when a Result does not bind the exact Task and correlation fields.
- Adds complete English and Simplified Chinese package documentation, including an agent quick-reference section.

### Changed

- Manages the current README and CHANGELOG release sections from one bilingual, versioned notes source.
- Distributes the project NOTICE separately from the Apache-2.0 LICENSE.

### Upgrade Notes

The candidate helpers do not alter the stable Harness API or add lifecycle, retry, orchestration, model-call, network, or Git-write semantics.
<!-- release-skill:changelog:end version=0.2.1 locale=en -->
