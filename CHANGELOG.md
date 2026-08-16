# Changelog

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
