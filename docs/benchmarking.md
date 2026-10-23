# Reproducible performance measurements

## Build and reproduce

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DMINIDB_BENCHMARKS=ON
cmake --build build-release -j2
./build-release/benchmark_engine > /tmp/minidb-benchmark-new.csv
python3 benchmarks/summarize.py /tmp/minidb-benchmark-new.csv > /tmp/minidb-summary-new.csv
```

Do not overwrite historical result files to record a new run. Benchmarks are
standalone executables, never CTest tests. `benchmark_lookup` retains the phase 3
experiment; `benchmark_engine` is the phase 5 experiment. Its temporary databases
use `mkstemp` under `/tmp`, exclusively, and are removed after successful trials.
An interrupted/failed run can leave its own `minidb-engine-benchmark-*` file.
No real user database is used.

## Method

Four sizes: 1000, 10000, 50000 and 100000 allocated records. At each size a complete
unmeasured trial warms the workload, followed by **five measured trials**, each
with a fresh database and the same deterministic data. Timings use
`clock_gettime(CLOCK_MONOTONIC)`. There are 11 experiments per size, 220 emitted
samples total. Each timing is a **whole batch in milliseconds**.

Keys are `2*i`, age `i%151`, name `Benchmark`. Insertion/update order uses a
Fisher-Yates shuffle with uint32 LCG `state = state*1664525 + 1013904223`, seed
`0x12345678`. Queries use the same LCG, seed `0xabcdef01`: 2000 queries, 75% hits
and 25% nonexistent odd IDs. Both lookup methods use exactly these queries and
the same file, with independent Pager caches. The benchmark-only sequential
implementation scans physical slots, skips index pages, checks the v3 bitmap
and decodes a matching active payload. Production SELECT always uses the B+ tree.

| Experiment | Operations per sample | Included work |
| --- | --- | --- |
| insert | N | Shuffled insertion, serialization, index splits, cache allocation and memory savepoints; excludes sync |
| sync_insert | 1 | Dirty page writes and file fsync after initial inserts |
| open_validate | 1 | Reopen, full structural/data validation and cache allocation/loading; OS cache uncontrolled |
| indexed_lookup | 2000 | Cached ID traversal, deserialize hits, status checks and checksum |
| sequential_lookup | 2000 | Same queries/results, cached physical slot scan, decode hits and checksum |
| update | N | Same shuffled IDs; change name to Updated and age to `(i+31)%151`; excludes sync |
| logical_delete | N/4 | Delete IDs with `i%4==0` in ascending order; excludes sync |
| ordered_scan | 20 | Twenty complete ordered scans with 25% tombstones; includes callback checksum |
| age_filter | 20 | Twenty `age >= 75` scans over the same tombstone state; includes callback checksum |
| reactivate | N/4 | Reinsert all deleted IDs with updated payloads; excludes sync |
| sync_changes | 1 | Flush final dirty data/metadata after update/delete/reactivate |

The first cache is populated by validation at open; every page in the sequential
Pager is explicitly loaded. Exact result comparisons and a warm-up lookup pass
per method precede the timed batches. Timed lookup order alternates with trial
number; scan/filter order also alternates and each receives an immediate untimed
warm-up scan. This reduces order bias but cannot eliminate frequency/OS noise.

Mutation order is deliberately fixed by state dependencies (insert, update,
delete, reactivate). These different operations are not interchangeable competing
implementations and their times should not be interpreted as a controlled
algorithm-versus-algorithm comparison.

Full point-result comparisons against a Record array and structural validation
occur outside timing after insert/update/delete and after the final close/reopen.
Lookup checksums are independently derived from generated data; exact outputs
are compared outside timing. Each timed scan verifies row count and the model
checksum. The test suite separately checks complete ordered scan contents.
Unsigned checksum overflow is defined; checksums supplement exact checks rather
than proving equality alone. Printing, reference generation, validation, cleanup
and query construction are outside operation timings, except `open_validate`.

## Cache, I/O and host conditions

Recorded on 2026-10-15: Arch Linux x86_64, kernel `7.2.9-arch1-1`, Intel Core
i5-1235U (10 cores, 12 logical CPUs), 7819348 KiB reported RAM, GCC 16.2.1,
CMake 4.4.4. Release flags: `-O3 -DNDEBUG -std=c17`, plus
`-Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes`.
No sanitizers, LTO or profiling instrumentation in the recorded baseline.

**`/tmp` is tmpfs on this host.** The sync experiments include actual `pwrite` and
`fsync` system calls to a memory filesystem, **not physical disk/SSD latency**.
Reopen includes filesystem reads and validation, with OS caches uncontrolled.
Lookup, mutations and scans measure CPU/cache work after pages are populated.
No experiment is called cold-cache. File-system behavior elsewhere will differ.

The process is single-threaded. No affinity, frequency locking, CPU isolation,
cache eviction or external host-load control was applied. Other engine tests
were not deliberately run alongside the measured baseline. Five repetitions
within one invocation are not independent machine-level experiments or confidence
intervals. Model data and two lookup page caches fit in memory. Reproduce on the
same mount type for comparable sync results; these are not durable-commit costs.

## Recorded results

Raw samples: [phase 5 CSV](../benchmarks/results-phase5-2026-10-15.csv).
All 44 summaries, including min/max and MAD:
[summary CSV](../benchmarks/summary-phase5-2026-10-15.csv).
MAD is the median absolute deviation from the median, without a scale factor.

Lookup times are ms per 2000-query batch; each cell is median ± MAD (not a
confidence interval):

| Records | Indexed ms | Sequential ms | Sequential/indexed median ratio |
| --- | --- | --- | --- |
| 1000 | 0.322252 ± 0.003530 | 1.498043 ± 0.050043 | 4.65 |
| 10000 | 0.454206 ± 0.017276 | 13.297411 ± 0.105617 | 29.28 |
| 50000 | 0.744984 ± 0.011782 | 140.732848 ± 0.760360 | 188.91 |
| 100000 | 0.917055 ± 0.018260 | 287.190818 ± 4.461099 | 313.17 |

At 100000 rows (all values ms per batch):

| Operation | Batch size | Median | Min..max | MAD |
| --- | --- | --- | --- | --- |
| Insert | 100000 | 93.443306 | 92.770464..94.103980 | 0.585289 |
| Update | 100000 | 43.062590 | 42.019754..43.419385 | 0.356795 |
| Logical delete | 25000 | 10.042866 | 9.802508..10.592885 | 0.240358 |
| Reactivate | 25000 | 11.055353 | 10.562834..11.515506 | 0.344297 |
| Ordered scan | 20 scans | 233.127949 | 232.617029..240.941606 | 0.219873 |
| Age filter | 20 scans | 226.458505 | 224.842582..227.126511 | 0.297640 |
| Open/validate | 1 reopen | 17.849977 | 17.379776..18.003133 | 0.106178 |
| Sync inserts, tmpfs | 1 sync | 3.406061 | 3.207686..3.615699 | 0.168905 |
| Sync changes, tmpfs | 1 sync | 1.698130 | 1.660772..1.925055 | 0.035671 |

The lookup advantage grows with N in this cached workload. The age filter still
visits all entries; its slightly lower scan time is consistent with computing
fewer output checksums, not an age-index shortcut. The benchmark itself includes
checking/consumption costs, so times are not isolated instruction costs.
Phase 3 measurements are historical and use different instrumentation/data;
do not treat the new numbers as a controlled regression against that experiment.

## Complexity and page accesses

Let N be allocated slots/index keys **including tombstones**, P allocated pages,
H index height, B node capacity, and K output rows. Page and record sizes are fixed.

| Operation | CPU complexity | Page work / qualification |
| --- | --- | --- |
| Sequential ID lookup | O(N) | Up to all data slots/pages (benchmark only) |
| Indexed ID lookup | O(H log B) = O(log N) | H index pages, plus one data page on a key hit |
| Ordered scan / age filter | O(H + N), plus callback work | Leaf chain plus N referenced slots; not necessarily physically sequential data |
| New indexed insert | O(H log B + B H) worst case | Leaf rewrite and possible split cascade; fixed B gives O(log N); metadata/data also changed |
| Non-key UPDATE | O(log N) | Locate, snapshot/rewrite one data page |
| Logical DELETE / reactivate | O(log N) | Locate, snapshot data and metadata; no tree rewrite |
| Open / .stats validation | O(P + N) for fixed page size | Complete file traversal and index/data consistency checks |
| Sync | O(P + D) for fixed page size | Scan cache bookkeeping and write D dirty pages, then fsync; latency depends on filesystem |

4096-byte pages hold 340 leaf entries or up to 510 internal children. High fanout
keeps H small (observed 2 or 3 at 100000 rows, depending on insertion order).
Binary search reduces comparisons within each node. Page visits are logical
accesses, not necessarily disk reads: this Pager preloads at open and never evicts.
A bounded-cache engine would turn misses into physical I/O, and randomized
record locations would matter much more. Complexity describes growth; allocation,
serialization, page copying, cache locality, clock frequency and filesystem latency
determine real time. Logical deletion does not reduce N or H.

## Portfolio chart provenance (2026-10-23)

The static [MiniDB showcase](https://minibd.pages.dev/) plots the
`indexed_lookup` and `sequential_lookup` median values from
[summary-phase5-2026-10-15.csv](../benchmarks/summary-phase5-2026-10-15.csv)
for **2,000 queries** at 1,000, 10,000, 50,000 and 100,000 records, with five
trials and preloaded caches. It uses a logarithmic time axis. Selecting a
dataset displays the ratio of the two median batch times at that size.
These are **within-MiniDB comparisons**, not measurements against Oracle, and
neither measures cold storage I/O. The website adds no new engine benchmarks.

## Profiling and optimization decision

Neither perf nor Valgrind was installed. GNU gprof 2.47 was available. After
recording the uninstrumented baseline, a separate Release build added `-pg -g`
and ran the complete benchmark (including checks and warm-ups):

```sh
cmake -S . -B build-profile -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DMINIDB_BENCHMARKS=ON '-DCMAKE_C_FLAGS=-pg -g' -DCMAKE_EXE_LINKER_FLAGS=-pg
cmake --build build-profile -j2
cd build-profile
./benchmark_engine > profile-samples.csv
gprof -b -p ./benchmark_engine gmon.out > profile-flat.txt
```

The recorded [flat profile](../benchmarks/profile-phase5-2026-10-15.txt) attributed
60.25% of sampled self CPU time to benchmark-only `sequential_select`, 19.25% to
`record_deserialize`, 3.83% to `visit_record`, 3.26% to `btree_search` and 2.97% to
`record_name_valid`. Sampling resolution was 0.01 s, with 10.44 s attributed self
time. This is an instrumented whole-harness profile, **not** a profile restricted
to timed production operations; validation and warm-up calls contribute too.
Inlining, sample granularity and instrumentation affect attribution. Profile
sample timings were not substituted into the baseline CSV.

The profile identifies linear scanning and repeated payload validation as costs
for future investigation, not a reason to remove integrity checks. No measured
bottleneck required a production algorithm change for this educational scale;
no speculative optimization was applied. Future work should first isolate a
representative production workload, preserve correctness checks, and record a
new comparable before/after baseline. No performance-regression improvement is
claimed by this phase.
