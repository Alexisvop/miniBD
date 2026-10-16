# Testing and validation

## Commands

Requirements: C17/POSIX toolchain, CMake 3.16+, Make or Ninja, Python 3.8+ for the
independent process oracle. The executable/library alone need no Python. Tests
use explicit CHECK conditions rather than assert, so Release does not disable them.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
ctest --test-dir build -LE stress --output-on-failure
ctest --test-dir build -L stress --output-on-failure

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-release -j2
ctest --test-dir build-release --output-on-failure

cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DMINIDB_SANITIZERS=ON
cmake --build build-sanitize -j2
env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize --output-on-failure
```

Linux has **19 CTest entries**. The original 15 are preserved; four new entries
are input_stress, tree_mutations, mixed_stress and process_model. Other POSIX
platforms omit the Linux-only linker-wrapped failures test (18 entries); those
platforms were not validated in this phase. CTest entries are executables or
scripts containing many checks, not a count of individual assertions.

## Coverage map

| Entry | Coverage |
| --- | --- |
| parser | Exact grammar, numeric limits, signs/overflow, whitespace, invalid UTF-8, 64-byte boundary, unchanged output on failure |
| repl | Stream behavior, input limit/NUL recovery, invalid commands, EOF, interactive output |
| record | Exact little-endian representation, UTF-8, length/padding/age corruption, round trips |
| pager | Cache/dirty state, before-images, capacity, truncation and page bounds |
| storage | Multi-page records, duplicates, missing IDs, ordered content, reopen, capacity and same-ID reactivation at full capacity |
| corruption | Metadata/header/record corruption and file truncation; no silent rewriting |
| btree | 10000 rows each ascending, descending, seeded random; all hits/misses, complete ordered scans, splits, invariants and reopen |
| btree_corruption | Three-level tree: cycles/aliasing, depth, separators, occupancy, leaf links, wrong type/range/location, orphan pages and v1 rejection |
| queries | Structured Executor, update/delete/reactivate, six filters, empty output, boundary IDs/ages, invalid direct commands and callback errors |
| mixed | Original 30000 operations over 3000 keys, independent C state model, six reopen cycles and full query/filter checks |
| tombstones | Bitmap boundaries, active counts, retained payload validation, unchanged index bytes, invalid bits and genuine v2 rejection |
| input_stress | 50000 deterministic mutations of valid parser inputs and 50000 serialized-record mutations; output preservation/canonical round trips |
| tree_mutations | 100000-row, height-3 tree; 33334 deletes, 13333 updates and 11112 reactivations; complete formula-model comparison and reopen |
| btree_large | 100000 rows each ascending, descending and random; all lookups, missing keys, full ordered content and invariants across reopen |
| mixed_stress | 60000 mixed operations over 10000 keys, 12 reopen cycles; all six filters and complete state checked every 1000 operations |
| failures | Allocation failures at insert/split/root propagation/update/delete/reactivate; EACCES, read error, short I/O, EINTR, partial/zero writes, ENOSPC, fsync/close errors and poisoned state |
| process_model | Python dictionary oracle, 10000 UTF-8 rows, 20000 mixed operations, five complete snapshots in fresh processes; every ID and six filters; real mode-permission denial |
| cli_queries | Independent CLI sessions, query/mutation output, controlled errors, persisted tombstones and EOF reactivation |
| cli | CLI arguments/help, separate-process persistence, recoverable inputs, invalid files and exit status |

The stress label selects btree_large, btree_corruption, mixed_stress and
tree_mutations. process_model has the integration label. `-LE stress` is a shorter
iteration path, not the full acceptance run. Benchmarks are excluded from CTest.

## Independent models and repeatability

- Tree insert patterns: uint32 LCG seed `0x12345678`, Fisher-Yates random ordering;
  expected IDs/names/ages generated from the ordinal, not read back from storage.
- Mixed C model: uint32 LCG seed `0x5341a78f`, independent exists/active/Record
  array. INSERT, SELECT, UPDATE, DELETE and same-ID reactivation are checked;
  full model/filter verification adds scans with all six predicates.
- Height-3 changes: formula-based model (multiples of 3 deleted, surviving
  multiples of 5 updated, multiples of 9 reactivated). Expected contents are
  generated independently of page layout and mutation code.
- Process model: Python random seed `0x51A7E`, dictionary plus standard operator
  predicates. Initial shuffled insert process, independent snapshot process,
  then four mutation/snapshot pairs: ten engine processes plus a permission probe.
  Each snapshot compares stdout and stderr exactly, every point lookup, full
  ordered scan and six filters. Normal close alternates `.exit` and EOF.
- Input mutations: seed `0x915e4bad`; deterministic bounded corpus exploration,
  not an exhaustive or coverage-guided fuzzing campaign.

C tests create unique temporary files via mkstemp under /tmp; the process oracle
uses TemporaryDirectory. CLI CMake tests use dedicated paths in the build tree.
Fault tests never open real user data. A root process skips the real mode-permission
scenario because root may bypass permissions; the EACCES wrapper remains tested.
The recorded run used an ordinary user and exercised the real denial case.

## Executed evidence, 2026-10-15

Arch Linux x86_64, GCC 16.2.1, CMake 4.4.4, Python 3.14.8. Strict-warning builds
completed without warnings. Timings below are whole-suite elapsed times, not
performance claims; sanitizer timing overlapped some nonbenchmark build work.

| Run | Result | Elapsed |
| --- | --- | --- |
| Original baseline Debug, before edits | 15/15 passed | 8.51 s |
| Expanded Debug | 19/19 passed | 10.70 s |
| Expanded Release | 19/19 passed | 5.53 s |
| ASan + UBSan + LeakSanitizer | 19/19 passed, no findings | 34.22 s |

Sanitizer execution ran outside the tool sandbox to avoid its ptrace restriction
on LeakSanitizer, with leak detection enabled and halt-on-error settings. Valgrind
was not installed, so no Valgrind result is claimed. gprof was available; see the
[profiling report](benchmarking.md). No time/memory limit was hit in these runs.
The page-limit test intentionally reaches the engine's configured capacity and
checks rollback, rather than treating it as an unexpected failure.

A source-only temporary copy containing CMakeLists.txt, src, include, tests and
benchmarks was also configured/built in fresh Debug and Release directories,
with all 19 Release entries executed. It contained no .git, prior build cache,
user database, local specification folder or prebuilt binary; it was removed
after validation. This checks clean-source reproducibility without committing
or publishing the working tree.

## Maintenance

CMake's clean target removes generated build products without touching sources:

```sh
cmake --build build --target clean
```

Keep builds separate when changing generators/instrumentation. After engine or
format changes, run all entries, not only the fast subset. After performance
changes, repeat comparable benchmarks and preserve raw samples/environment.
No benchmark threshold is used as a unit-test assertion. See [audit](audit.md)
for findings and [limitations](limitations.md) for what these tests do not prove.
