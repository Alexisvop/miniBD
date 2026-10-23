# MiniDB visual guide

MiniDB is an educational single-table database engine, not an Oracle-compatible or production database. These diagrams describe **disk format v3**. For byte-exact offsets and invariants, see [storage-format.md](storage-format.md) and [btree-design.md](btree-design.md).

## Interactive portfolio demonstration

The public [MiniDB showcase](https://minibd.pages.dev/) offers an interactive
architecture explorer, an **educational browser-only B+ Tree simulation**, a
simulated command terminal and a chart of recorded benchmarks. It does **not**
run the C17 engine or write persistent database files. The diagrams below
explain the actual C implementation and its v3 disk layout.

## From command to disk

```mermaid
flowchart TD
    User["Terminal / input commands"] --> REPL["REPL: read input and display results"]
    REPL --> Parser["Parser: validate text and arguments"]
    Parser --> Exec["Executor: dispatch commands and return structured results"]
    Exec --> Storage["Storage: record visibility and consistency"]
    Storage --> Tree["Persistent B+ Tree: ID → (page, slot)"]
    Storage --> Codec["Record codec: 70-byte binary records and tombstones"]
    Tree --> Pager["Pager: 4096-byte page cache, dirty tracking and sync"]
    Codec --> Pager
    Pager --> Disk[("MiniDB .db file")]
```

**Ownership:** Main owns Storage. Storage owns Pager and B+ Tree state; the tree borrows Pager. Only the REPL formats terminal output. The Parser and Executor do not write data to disk themselves.

## Page organization

```mermaid
flowchart TB
    File[("Single .db file, up to 4096 pages")]
    File --> Meta["Page 0: metadata (magic, version v3, counts, root page ID)"]
    File --> Data["Data pages: 16-byte header + up to 58 records × 70 bytes + padding"]
    File --> Leaf["Leaf pages: up to 340 key/location entries"]
    File --> Internal["Internal pages: up to 509 separator keys + 510 children"]
    Data --> Tomb["8-byte tombstone bitmap in page header"]
    Leaf --> Lookup["ID → (data page, record slot)"]
```

Page IDs determine positions in the file (offset = page ID × 4096). **Data and index pages may be interleaved** and page IDs alone do not reveal their type. The diagram groups page *kinds*, not contiguous regions.

## Indexed lookup and ordered scan

```mermaid
flowchart TD
    Select["SELECT id"] --> Internal["Traverse internal separators"]
    Internal --> Leaf["Find ID in sorted leaf entries"]
    Leaf --> Location["Read referenced data page and slot"]
    Location --> Active{"Tombstone set?"}
    Active -- No --> Result["Return active record"]
    Active -- Yes --> Deleted["Report logically deleted"]
    Leaf -. "Leaf-to-leaf links" .-> Next["SELECT_ALL ordered by ID"]
```

The B+ Tree indexes **ID**, not age. `SELECT_ALL WHERE age >= 21` traverses indexed entries and applies the age predicate to active records; it is not an age-indexed point lookup. Deleted IDs remain in leaves and do not reclaim disk space.

## A session that survives a restart

Build once, then run from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j2
sh scripts/demo.sh ./build/minidb
```

The script uses `examples/demo.txt`, opens a fresh temporary database, closes it normally, then reopens the **same file** in a new process and queries its records. It cleans up temporary files. It does not modify any personal database. Closing normally syncs modified pages; **MiniDB provides no WAL or crash recovery guarantee**.

## Validate the implementation

```sh
ctest --test-dir build --output-on-failure
```

A GitHub Actions workflow runs Debug, Release and sanitizer configurations on pull requests and pushes to `main`. See [testing.md](testing.md) for what the 19 suites cover and [limitations.md](limitations.md) for unsupported guarantees.
