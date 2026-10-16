# Phase 5 engineering audit

Date: 2026-10-15. Baseline: commit `1766422` (`Act, Del and queries`), clean
working tree before phase 5. The specification files under `recursos/` are local,
ignored inputs; public documentation is self-contained. The user's ignored
`indexed.db` was not opened or modified. No commit, push or remote configuration
change was performed.

## Scope and method

Read the main specification and phase 5 requirements, all production C sources,
public headers, CMake, current format/architecture/tree documentation, existing
fault tests, stress/reference tests and the phase 3 benchmark. Ran all original
15 CTest entries before editing: 15/15 passed (Debug, 8.51 seconds).

| Area | Review and evidence |
| --- | --- |
| Parser/REPL | Bounded line consumption, unsigned-char ctype calls, numeric overflow checks, output unchanged on parse failure, strict UTF-8, EOF/NUL/overlong input coverage |
| Record codec | Explicit little-endian fields; length checked before payload reads; no unaligned casts/native struct writes; zero padding verified; unchanged outputs on failure |
| Executor | Borrowed callbacks, no allocated result ownership, structured errors/counts, no terminal I/O, filter validation and early callback failure |
| Storage | Stable page/slot locations, tombstone visibility, counters derived during validation, unique index/data correspondence, fallible work before counter publication |
| B+ tree | Bounded temporary arrays, upper/lower bounds, split propagation, root restoration on failure, fixed page capacities, cycle/depth/range/leaf-link validation |
| Pager | Checked allocations, bounded page offsets (16 MiB), short transfers/EINTR loops, cleanups after partial reads, before-images/dirty flags, poison after write/fsync failure |
| Main/status | Open/close errors propagated, normal close after caught REPL errors, final output flush checked, status diagnostics |
| Build/tests | Strict warnings, assertions independent of NDEBUG, Linux-only linker fault wrappers, benchmarks outside CTest |

The interfaces require valid live handles and non-NULL required outputs. Only
Executor explicitly accepts NULL storage/command/sink according to its contract;
other APIs are internal C interfaces, not arbitrary-pointer validation services.
Callbacks must not mutate or close their borrowed storage. External concurrent
file modifications remain unsupported.

## Findings and disposition

No critical/high-severity engine correctness defect was found in the reviewed
paths and executed scenarios. This is a scoped review, not proof of defect absence.
No production storage, index or file-format algorithm was changed.

| Severity | Finding | Disposition |
| --- | --- | --- |
| Medium, verification gap | Mixed model stopped at 30000 operations; no large complete-state oracle across fresh processes | Preserve original test and add 60000-operation mode, height-3 mutation test, and Python process oracle with full payload/filter comparison |
| Medium, measurement gap | Historical benchmark covered only lookup and published no trial dispersion; no phase 4 mutation timings | New standalone v3 workload, raw five-trial samples, median/min/max/MAD, separate synchronization/open timings, exact lookup validation and model checks |
| Low, robustness coverage | Handwritten malformed input cases lacked repeatable mutation exploration | Add 50000 seeded parser mutations and 50000 codec mutations; successful canonical round trips and failure-output preservation |
| Low, presentation gap | README was a phase 4 handoff; no consolidated test/benchmark/limitations guide or license status | Rewrite README for external review, add technical guides, retain historical measurements and explicitly identify license status |
| Accepted design limitation | Cache acknowledgment is not a durable transaction; no locking/checksums/reclamation | Keep scope unchanged and document failure windows in limitations/format guides |

## Integrity reasoning

Strict global uniqueness of index keys, equality between each indexed key and its
referenced record ID, and equality of index-entry and allocated-slot counts imply
a one-to-one mapping. Two different keys cannot reference the same valid slot;
two equal keys are rejected. Together with complete page reachability and dense
slot validation, this rules out missing/orphan/aliased record mappings for files
that satisfy these invariants. It does not detect a coordinated, structurally valid
change to both an index key and record, or a valid-looking payload bit flip.

Logical deletion changes visibility, not key membership. Separator minima and
occupancy therefore remain valid across delete/reactivate. Tests cover retained
keys at bitmap boundaries and changes throughout a three-level tree.

## Remaining scope

See [test evidence](testing.md), [measured performance](benchmarking.md), and
[explicit limitations](limitations.md). Valgrind and perf were unavailable in this
environment; gprof was available for complementary CPU profiling. No external
compatibility certification, exhaustive fuzzing campaign, crash-recovery guarantee,
license selection or production-readiness claim is made.
