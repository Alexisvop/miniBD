# MiniDB

An educational database engine written from scratch in C17, exploring systems
programming and database internals as preparation for database engineering
internships, including Oracle MDC. No existing database backend or external tree
library is used. This is not a production database.

**Current milestone: phase 4 — query execution, UPDATE, logical DELETE and age
filters.** Persistent storage and the ID B+ tree from earlier phases remain.

**Compatibility:** application v0.4 uses **disk format v3**. Old v1/v2 files are
rejected explicitly without modification. No migration tool is included; keep
old databases and use a fresh path for this version.

## Build and run

On Arch Linux, install missing tools if necessary:

```sh
sudo pacman -S --needed base-devel cmake ninja gdb
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/minidb queries.db
```

Required: GCC, CMake 3.16+, Ninja or Make, and C17/POSIX. No third-party runtime
libraries are needed. Commands work in Fish. Make can be used with
`-G "Unix Makefiles"` in a separate build directory. Build artifacts and databases
are ignored by Git. `--help` does not open a database. Missing/zero-byte regular
files are initialized; nonempty invalid files are never silently reinitialized.

## Executed example

```text
minidb> INSERT 3 "Mariana" 25
OK: 1 row inserted
minidb> INSERT 1 "Alexis" 21
OK: 1 row inserted
minidb> INSERT 2 "Carlos" 20
OK: 1 row inserted
minidb> UPDATE 1 "Alexis Perez" 22
OK: 1 row updated
minidb> DELETE 2
OK: 1 row deleted
minidb> SELECT_ALL WHERE age >= 21
1 | Alexis Perez | 22
3 | Mariana | 25
minidb> .exit
```

Reopen `queries.db`:

```text
minidb> SELECT 1
1 | Alexis Perez | 22
minidb> SELECT 2
ERROR: record is logically deleted
minidb> INSERT 2 "Carlos returned" 23
OK: 1 row inserted
minidb> SELECT_ALL WHERE age <= 23
1 | Alexis Perez | 22
2 | Carlos returned | 23
minidb> .exit
```

Both sessions were executed in separate processes during phase 4 verification.
Before reactivation, .stats reported 3 total records, 2 active, 1 deleted and 3
index entries. Afterwards it reported 3 active, 0 deleted and still 3 entries.

## Commands and semantics

| Command | Behavior |
| --- | --- |
| `INSERT <id> "<name>" <age>` | Insert a new ID; reactivate the same slot if that ID is deleted; reject active duplicates |
| `UPDATE <id> "<name>" <age>` | Update an active row's name/age; ID and index location remain unchanged |
| `DELETE <id>` | Mark an active row deleted; keep its payload and index entry |
| `SELECT <id>` | Indexed lookup; return a row only if active |
| `SELECT_ALL` | All active rows in ascending ID order |
| `SELECT_ALL WHERE age <op> <value>` | Scan active rows in ID order, applying one age comparison |
| `.stats` | Total/active/deleted records, index entries, page/node counts, height and session split counters |
| `.btree` | Full physical index structure, including retained deleted IDs |
| `.help`, `.exit` | Help and normal close |

Operators are `=`, `!=`, `<`, `<=`, `>` and `>=`. Use whitespace between WHERE,
age, operator and value: `age >= 21` is supported; `age>=21` is not. The field
name is lowercase `age`; keywords are uppercase. This is not SQL. There are no
compound conditions, other filter fields or implicit conversions. An empty scan
returns `OK: 0 rows`.

IDs are decimal uint32 values (0..4294967295). Names require quotes and 1..64
valid UTF-8 bytes; the length counts bytes, not characters. Age/filter values are
integers in 0..150. Leading zeroes are allowed. Invalid UTF-8, ASCII controls/DEL
in names, quotes inside names, signed values, extra arguments and semicolons are
rejected. Backslash is literal, with no escaping. Blank lines and surrounding
whitespace are accepted. Lines over 4096 bytes before LF, or containing NUL, are
fully discarded. EOF processes a final command without a trailing newline.

UPDATE/DELETE of a missing ID return `record not found`; an already deleted ID
returns `record is logically deleted`. Repeated DELETE does not change counts.
SELECT never returns deleted payloads. Primary-key changes are not implemented.

Recoverable input/duplicate/missing/deleted/capacity errors print a diagnostic and
allow the session to continue; .exit/EOF can still return 0. Fatal file, memory,
format or I/O errors return 1. This is not a fail-fast transactional batch runner.

## Architecture and persistence

```text
REPL -> Parser -> Command + AgeFilter
  +-> Executor -> ExecutionResult + row/node callbacks
        +-> Storage -> B+ tree -> Pager -> file
              +-> 70-byte records and deletion bitmap -> Pager
```

