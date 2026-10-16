# Architecture through phase 4

## Layers and ownership

```text
main: open -> REPL -> close/sync -> final exit status
  REPL: bounded input, terminal formatting, .help/.exit, error recovery
    Parser: text -> Command + optional AgeFilter
    Executor: Command -> ExecutionResult + borrowed row/node callbacks
      Storage: rows, visibility, index coordination and validation
        BTree: ID -> (page, slot), sorted leaf scan and structural inspection
        Record codec: unchanged 70-byte serialization and UTF-8 validation
        Pager: cached pages, memory savepoints, dirty writes and fsync
```

Main owns Storage; Storage owns Pager and BTree state; BTree borrows Pager.
REPL/Executor borrow Storage. Row and node callbacks borrow their argument only
for the duration of the call and must not modify/close the scanned database.
No terminal I/O remains in Executor, Storage or BTree. Only REPL formats query
rows, success messages, statistics and tree descriptions.

## Commands and structured execution

`Command` contains a type, a Record and an AgeFilter. INSERT/UPDATE use the full
Record; SELECT/DELETE use its ID. Only SELECT_ALL accepts a filter. The parser
preserves the existing strict numeric/UTF-8 checks and publishes output only on
success. WHERE grammar is deliberately limited to `WHERE age <op> <value>` with
whitespace between tokens and a value in 0..150.

`executor_execute(storage, command, sink)` returns an `ExecutionResult`:

- `status`: DbStatus for success, missing/deleted/duplicate ID, invalid command or
  record, capacity, allocation, I/O, permission, corrupt or incompatible file.
- `rows`: successfully affected rows for mutations, or query rows delivered before
  the final status. Zero matches are `DB_OK` with rows=0. Missing/deleted SELECT
  has rows=0 and invokes no row callback.
- `stats`: structured StorageStats, populated only for successful .stats.

An optional ExecutionSink supplies record/node callbacks and an opaque context.
NULL callbacks discard values but still count query results, allowing tests and
other clients to run without terminal streams. Query callback failures propagate
and preserve the count of earlier successful deliveries. There is no allocated
result list to free. .btree uses BTreeNodeView callbacks, not encoded bytes or
printing from Storage. Invalid direct C commands/filters return DB_INVALID_COMMAND.

The REPL turns successful mutations into `OK: 1 row inserted/updated/deleted`,
empty scans into `OK: 0 rows`, and recoverable statuses into diagnostics. Fatal
storage/allocation/output errors end the REPL; main still attempts normal close.
An error while rendering an already accepted mutation does not undo that mutation.

## Point operations and visibility

The B+ tree retains one entry for every allocated data slot, including tombstones.
The metadata total count and `storage_record_count` count those physical slots.
A separate active count is stored and validated against deletion bits. A missing
ID is DB_NOT_FOUND; an indexed but deleted ID is DB_DELETED. Neither state exposes
a row through public SELECT. DELETE repeated on a tombstone returns DB_DELETED.

UPDATE uses indexed lookup and requires an active row. It preserves the ID and
location, validates/serializes the new name/age, then rewrites one data slot within
a memory savepoint. No index, total count or active count changes.

DELETE performs the same lookup and sets the slot's bitmap bit. It decrements the
active count, preserving the record bytes, index entry, links, counts of occupied
slots and tree shape. INSERT on a deleted ID replaces its payload, clears its bit
and increments active count without adding a slot or index entry. INSERT on an
active ID still returns DB_DUPLICATE_ID. Slots are not reused for different IDs.

## Scans and filters

Storage follows the ID-ordered leaf chain, checks each referenced slot and emits
only active records. Executor applies one optional age predicate (`= != < <= > >=`)
to those rows before passing matches to the client. Thus results stay ordered by
ID and deleted rows cannot match, regardless of their stored age.

Age filtering is a **scan**, not an age-index search: it visits all indexed keys,
including tombstones, with O(H + N) tree work. Point lookup remains O(log N).
Only ID is indexed. There is no query optimizer, boolean expression language,
compound filter, join or SQL grammar.

## Format, validation and consistency

[Format v3](storage-format.md) uses eight former reserved data-header bytes as a
64-bit bitmap, encoded byte by byte. There are 58 possible slots; bits of unused
slots and bits 58..63 must be zero. Metadata offset 36 now stores active count.
The 70-byte record and B+ tree node formats are unchanged. V1/v2 are rejected
without modifications, and no migration tool is included.

Open validates all records, including deleted payloads, and checks every index
location against a matching record ID. The index count must equal total slots;
active count must equal occupied clear bitmap bits. The original ordering,
separator, occupancy, depth, link, cycle and reachability validations remain.
The index is loaded/validated, never reconstructed on open.

Pager savepoints preserve bytes and original dirty flags. UPDATE snapshots one
data page; DELETE/reactivation snapshot data and metadata; new INSERT may also
modify index pages. All fallible work finishes before publishing scalar counters.
Failure restores memory, including prior accepted unflushed changes. Deleted
IDs can reactivate even at the file page limit, provided memory allocation succeeds.

This is not ACID, isolation or WAL. Disk writes occur at explicit sync/normal close,
not per mutation. A bitmap page and its metadata counter can be partially written
on failure; fsync does not make those writes atomic. The Pager remains poisoned
after write/sync errors, reports them and avoids retrying writes. No crash recovery,
concurrent access, cache coherence, directory fsync or automatic repair exists.

## Statistics and inspection

.stats reports total allocated records, active records, deleted records, total
index entries, total/data/index pages, internal/leaf counts and height, followed by
session split counts. Structural values describe current state, including pending
cache changes; they are persisted only after successful sync. Session split counts
reset at open. UPDATE/DELETE/reactivation do not increment them.

.btree deliberately shows retained deleted IDs because it describes the physical
index. All-deleted databases can have zero active rows but a nonempty multilevel
tree. Logical deletion does not reclaim disk space, rebalance nodes, erase payloads
or compact the file. Page payload cache remains bounded at 16 MiB without eviction.
