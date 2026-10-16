> Historical phase 2 format. The current executable rejects v1 files without modification.

# MiniDB storage format, version 1

This is the phase 2 sequential heap format, introduced with MiniDB v0.2. The
application version and disk format version are independent. The format does
not serialize C structures, pointers, padding, or native-endian integers.

## Conventions and capacity

- A byte is an octet; all multibyte integers are unsigned **little-endian**.
- Each page is exactly **4096 bytes**. Page ID `p` starts at file offset `p * 4096`.
- Page 0 contains metadata. Data pages have IDs 1 through `page_count - 1`.
- Maximum total pages: **4096**, including metadata (16 MiB file size).
- A data page holds **58 records**; maximum rows: **237510**.
- All reserved and unused bytes must be zero in version 1.
- No checksums, deleted slots, free list, or B+ tree exist in this version.

```text
file offset 0          4096                8192
            +----------+-------------------+-------------------+ ...
            | metadata | data page 1       | data page 2       |
            | page 0   | header + 58 slots | header + 58 slots |
            +----------+-------------------+-------------------+
```

## Metadata page (page ID 0)

Offsets in this table are relative to the beginning of page 0.

| Offset | Bytes | Field | Required value / interpretation |
| --- | --- | --- | --- |
| 0 | 8 | Magic | ASCII `MINIDB` followed by CR and LF: `4d 49 4e 49 44 42 0d 0a` |
| 8 | 4 | Format version | `1` |
| 12 | 4 | Page size | `4096` |
| 16 | 4 | Total page count | Includes metadata; must equal file size / 4096 |
| 20 | 4 | Total record count | `0..237510` |
| 24 | 4 | Reserved root page ID | `0` means no index; any other value is invalid in v1 |
| 28 | 4 | Page type | `1` (metadata) |
| 32 | 4064 | Reserved | All zero |

An empty database has one metadata page, record count 0, and file size 4096.
The reserved root field does not imply that a tree exists. A future indexed
format may require a new version and migration; backwards compatibility with a
future format is not promised.

## Data pages

Offsets are relative to the beginning of each data page.

| Offset | Bytes | Field | Required value / interpretation |
| --- | --- | --- | --- |
| 0 | 4 | Page type | `2` (sequential records) |
| 4 | 4 | Occupied slots | `1..58` |
| 8 | 8 | Reserved | All zero |
| 16 | 4060 | 58 record slots | 70 bytes each; unused slots are zero |
| 4076 | 20 | Tail padding | All zero |

Records are appended to the last page. A new page is allocated only when needed.
Every data page except the last must be full. There are no empty data pages or
holes, and no deletion operation. These invariants imply:

```text
page_count = 1 + ceil(record_count / 58)
record_offset(page, slot) = page * 4096 + 16 + slot * 70
```

`SELECT_ALL` visits page IDs in increasing order and slots in increasing order,
which is **insertion order**, not key order.

## Serialized record (70 bytes)

| Offset | Bytes | Field | Interpretation |
| --- | --- | --- | --- |
| 0 | 4 | ID | Little-endian uint32; both 0 and 4294967295 are valid |
| 4 | 1 | Age | `0..150` |
| 5 | 1 | Name byte length | `1..64` |
| 6 | 64 | Name payload | First `length` bytes are UTF-8; remaining bytes are zero |

The name has no required NUL terminator on disk; an exactly 64-byte name fills
its payload. Deserialization adds the terminator in `Record.name[65]`.
Embedded NUL, ASCII controls, DEL, double quotes, malformed UTF-8, overlong
encodings, surrogate code points, and values beyond U+10FFFF are rejected.
Names are not normalized and their length counts bytes, not characters.

For `{id = 1, name = "Alexis", age = 21}`, the first 12 bytes are:

```text
01 00 00 00 15 06 41 6c 65 78 69 73
```

The remaining 58 record bytes are zero. `record_serialize` checks validity before
writing output. `record_deserialize` validates and constructs a local record,
then publishes it only on success. Neither function writes `sizeof(Record)`.

## Opening and validating a database

The pager uses `open` with read/write access and creation if missing, without
truncation. New files have mode 0600 before the process umask. A regular file is
required; directories/devices/FIFOs are not database storage. `O_NONBLOCK`
prevents a FIFO path from blocking before it can be rejected. No lock is taken.

A missing file is created. A zero-byte regular file is treated as an empty new
database and receives a synchronized metadata page. A nonempty file is never
reinitialized or automatically repaired. Failure during creation can leave an
empty or partial file; it is not automatically removed.

For existing nonempty files, opening checks:

1. The file is page-aligned and within the page limit.
2. Magic, version, page size, total pages, record count, root field, and metadata
   type are supported and consistent. Unknown versions have a distinct error.
3. All data pages have the expected type, slot count, and zero reserved/padding
   bytes. Every record is valid, including its UTF-8 and name padding.
4. Record IDs are unique. A temporary array of IDs is sorted to detect duplicate
   corruption, then freed. It is not retained or used as a query index.

The read path handles short reads and interrupted system calls; premature EOF
is a truncated-format error. A failed open discards cache memory without writing
any pages. Tests compare rejected file contents byte for byte after opening.

Validation detects structural problems but cannot detect every bit flip. A bit
flip changing one valid value to another may remain undetected without checksums.
External modifications while the database is open are unsupported; the cache is
not coherent across processes and the open-time validation is not repeated on
every command.

## Cache and synchronization policy

The pager stores individually allocated 4096-byte buffers with a dirty flag and
an array of page pointers. `pager_get` reads an uncached page; `pager_write`
marks it dirty; `pager_allocate` appends a zeroed dirty page. Borrowed buffer
addresses remain stable until the pager is closed or discarded.

There is no eviction. Although the pager loads pages on demand, opening a
nonempty database validates all records and thus loads every page. Memory for
page payloads is bounded at 16 MiB, plus page/cache bookkeeping and a temporary
ID array of up to 950040 bytes during validation (excluding library allocator
and sanitizer overhead).

New database metadata is synchronized on initialization. INSERT modifies cache
pages only: `OK: 1 row inserted` means the row is accepted in this session, not
that it has reached durable storage. Later SELECT commands see it immediately.

At `.exit` or EOF, `main` calls `storage_close`:

1. Write dirty data pages using complete `pwrite` loops.
2. Write dirty metadata page 0.
3. Call `fsync` on the file, retrying EINTR.
4. Clear dirty flags only after successful synchronization.
5. Close the descriptor and free all cache and storage allocations.

`storage_sync` exposes the same synchronization to C callers; there is no `.sync`
command. Closing an unmodified database performs no writes or extra fsync.
After a caught REPL error, main still attempts to close the database and save
previously accepted changes; there is no rollback of the session.

A write, zero-byte write, or fsync failure poisons the pager. Further page access
or synchronization fails, and close frees resources **without retrying writes**.
The failure reaches main and yields exit status 1. The file may be partially
written and subsequent opening may reject it. Close errors are also reported;
close is not retried, to avoid accidentally closing a reused descriptor.

**There is no crash atomicity.** Writing metadata last does not make INSERT or
close transactional. Ctrl-C, termination, crashes, disk failures, or power loss
can lose accepted records or leave inconsistent pages. There is no WAL, recovery,
backup rollback, directory fsync, or multi-process access guarantee. Use `.exit`
or EOF and check the exit status for normal-close persistence.
