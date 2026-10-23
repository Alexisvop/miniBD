# MiniDB documentation and repository consistency review

**Review date:** 2026-10-23  
**Baseline:** default branch `main`, merge commit
`1216f86f1633075b37daa8f891fa9340a555793e`.  
**Review scope:** 79 tracked files at baseline, including 16 Markdown files,
production C headers/sources, test suites, build and CI configuration,
benchmarks/CSV/profile data, website sources, lockfile and assets.

## Current version map

| Component | Current state | Primary source |
| --- | --- | --- |
| C executable | MiniDB v0.5 | `src/repl.c` |
| On-disk layout | v3; v1/v2 deliberately unsupported | `include/storage.h`, [storage format](storage-format.md) |
| Pager | 4096-byte pages; limit 4096 pages | `include/pager.h` |
| Record | fixed 70-byte serialization; UTF-8 name up to 64 bytes; age 0..150 | `include/record.h`, `src/record.c` |
| Index | persistent B+ Tree, up to 340 entries per leaf, 509 separators per internal page | `include/btree.h`, [index design](btree-design.md) |
| Engine automated tests | 19 Linux CTest entries in Debug, Release and sanitizer configurations | `CMakeLists.txt`, [testing](testing.md) |
| Browser site | static React + TypeScript + Vite; educational simulations | `site/src/`, [website guide](../site/README.md) |
| Public showcase | [minibd.pages.dev](https://minibd.pages.dev/), publication and desktop screenshots confirmed by owner | [website guide](../site/README.md) |
| Website CI | 3 Node tree tests, npm audit and production build | `.github/workflows/site-ci.yml` |
| License | MIT | [LICENSE](../LICENSE) |

## Cross-checks completed

- Enumerated the entire tracked file tree (not merely top-level documents) and
  reviewed content, version markers and configuration across the source, tests,
  website, benchmark assets and documentation.
- Compared disk-format, record, page, index and command descriptions against C
  headers and relevant implementation functions.
- Checked the tracked Markdown relative links against actual repository paths;
  **66 relative links were resolvable** at the baseline scan.
- Confirmed the existence of both GitHub Actions workflows and their
  successful runs for main merge commit `1216f86` on 2026-10-23.
- Compared the website's published benchmark values against the Phase 5 summary
  CSV. Those values measure a cached lookup workload, not SSD latency.
- Retained original measured results, raw datasets and dated execution evidence
  without changing their historical meaning.

## Inconsistencies corrected

1. The root README was missing a prominent link to the live recruiter showcase.
   Added the confirmed public URL and website CI link.
2. The website guide still described **creating** a Pages project, although the
   showcase was already deployed. It now records the public production link,
   build settings and outstanding live-site review steps.
3. The security policy referred to a **future** static showcase. Updated this to
   the published site and reinforced the synthetic-data boundary.
4. The historical benchmark README incorrectly suggested the Phase 5 harness
   does not measure tombstones or age filters. `benchmarks/engine.c` actually
   includes logical deletion, age-filtered scans and reactivation. The Phase 3
   lookup-only result remains labeled historical.
5. Added website-versus-engine boundaries to architecture and visual guides.
6. Testing and benchmarking documents now record current CI and website
   provenance, while preserving original Phase 5 measurements.
7. The security review now separates its initial snapshot from post-publication
   follow-ups. The engineering log has a dated post-Phase-5 addendum.

## Intentionally historical material

Do **not** interpret these documents as current format specifications:

- [v1 storage format](storage-format-v1.md): Phase 2 archive.
- [v2 storage format](storage-format-v2.md): Phase 3 archive.
- [Phase 5 engineering audit](audit.md) and dated entries in
  [engineering log](engineering-log.md): evidence of what was true *at those
  earlier milestones*.
- 2026-10-14 and 2026-10-15 benchmark CSV files and profiling artifacts:
  preserved experiment records, **not** new 2026-10-23 measurements.

The current machine-readable constants and production behavior remain unchanged
by this documentation-only update.

## Remaining external validation

- Examine the **live** Cloudflare response security headers and mobile/keyboard
  accessibility separately; a passing build and static source review do not
  demonstrate those properties on the deployed domain.
- Confirm whether GitHub Private vulnerability reporting is enabled and review
  historical commit author email privacy settings.
- Use real GitHub Actions results for this documentation PR before merging.
- No full penetration test, secret history purge, independent browser E2E test,
  untrusted-file fuzzing certification or production-grade DB security claim is
  made by this audit.

**Conclusion:** the reviewed documents now describe the current MiniDB v0.5
engine, v3 disk layout and published static showcase without overwriting
historical evidence or claiming nonexistent durability/security features.
