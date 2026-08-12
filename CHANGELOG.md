# Changelog

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
