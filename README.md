# MiniDB

An educational relational database engine built from scratch in C17. The project
explores systems programming and database internals as preparation for database
engine engineering internships, including Oracle MDC. It does not use SQLite or
another database backend and is not a production database.

**Current milestone: phase 2 — persistent sequential storage.** INSERT, SELECT,
and SELECT_ALL now operate on a binary database file. Records persist after a
successful normal close and can be retrieved in a new process. The index and
later query operations remain future work.

## Implemented

- Interactive REPL with `.help`, `.exit`, EOF handling and bounded input.
- Validated INSERT, SELECT and SELECT_ALL commands, with errors for duplicate
  IDs and missing records.
- Explicit 70-byte record serialization, independent of C padding/byte order.
- Versioned little-endian file format with 4096-byte metadata and data pages.
- Pager with stable cached buffers, dirty tracking, complete reads/writes and
  file synchronization at normal close.
- Structural validation on open, including malformed UTF-8, truncated files,
  unsupported versions, invalid counts/types/padding and duplicate stored IDs.
- Strict C17 compilation, CTest regression/integration tests and ASan/UBSan builds.

## Build on Arch Linux

Required: GCC, CMake 3.16 or newer and Ninja (or Make). GDB is optional.
There are no third-party runtime libraries; production uses the C library and
POSIX file and terminal interfaces.

Install missing tools if needed:

```sh
sudo pacman -S --needed base-devel cmake ninja gdb
```

From the project directory (these commands also work in Fish):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/minidb database.db
```

The executable runs from the build directory; no system installation is needed.
To use Make, choose `-G "Unix Makefiles"` in a separate build directory. Warning
flags target GCC and Clang on POSIX systems. Build artifacts and `*.db` files
are ignored by Git.

## Usage and persistence

The database path is now required. A missing file or an existing zero-byte file
is initialized; a nonempty invalid file is rejected without being rewritten.
`--help` displays usage without opening any database.

First session:

```text
minidb> INSERT 1 "Alexis" 21
OK: 1 row inserted
minidb> INSERT 2 "Carlos" 22
OK: 1 row inserted
minidb> .exit
```

Reopen with `./build/minidb database.db`:

```text
minidb> SELECT 1
1 | Alexis | 21
minidb> SELECT_ALL
1 | Alexis | 21
2 | Carlos | 22
minidb> INSERT 1 "Duplicate" 99
ERROR: duplicate ID
minidb> SELECT 999
ERROR: record not found
minidb> .exit
```

SELECT_ALL follows **insertion order**, not ascending ID. Searches and duplicate
checks scan sequentially; no index exists yet. The two-session example above
was executed during phase 2 verification and produced an 8192-byte database
(one metadata page and one data page).

`OK` means the row was accepted in memory. `.exit` or EOF writes dirty pages,
then calls `fsync` and closes the file. **Check the exit status**: a write, sync
or close failure returns 1 and may leave an incomplete file. Ctrl-C or unexpected
termination is not a normal close and can lose changes. There are no crash or
power-loss guarantees.

Piped input suppresses the banner and prompt:

```sh
printf 'INSERT 1 "Alexis" 21\n.exit\n' | ./build/minidb example.db
printf 'SELECT 1\n.exit\n' | ./build/minidb example.db
```

Use a new `example.db` for this example; re-running INSERT against the same file
correctly rejects the duplicate ID. EOF also processes and saves a final command
without a trailing newline.

## Command grammar

This is a small case-sensitive language, **not SQL**.

| Input | Rules |
| --- | --- |
| `INSERT <id> "<name>" <age>` | All three fields required; ID must be unique. |
| `SELECT <id>` | Exactly one ID; missing records return a controlled error. |
| `SELECT_ALL` | No arguments; rows in insertion order. |
| `.help`, `.exit` | No arguments. |
| ID | Decimal digits, 0..4294967295; leading zeroes accepted. |
| Name | 1–64 UTF-8 bytes, enclosed in double quotes; spaces preserved. |
| Age | Decimal digits, 0..150 inclusive. |

Whitespace separates tokens. Leading/trailing whitespace and blank lines are
accepted. Names cannot contain ASCII controls, DEL or double quotes. There is no
escape syntax; backslash is literal. Malformed UTF-8, signed/decimal numbers,
extra arguments and SQL semicolons are rejected. The name limit is in bytes:
32 repetitions of `é` occupy 64 bytes.

Input is limited to 4096 bytes before LF (CR counts toward that limit). Oversized
lines and lines containing NUL are consumed completely and rejected so the next
line can still be processed. Ctrl-D at an empty terminal prompt ends the session.

Invalid commands, duplicate IDs, missing records and capacity errors print
`ERROR` and allow further commands. `.exit`/EOF normally return 0 even if such
recoverable errors occurred. Invalid process arguments, opening failures,
allocation failures and fatal I/O/storage errors return 1. This is not a
transactional or fail-fast batch query runner.

## Storage architecture and format

```text
main -> REPL -> Parser -> Command
          |
          +-> Executor -> Storage -> Pager -> database file
                            |
                            +-> Record codec + little-endian helpers
