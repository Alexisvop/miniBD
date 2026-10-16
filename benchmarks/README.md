# Benchmark entry points

The current phase 5 benchmark is `engine.c`, covering lookup, mutations, scans,
filters, open and synchronization. See [methodology and results](../docs/benchmarking.md),
[raw trials](results-phase5-2026-10-15.csv), [summary](summary-phase5-2026-10-15.csv)
and [gprof profile](profile-phase5-2026-10-15.txt). `summarize.py` validates all
44 experiment groups and reports medians, min/max and median absolute deviations.

The lookup-only benchmark below is retained as historical phase 3 evidence.

> The recorded measurements below are historical phase 3/v2 results, not phase 4 measurements.
> The current benchmark builds fresh v3 databases containing only active rows; it does not measure tombstones or age filters.

# Lookup benchmark

Standalone cached-lookup comparison, excluded from CTest. The recorded run used
the same v2 file and identical queries for indexed and sequential retrieval. Sequential
lookup is implemented only in `benchmarks/lookup.c`: the phase 2 slot scan adapted
to skip index pages, without modifying production SELECT.

## Reproduce

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DMINIDB_BENCHMARKS=ON
cmake --build build-release
./build-release/benchmark_lookup
```

Temporary files are created under `/tmp` and removed after each successful size.
No existing user database is used. The benchmark needs no Python or external
runtime dependency. Raw CSV from the recorded run is committed alongside this
file; a new run should produce its own output rather than overwrite that record.

## Method

- Sizes: 1000, 10000, 100000 rows inserted in shuffled order using a specified
  uint32 LCG and Fisher-Yates shuffle (initial seed `0x12345678`).
- Close and reopen before measuring. Storage validation fills the indexed cache;
  explicitly load all pages into an independent Pager for the sequential scan.
- Use 2000 identical queries for each method, 75% existing even IDs and 25%
  nonexistent odd IDs, with query seed `0xabcdef01`.
- Run one unmeasured warm-up pass per method, then five measured trials each,
  alternating which method runs first. Report the median batch time per method.
- Use `clock_gettime(CLOCK_MONOTONIC)` and verify result checksums for every pair.
  Both successful paths deserialize the same record and consume its ID/age/name.
- Exclude insertion, opening, validation, synchronization, printing and cleanup
  from measured intervals. Both read handles remain unmodified during comparison.

This measures **cached CPU lookup work**, not disk latency or cold-cache I/O.
The sequential baseline scans the same mixed-page layout and incurs a type check
for index pages; it is not a separately running v1 binary. Both methods use their
own fully populated page cache, so total benchmark memory includes two caches.

## Recorded measurements: 2026-10-14

Environment: Arch Linux x86_64, kernel 7.2.9-arch1-1, Intel Core i5-1235U, GCC
16.2.1, CMake Release (`-O3 -DNDEBUG`), no sanitizers or LTO. Run inside the local
execution environment, without CPU affinity, frequency locking or isolation from
other host work. Data fit in memory. Five trials belong to one process invocation,
not independent machine-level experiments; ratios may vary on another run.

| Rows | Indexed median, ms / 2000 queries | Sequential median, ms / 2000 queries | Sequential / indexed |
| --- | --- | --- | --- |
| 1000 | 0.170638 | 0.675523 | 3.96 |
| 10000 | 0.233652 | 7.107775 | 30.42 |
| 100000 | 0.571701 | 156.828272 | 274.32 |

These measurements support the expected scaling advantage for this particular
cached workload. They do not measure inserts, full scans, durability, opening
cost, cold caches or concurrent access. No production throughput claim follows.
See [raw CSV](results-2026-10-14.csv) and the source for exact reproducibility.