Executor returns structured status, affected/delivered row count and optional
statistics. It can run with no terminal or callbacks; only REPL formats output.
Storage and BTree also provide structured data without printing. Callbacks borrow
their arguments and propagate errors; they must not modify the scanned database.

The B+ tree indexes ID only. Point lookups are indexed; **age filters are scans**
over the ID-ordered entries, skipping tombstones and evaluating the age condition.
There is no age index or query optimizer.

The format uses 4096-byte pages and explicit little-endian fields. Records remain
70 bytes; data pages retain 58 slots. A bitmap in each data header marks logical
deletion. Leaves retain 340 ID/location entries; internal nodes retain 509 keys.
Metadata stores total slots and active rows separately. Every index entry still
maps to a data slot, including deleted ones, and open validates this relationship.

UPDATE/DELETE/reactivation keep index pages unchanged. Memory savepoints revert
ordinary allocation/capacity failures and preserve earlier accepted changes.
`OK` means accepted in cache. **Normal .exit/EOF performs writes and file fsync;
check the exit status.** Partial writes may leave data/bitmap/metadata inconsistent.
There is no WAL, ACID commit, rollback of an entire session or crash recovery.
Ctrl-C, process termination and power loss can lose changes or corrupt the file.

Logical DELETE is not physical removal, secure erasure or space reclamation.
Tombstones retain data slots and index entries. Same-ID reinsertion reuses its
slot even at capacity, but deleted slots are not recycled for unrelated IDs.
The total file limit stays 4096 pages (16 MiB), with no cache eviction or locking.
Actual row capacity depends on index occupancy; an ascending test reached 176900
allocated rows. Opening validates/caches the whole file without rebuilding the tree.

See [architecture](docs/architecture.md), [v3 offsets and consistency](docs/storage-format.md),
[B+ tree design](docs/btree-design.md), and [engineering log](docs/engineering-log.md).

## Verification

All 11 phase 3 CTest entries remain, including the 100000-row tree tests. Four new
entries cover this milestone:

| New test | Coverage |
| --- | --- |
| `queries` | Executor without terminal I/O, UPDATE/DELETE/reactivation, all six filters, zero matches, bounds, invalid commands, callback errors and reopen |
| `mixed` | 30000 mixed operations over 3000 IDs, reference-model queries/filters, invariant checks and six reopen cycles |
| `tombstones` | Exact bitmap bits, unchanged index pages, persistent state, invalid bitmap/counts, deleted payload validation and genuine v2 rejection |
| `cli_queries` | Real process sessions, mutation/filter output, recovery, tombstone persistence and reactivation saved through EOF |

The existing failure suite now injects allocation failures during UPDATE, DELETE
and reactivation, compares closed files byte-for-byte, and exercises failed
write/fsync after the new operations. The capacity test verifies reactivation at
the page limit. No test uses real user databases. Test checks remain enabled in
Release; fault injection uses Linux linker wrapping only in tests.

Actual results on 2026-10-15, Arch Linux x86_64, GCC 16.2.1: **15/15 tests passed
in Debug and 15/15 with ASan/UBSan/leak detection**, with no sanitizer findings.
Debug and Release compiled successfully with strict warnings. The sanitizer run
used execution outside the sandbox because its ptrace restriction prevents
LeakSanitizer from operating. Valgrind was not run.

```sh
cmake -S . -B build-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DMINIDB_SANITIZERS=ON
cmake --build build-sanitize
env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize --output-on-failure
```

For shorter iterations, `ctest --test-dir build -LE stress --output-on-failure`
omits the 100000-row test. The existing [lookup benchmark](benchmarks/README.md)
still builds, but its committed measurements are historical phase 3/v2 results.
No new phase 4 performance numbers are claimed.

## Debugging and limitations

```sh
gdb --args ./build/minidb queries.db
```

Useful breakpoints: `executor_execute`, `storage_update`, `storage_delete`,
`modify_slot`, `pager_rollback`. Use the Debug build and `next`, `bt`, `continue`.
VS Code can run these commands in its terminal or through a C/C++ debugger.

Remaining limitations: no physical deletion/rebalancing, compaction, migration,
WAL/recovery, concurrent access, checksums, cache replacement, full SQL, joins,
replication or optimizer. .btree can produce large output; .stats validates the
full structure and reports current values including pending cache changes.

Phase 4 is complete; phase 5 requires a separate instruction. No commits were
created automatically. Suggested commit:
`feat(query): add updates, tombstones, age filters, and structured execution results`.
