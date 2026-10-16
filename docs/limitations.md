# Known limitations and guarantees

MiniDB is a single-table educational C17/POSIX engine, not a production DBMS or an
Oracle Database-compatible engine. Use one process and one owner per database.
No file locks, thread synchronization, coherent concurrent reader cache or writer
coordination exist. Concurrent access can corrupt files.

## What success means

A successful INSERT/UPDATE/DELETE means accepted into memory. The Pager's savepoint
can restore the current mutation after ordinary allocation/capacity failures,
including a split, while preserving earlier accepted changes. It is not a
user-visible transaction, session rollback, isolation mechanism or durable commit.

At `.exit`/EOF or explicit C API synchronization, dirty nonmetadata pages are
written in page-ID order, followed by metadata and file `fsync`. The exit/close
status must be checked. The directory is not fsynced when a new file is created.
The tests establish persistence after successful normal close and reopen, including
fresh processes; they do not establish power-loss durability.

| Failure window | Possible result |
| --- | --- |
| Before synchronization | New cache changes lost on termination; file may retain an earlier state |
| Partial data/index page write | Torn record/node, invalid references or a valid-looking altered payload |
| Between index/data pages | Missing index entries, mismatched keys/locations, orphan pages or stale metadata |
| Between bitmap and metadata writes | Deleted visibility and active count may disagree |
| During/after fsync failure | Durability uncertain even if writes reached the OS cache |
| After create without directory synchronization | Name/directory persistence is not guaranteed by file fsync alone |

Write, zero-write or fsync failure poisons the Pager. Subsequent access and write
retries are rejected; close releases resources and reports failure. Validation
rejects many inconsistent files but cannot repair them. No WAL, checksums, redo,
undo recovery or ACID transaction protocol is implemented. No abrupt-termination
experiment is used to claim recovery. Ctrl-C is not a normal database close.

REPL output failure after a successful mutation does not undo it; main attempts
normal close after caught errors. Recoverable input/duplicate/missing/deleted errors
allow later commands and may still yield exit 0. This is not an atomic batch CLI.

## Capacity and compatibility

- 4096 pages of 4096 bytes, at most a 16 MiB file. Index occupancy consumes capacity;
  the ascending capacity regression reaches 176900 allocated records, not a
  universal maximum for every insertion order.
- Cache retains all accessed pages. Open validates and caches the entire file;
  there is no eviction or bounded working set smaller than the database.
- Logical DELETE retains the payload, slot and index key. It is not secure erasure
  or reclamation. Reinsertion of the same ID reuses its slot; unrelated IDs cannot.
- Format v3 is unchanged in phase 5. V1/v2/unknown versions are rejected without
  migration. Empty regular files are initialized; nonempty malformed files are not.
- Structural validation is not a checksum: valid-looking changes can go undetected.
- No physical deletion/rebalancing, compaction, migration tool, variable schema,
  SQL standard, joins, compound expressions, age index, optimizer, replication,
  distribution, authentication or multiwriter support.
- Names contain 1..64 UTF-8 bytes, not 64 Unicode characters. No normalization,
  collation or escaping. Age is 0..150; ID is uint32. Input is capped at 4096 bytes.
- C API handles/output buffers must satisfy their documented lifetimes; callbacks
  may not mutate/close storage. This is not a stable external ABI commitment.

## Measurement and validation limits

The [benchmarks](benchmarking.md) fit in memory and report host-specific results;
no cold-cache, concurrency or production-throughput claim follows. Tests are
repeatable finite scenarios, not proof that all bugs or corruptions are detected.
Linux linker wrappers simulate failures; they are not actual failing hardware.
Python 3.8+ is needed only for the process oracle and result summarizer, not the
engine. No third-party database is used as the implementation or reference model.
