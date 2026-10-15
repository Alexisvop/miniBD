# Architecture through phase 2

## Data flow

```text
main: arguments, open, REPL, close and final error status
  |
  +-- REPL: bounded input, help/exit, diagnostics
        +-- Parser: text -> Command + ParseStatus
        +-- Executor: Command -> storage operation + formatted result
              +-- Storage: rows, uniqueness, scans and format validation
                    +-- Record codec: field validation and explicit bytes
                    +-- Pager: file descriptor, cached pages, dirty flags, fsync
```

`status.h` defines shared structured database errors; `encoding.h` provides
alignment-independent little-endian uint32 helpers. There are no external
runtime libraries. The code uses C17 and POSIX file/terminal interfaces; no
Linux-only API is required by the production code. The deterministic fault test
uses GNU-compatible linker wrapping on Linux, separate from production code.

## CLI, parser and executor

`main` now requires a database path, opens it before reading commands, and closes
it on every return from the REPL. It reports errors from opening, synchronization,
closing, and output flushing. Missing/extra process arguments are rejected before
opening storage. `--help` does not create a database.

The existing bounded REPL reader and parser grammar are retained. `repl_run`
now borrows a `Storage *` in addition to its streams. The dispatch switch sends
INSERT, SELECT, and SELECT_ALL to `executor_execute`; meta-commands stay in the
REPL. Input buffers and command objects remain caller-owned stack objects.

The parser still publishes a command only on successful validation. The UTF-8
checker moved to the record module so parser input and records supplied directly
through the C storage API follow the same validation rules. No parser test was
removed. REPL/CLI tests retain the previous input/error scenarios, with actual
execution results replacing phase 1 parse-preview expectations.

`executor_execute` maps commands to storage calls and returns `DbStatus`. A scan
visitor prints each row, propagating output failures. Duplicate IDs, missing
records, invalid records, and capacity errors are recoverable command errors;
fatal storage/allocation/I/O errors end the REPL. Buffer flushing also detects
errors that a buffered `fprintf` could initially hide.

## Record codec

`Record` is an in-memory structure only. The disk codec has fixed 70-byte input
and output buffers. It explicitly writes an ID, age, name length, and a zero-padded
64-byte UTF-8 payload. Validation precedes copying. Deserialization never exposes
an unterminated name to a caller and rejects noncanonical padding.

The grammar remains case-sensitive, without escaping or semicolons. IDs are
uint32 decimals, names are 1–64 UTF-8 bytes, and ages are 0–150. The format and
schema share these limits but do not depend on native struct layout.

## Storage invariants and operations

`Storage` owns a `Pager` and the validated total row count. Page 0 is metadata;
data pages follow contiguously, densely packed at 58 records each. Only the last
data page may be partial, and no empty data page is allocated.

- **Open:** initialize a zero-byte file or fully validate an existing one.
  Validate the header before using its counters for allocations. Read and check
  every data page, deserialize every row, and reject duplicate IDs using a
  temporary sorted ID array. Discard the handle without flushing on failure.
- **SELECT:** scan pages and occupied slots, compare ID bytes, then deserialize
  the matching row. Return `DB_NOT_FOUND` without modifying output if absent.
- **INSERT:** validate/serialize, scan for duplicates, check capacity, acquire
  metadata and the last/new page, then append and update counts. All operations
  that can allocate or read occur before publishing the row. Allocation failure
  can mark an unchanged metadata page dirty, but does not change row/page counts.
- **SELECT_ALL:** visit rows in physical insertion order through a callback.
  A callback failure stops the scan. Callbacks borrow each row for one invocation
  and must not mutate or close the scanned storage.
- **Sync/close:** delegate writes and resource management to the pager; errors
  propagate to callers. Close consumes the handle even when it fails.

Lookup, duplicate checking and complete scans are O(N). A sequence of N distinct
INSERT operations is O(N²). Opening validates O(N) bytes and performs an O(N log N)
ID sort; this array is discarded, so no index is implemented. This phase favors
correctness and simple persistence, not insertion throughput.

## Pager and ownership

`Pager` owns one descriptor and at most 4096 cached page objects. Its API uses
page IDs and raw byte buffers, with no record, metadata-field, or node-layout
knowledge. Page zero is written last by convention. New B+ tree page types can
use the same allocation/read/write API in phase 3, while Storage and disk format
validation will need to evolve.

Read buffers returned by `pager_get` are const. `pager_write` returns a mutable
buffer and marks its page dirty. `pager_allocate` checks capacity, allocates a
zeroed buffer, and only then increments the page count. Borrowed pointers stay
valid until close because buffers are never evicted or moved.

`pread`/`pwrite` loops retry EINTR and handle short transfers. The file is checked
with `fstat` before pages are allocated. All allocations are checked. On sync
failure the pager enters a failed state, stops accesses, and avoids write retries
at close. `pager_discard` closes and frees without synchronization, which is
necessary for safely rejecting invalid files.

A fixed pointer table and no eviction keep ownership simple and cap payload
memory at 16 MiB. This is a deliberate initial limit, not a configurable cache
replacement policy. Storage validation loads all pages when opening a populated
file. Single-process access is assumed; there are no locks or cache invalidation
notifications from other processes.

## Error and persistence boundary

An accepted INSERT changes memory immediately. Normal `.exit`/EOF triggers full
writes and file fsync; it is the successful close that completes the CLI's
persistence boundary. A failed close returns a nonzero process status even if
INSERT previously printed OK. There is no commit/rollback or atomic group of
page writes. Read [the format and sync contract](storage-format.md) before adding
transactional or crash-safety claims.

The next phase is a B+ tree, only when explicitly requested. UPDATE, DELETE,
`.stats`, `.btree`, SQL, WAL, recovery, and concurrent access remain unimplemented.
