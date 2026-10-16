> Historical phase 3 format. Application v0.4 rejects v2 files without modification.

# MiniDB disk format v2 (phase 3)

Application v0.3 uses **format version 2**. All multibyte fields are unsigned
little-endian integers; all pages are 4096 bytes. No C structure is written
as native memory. Record serialization remains 70 bytes, unchanged from phase 2.

## Compatibility

Version 1 assumed all pages after metadata were data pages and forbade a nonzero
root ID. Version 2 mixes data and index pages and uses the root field, so this is
an incompatible format change. Version 1 is explicitly rejected without writes.
There is **no automatic or bundled offline migration tool**. Keep old files and
use the phase 2 executable to read/export them if needed; use a fresh path for v2.
The [v1 format](storage-format-v1.md) is retained as a historical reference only.
Unknown versions are also rejected, never silently reinterpreted.

## Metadata page 0

| Offset | Bytes | Field | Meaning |
| --- | --- | --- | --- |
| 0 | 8 | Magic | `MINIDB\r\n` (`4d 49 4e 49 44 42 0d 0a`) |
| 8 | 4 | Version | 2 |
| 12 | 4 | Page size | 4096 |
| 16 | 4 | Total pages | All types, including metadata; must match file size |
| 20 | 4 | Total records | Must equal occupied data slots and indexed entries |
| 24 | 4 | Root page ID | Nonzero ID of the one B+ tree root |
| 28 | 4 | Page type | 1 (metadata) |
| 32 | 4 | Last data page ID | 0 only when empty; otherwise the last allocated data page |
| 36 | 4060 | Reserved | Zero |

Empty databases contain page 0 and an empty root leaf at page 1, totaling 8192
bytes. The first data page is allocated at the first INSERT. A missing or
zero-byte file is initialized and synchronized. Nonempty invalid files are
rejected without reinitialization, repair or flushing.

## Shared page addressing and types

File offset = `page_id * 4096`. Valid allocated IDs are `0..total_pages-1`.
References from the tree must be nonzero and less than the total page count.
Zero is the sentinel for no next leaf and no data page in an empty database.
Page type is a uint32 at offset 0 except for metadata (offset 28).

| Type | Meaning |
| --- | --- |
| 1 | Metadata, only at page 0 |
| 2 | Data page |
| 3 | B+ tree leaf |
| 4 | B+ tree internal node |

Data and index pages are interleaved by allocation time. A page's ID no longer
implies its type or its position among data pages. Existing pages are not reused.

## Data page (type 2)

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | Type = 2 |
| 4 | 4 | Occupied slots, 1..58 |
| 8 | 8 | Reserved zero |
| 16 | 4060 | 58 slots of 70 bytes; unused slots are zero |
| 4076 | 20 | Zero padding |

Only the last data page may be partial. Other data pages are full. All data pages
remain in insertion order by page ID, but there may be index pages between them.
Record location `(page, slot)` means file offset `page*4096 + 16 + slot*70`.

## Record (unchanged)

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | uint32 ID |
| 4 | 1 | Age 0..150 |
| 5 | 1 | UTF-8 name length, 1..64 bytes |
| 6 | 64 | Name bytes followed by zero padding |

No disk terminator is required when the payload fills all 64 bytes. In memory,
`Record.name` has an extra byte for NUL. Empty names, ASCII controls, DEL, double
quotes, invalid UTF-8 and nonzero name padding are rejected. ID 0 and UINT32_MAX
are both valid. The index stores locations, not duplicate copies of full records.

## Leaf node (type 3)

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | Type = 3 |
| 4 | 4 | Entry count, 0..340 (0 allowed only for an empty root) |
| 8 | 4 | Next leaf ID, or 0 |
| 12 | 4 | Reserved zero |
| 16 | 4080 | 340 entries of 12 bytes; unused bytes zero |

Entry `i` begins at `16 + 12*i`: ID at +0, data page ID at +4, slot at +8.
Capacity is `floor((4096-16)/12) = 340`. Nonroot leaves contain at least 170
entries. Entry IDs are strictly increasing. Leaf links match the in-order
sequence of tree children, and the rightmost leaf has next=0.

## Internal node (type 4)

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | Type = 4 |
| 4 | 4 | Separator count, 1..509 |
| 8 | 8 | Reserved zero |
| 16 | 4 | Child 0 |
| 20 | 4072 | 509 pairs: separator uint32, right child uint32 |
| 4092 | 4 | Zero padding |

With `n` separators there are `n+1` children. Separator `i` is the exact minimum
key in child `i+1`. Keys below the first separator go left; equality goes right.
Capacity is `floor((4096-16-4)/8) = 509`. Nonroot internal nodes have at least
254 separators; an internal root needs at least one.

There is no redundant root flag or parent pointer. The root is identified by
metadata offset 24; all other reachable nodes are nonroot. See
[B+ tree algorithms and invariants](btree-design.md).

## Validation on open

Opening checks file type/length/limit, magic, version, page size/count, reserved
bytes and known types. It validates all data records and dense data-page packing.
Then it walks the stored tree (it does **not** rebuild it), verifying:

- Capacity/minimum occupancy, sorted unique keys and zero unused bytes.
- Valid, uniquely visited child references; no cycles, aliasing or orphan nodes.
- Exact separator minima and disjoint child ranges.
- Equal leaf depths and correct next-leaf chain, including terminal zero.
- Each index location addresses an occupied data slot with the same ID.
- Indexed entry count equals the data record count and metadata count.

Strict unique index keys, matching record IDs and equal counts ensure every data
slot is represented once. Corrupted duplicate data IDs cannot both be indexed.
No checksums exist: a consistent change to otherwise valid values may evade
structural validation. External modifications while open are unsupported.

## Limits and ownership

The Pager's maximum remains 4096 total pages, a 16 MiB file. Index pages consume
part of that budget, so the phase 2 limit of 237510 rows is now only a data-only
upper bound. Actual capacity depends on node occupancy and insertion order. The
ascending-insert capacity test reached **176900 rows** before an allocation
needed for the next insertion could not fit; failed insertion preserved the tree.

All cached page buffers remain stable until close. There is no eviction. Opening
validates and loads the full file. Payload memory is at most 16 MiB, plus pointer
bookkeeping, temporary before-images of changed pages and bounded traversal
stacks. No query index is rebuilt or held separately from the cached disk nodes.

## Normal-error consistency and disk synchronization

Each INSERT uses a Pager memory savepoint. The first write to an existing page
copies its prior bytes and dirty flag. Newly allocated pages are tracked by the
initial page count. If any allocation/read/index step fails, rollback restores
all modified pages and dirty flags, drops new pages and restores root/split
state. Metadata and public row counters advance only after successful insertion.
No fallible operation occurs after publishing those counters. This also preserves
earlier accepted changes that have not yet been synchronized.

This mechanism is **not an ACID transaction or WAL**. No sync is allowed during
a savepoint, and disk writes happen only outside it. Successful INSERT means
accepted in the cache. `.exit`/EOF closes normally: write all dirty nonmetadata
pages in ID order, then metadata, then file `fsync`; clear dirty flags only after
success. C callers may also invoke `storage_sync`. No directory fsync is done.

Write/zero-write/fsync failure poisons the Pager, propagates to the CLI, and blocks
further access or automatic write retries. Closing releases memory and reports
errors. Partially written files may be rejected on reopen. Ctrl-C, crashes, power
loss and concurrent access remain unsupported. The in-memory savepoint does not
repair partial writes or make multi-page disk changes atomic.