```

The parser performs no I/O or allocation. The executor maps validated commands
to storage operations. Storage owns record placement, uniqueness and format
validation; Pager owns descriptors, raw pages, cache buffers and synchronization.

| Property | Version 1 disk format |
| --- | --- |
| Metadata | Page 0: magic, version, page size/count, row count, reserved root ID and type |
| Data | Pages 1 onward: 16-byte header, 58 record slots, 20-byte tail padding |
| Record | 4-byte ID, 1-byte age, 1-byte name length, 64-byte name payload |
| Integer order | Little-endian |
| File limit | 4096 total pages, 16 MiB, 237510 records |
| Cache | No eviction; at most 16 MiB of page payloads plus bookkeeping |

Names are explicitly length-prefixed and zero-padded on disk. No native `Record`
structure is written directly. Page types and a format version leave room for
future extensions; a B+ tree format may require migration. See the complete
[offset tables and sync contract](docs/storage-format.md),
[architecture and ownership](docs/architecture.md), and
[engineering log](docs/engineering-log.md).

## Verification

The three phase 1 suites were run successfully before changes. Parser tests
remain intact; REPL and CLI assertions now expect real storage results. The
phase 2 Linux build has eight CTest suites:

| Suite | Coverage |
| --- | --- |
| `parser` | Original syntax, numeric bounds, UTF-8 and output-preservation cases |
| `repl` | Help/exit, prompts, EOF/CRLF, oversized/NUL input, recovery, stream errors |
| `record` | Exact serialized bytes, full-length names, round trips, invalid records |
| `pager` | Cache, dirty writes, reopen, truncated reads and the 4096-page limit |
| `storage` | Empty database, multi-page rows, scans, duplicate IDs, UTF-8, reopen and capacity |
| `corruption` | Headers, unsupported versions, types, counters, padding, records, duplicate IDs and truncation; rejected files unchanged |
| `failures` | Injected allocation/permission failures, short transfers, EINTR, disk-full/zero writes, fsync/close failures and no write retries |
| `cli` | Separate-process persistence, EOF save, duplicate/missing keys, arguments and invalid-file preservation |

The capacity test prepares a valid nearly-full file directly, then inserts the
237510th row, rejects another row, and reopens the result. This validates the
capacity boundary; it is **not** a benchmark or a claim that 237510 rows were
loaded via CLI INSERT. Multi-page INSERT testing uses 175 sequential API calls
with descending IDs, plus boundary-ID/name cases. A full INSERT workload has
quadratic cost until indexing is implemented.

The failure suite uses GNU-compatible linker `--wrap` on Linux without adding
fault-injection hooks to production. Non-Linux builds omit that suite. All
other suites use C17/POSIX APIs. Test checks remain active in Release builds.

Sanitizer configuration (Fish-compatible):

```sh
cmake -S . -B build-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DMINIDB_SANITIZERS=ON
cmake --build build-sanitize
env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize --output-on-failure
```

Verified on 2026-10-14 on Arch Linux x86_64 with GCC 16.2.1, CMake 4.4.4 and
Ninja 1.13.2: **8/8 suites passed in Debug, and 8/8 with ASan, UBSan and leak
detection enabled**, without sanitizer findings. The sanitizer test process ran
outside the sandbox because its ptrace restrictions prevent LeakSanitizer from
working there. Valgrind was not run. There are no performance benchmark results
or B+ tree/transaction/crash-recovery test claims for this milestone.

## Debugging

Use the Debug build:

```sh
gdb --args ./build/minidb database.db
```

Useful GDB commands:

```text
break storage_insert
run
```

Enter an INSERT command in the MiniDB prompt. Use `print *record`, `next`, `bt`
and `continue` to inspect insertion. Set `break pager_sync` to follow the flush
path during initialization and normal close. `break parser_parse` still exposes
the parsing boundary. Avoid running LeakSanitizer under GDB/ptrace.

VS Code can run these commands in its integrated terminal. With an optional C/C++
debugging extension, use `build/minidb` as the program, `database.db` as its
argument, `gdb` as debugger and the workspace as working directory.

## Limitations and next steps

- One fixed-schema table, sequential scans and O(N²) bulk insertion.
- Opening scans all records, loads all pages and temporarily sorts IDs to reject
  duplicate corruption; the ID array is discarded, not retained as an index.
- Bounded file/cache size and no page eviction, space reuse or compaction.
- One process at a time; no locking, concurrent access or external file changes.
- No checksums: structurally valid accidental changes may go undetected.
- No ACID transactions, WAL, rollback, crash/power-loss recovery or directory fsync.
- No UPDATE, DELETE, B+ tree, `.stats`, `.btree`, full SQL, joins or replication.

Phase 3 will add a persistent B+ tree only after it is requested. Later phases
will address updates/deletion, structural inspection, expanded tests and
reproducible benchmarks. No third-party source code is vendored.

Suggested phase 2 commit:
`feat(storage): add paged persistence, record serialization, and failure tests`.
