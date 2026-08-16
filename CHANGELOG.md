# Changelog

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
