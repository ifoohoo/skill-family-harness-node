# Changelog

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
