# MiniDB disk format v3 (phases 4–5)

Applications v0.4 and v0.5 use **format version 3**. All multibyte fields are unsigned
little-endian integers; all pages are 4096 bytes. No C structure is written
as native memory. Record serialization remains 70 bytes, unchanged from phase 2.

## Compatibility

V3 assigns deletion bits to the previously reserved data-header bytes 8..15 and
an active-row count to metadata bytes 36..39. The record payload and B+ tree node
layouts stay unchanged, but v2 requires those fields to be zero. Keeping version
2 would falsely claim compatibility, so a new format version is mandatory.

V1 and v2 are explicitly rejected without writes. There is no bundled migration
tool and no automatic conversion. Keep old files and use the corresponding older
executable to read them; use a fresh path for v3. Historical references:
[v1](storage-format-v1.md), [v2](storage-format-v2.md). Unknown versions are rejected.

## Metadata page 0

| Offset | Bytes | Field | Meaning |
| --- | --- | --- | --- |
| 0 | 8 | Magic | `MINIDB\r\n` (`4d 49 4e 49 44 42 0d 0a`) |
| 8 | 4 | Version | 3 |
| 12 | 4 | Page size | 4096 |
| 16 | 4 | Total pages | All types, including metadata; must match file size |
| 20 | 4 | Total allocated records | Active + deleted; equals occupied slots and index entries |
| 24 | 4 | Root page ID | Nonzero ID of the one B+ tree root |
| 28 | 4 | Page type | 1 (metadata) |
| 32 | 4 | Last data page ID | 0 only when empty; otherwise the last allocated data page |
| 36 | 4 | Active records | Number of occupied slots whose tombstone bit is zero |
| 40 | 4056 | Reserved | Zero |

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
| 8 | 8 | Tombstone bitmap |
| 16 | 4060 | 58 slots of 70 bytes; unused slots are zero |
| 4076 | 20 | Zero padding |

Tombstone bit `i` lives in byte `8 + i/8`, at bit position `i%8` (least significant
bit first). Zero means active; one means logically deleted. Only occupied slots
may have set bits. Bits from `slot_count` through 63 must be zero, including the
six bits 58..63 that never correspond to a record. The bitmap is read/written by
explicit byte operations; no native integer representation is serialized.

Examples: deleting slot 0 sets byte 8 bit 0; slot 7 sets byte 8 bit 7; slot 8 sets
byte 9 bit 0; slot 57 sets byte 15 bit 1. Record bytes are retained on deletion
and still fully validated, so deletion is neither secure erasure nor space reuse.

Only the last data page may be partial. Other data pages are full of allocated slots, counting deleted records. All data pages
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
bytes and known types. It validates all data records (including deleted payloads),
dense data-page packing, unused bitmap bits and the stored active count against
actual clear bits in occupied slots.
Then it walks the stored tree (it does **not** rebuild it), verifying:

- Capacity/minimum occupancy, sorted unique keys and zero unused bytes.
- Valid, uniquely visited child references; no cycles, aliasing or orphan nodes.
- Exact separator minima and disjoint child ranges.
- Equal leaf depths and correct next-leaf chain, including terminal zero.
- Each index location addresses an occupied data slot with the same ID.
- Indexed entry count equals the allocated data-slot count and metadata total.
- Active count is at most the total and matches the bitmap; deleted = total - active.

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

## UPDATE, DELETE and reinsertion

UPDATE finds the location through the ID index and requires an active row. It
serializes the replacement name/age with the same ID, takes a memory savepoint,
and rewrites only the data slot. Counts, locations and every index page stay
unchanged. Updating a deleted ID returns `DB_DELETED`; a never-inserted ID returns
`DB_NOT_FOUND`. There is no primary-key change operation.

DELETE finds the indexed slot, requires it to be active, and sets its tombstone
bit while decrementing metadata active count in the same memory savepoint. It
never changes the record payload, slot count, index entry, leaf links or tree
shape. Repeated DELETE returns `DB_DELETED` without decrementing again. The entry
continues to participate in tree validation and remains visible in `.btree`.

INSERT on an active ID returns `DB_DUPLICATE_ID`. INSERT on a deleted ID replaces
its record payload, clears its bit and increments the active count, keeping the
same slot and the single index entry. It can succeed at the page limit because
it needs no new page (temporary memory allocation may still fail). INSERT of a
new ID appends a new slot and index entry and increments both total and active
counts. Deleted slots are not recycled for unrelated IDs.

SELECT distinguishes `DB_DELETED` from `DB_NOT_FOUND`, without publishing record
contents in either case. Scans traverse all indexed entries but emit only active
rows. Age filters are applied to those emitted rows in Executor; there is no age
index. Tombstones do not shrink the file or restore capacity for new IDs.

## Normal-error consistency and disk synchronization

All mutations use the existing Pager memory savepoint. INSERT of a new ID may
snapshot data, index and metadata pages; UPDATE snapshots one data page; DELETE
and reactivation snapshot the data and metadata pages. Before-image allocation
failure rolls back bytes/dirty flags. INSERT rollback also removes new pages and
restores root/split state. Counters are published only after all fallible work
succeeds. Previously accepted unflushed changes are preserved.

This mechanism is **not an ACID transaction or WAL**. No sync is allowed during
a savepoint, and disk writes happen only outside it. A successful mutation means
accepted in the cache. `.exit`/EOF closes normally: write all dirty nonmetadata
pages in ID order, then metadata, then file `fsync`; clear dirty flags only after
success. C callers may invoke `storage_sync`. No directory fsync is done.

A failed output callback may leave already delivered rows visible; the structured
Executor result reports the delivered count and error. A terminal error after a
successful mutation does not undo it. Main still attempts to close normally after
a caught REPL error; this is not session rollback.

Write/zero-write/fsync failure poisons the Pager, propagates to the CLI, and blocks
further access or automatic write retries. Closing releases memory and reports
errors. Partial writes can leave mismatched data/index pages or a tombstone bitmap
inconsistent with metadata active count; reopening then rejects the file. No
automatic repair exists. Ctrl-C, crashes, power loss and concurrent access remain
unsupported. Memory savepoints and fsync do not make multi-page disk writes atomic.


## Phase 5 compatibility verification

Phase 5 does not change offsets, version or serialization. The original format
corruption suites remain, and new full-state process/model tests cover normal
close/reopen with UTF-8 payloads, updates, tombstones and reactivation. An ignored
user database was not opened during validation. See [testing](testing.md) for
executed scenarios and [limitations](limitations.md) for remaining failure windows.
