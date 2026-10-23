# Engineering log

## 2026-10-14 — Phase 1: foundations and CLI

### Objective and initial inspection

Implement only the first milestone of `Proyect_espec.md`: a working command-line
interface and validated parser with reproducible builds and tests. The directory
contained the specification and protected environment directories, with no prior
MiniDB source. `git status` initially reported that no Git repository existed.
The local repository was subsequently initialized on `main`, without commits or
remotes. The original specification was preserved.

Available tools were GCC 16.2.1, CMake 4.4.4, Make 4.4.1, Ninja 1.13.2, and
GDB 18.1 on Arch Linux x86_64 (kernel 7.2.9-arch1-1). Valgrind was unavailable.

### Decisions

- Separate process setup, line reading/interaction, parsing, and record schema.
  Avoid placeholder storage/index modules before their behavior is needed.
- Build `minidb_core` once and link it into the CLI and C tests. Enforce C17 and
  strict warnings, including `-Wall -Wextra -Wpedantic -Werror`.
- Use decimal uint32 IDs, 1–64-byte UTF-8 names, and ages 0–150. Reject empty
  names, malformed UTF-8, ASCII controls, and implicit numeric conversions.
- Keep command syntax case-sensitive, without SQL semicolons or quote escaping.
  These are intentional constraints, documented in `.help` and the README.
- Bound input at 4096 bytes, consume rejected lines completely, and test that
  the following command still works. This prevents truncation from turning one
  invalid line into multiple commands.
- Publish parsed commands only on success. Parser callers retain ownership of
  all memory, making error behavior and future executor integration explicit.
- Accept an optional database path for the future CLI shape, while explicitly
  reporting that phase 1 does not open it. Output says `PARSED`, never inserted
  or retrieved. Duplicate IDs are not checked without storage.

### Verification and issues encountered

1. Debug configuration and compilation completed without warnings. CTest passed
   all three suites: parser, REPL, and CLI.
2. Added deterministic stream failures using POSIX `/dev/null` opened with an
   incompatible mode. The REPL returned failure on read/write errors as expected.
   The updated Debug build again passed all three suites.
3. The first sanitizer run failed because LeakSanitizer could not operate under
   the sandbox's ptrace restrictions. With leak detection disabled, ASan and
   UBSan passed all three suites. A subsequent run outside the sandbox with
   `detect_leaks=1` passed all three suites with no sanitizer findings.
4. GDB initially could not trace a process inside the sandbox. Outside it, a
   breakpoint at `parser_parse` showed the expected input and stack trace, and
   `test_parser` completed normally after continuing.
5. A real piped CLI session recognized INSERT, SELECT, and SELECT_ALL, rejected
   age 151, and explicitly reported that `database.db` was not opened/created.

No measured benchmark, persistence, index integrity, or large-dataset result is
claimed. No Valgrind check was performed. Tests are initial phase 1 coverage,
not proof that all possible input or platform conditions are handled.

### Learning checkpoint and next milestone

This phase establishes the boundary between recognizing a valid command and
executing it. Fixed buffers, explicit numeric bounds, and publish-on-success
parsing make input handling predictable before persistent state is introduced.

Suggested commit: `feat(cli): add phase-one REPL, validated parser, and CTest coverage`.

Stop here until phase 2 is requested. Its first design task is an explicit binary
format: magic/version fields, byte order, record offsets, 4096-byte page layout,
and behavior for empty, truncated, or incompatible files. Then implement the
pager and sequential storage with reopen tests. B+ tree indexing follows in
phase 3; there is no crash-recovery or atomicity guarantee planned without a
transactional mechanism.

## 2026-10-14 — Phase 2: sequential persistent storage

### Inspection and scope

Read `recursos/Proyect_Fase2.md`, the main specification in `recursos/`, the
README, architecture notes and existing modules. The repository was clean on
`main`, tracking `origin/main`, with committed phase 1 code. All three existing
CTest suites passed before edits. Existing user commits, remote configuration,
ignored resources and the original specifications were left intact. No commit
or remote operation was performed during phase 2.

The scope is storage and normal-close persistence only. No B+ tree, UPDATE,
DELETE, transaction, crash recovery or concurrent access was added.

### Design and implementation decisions

- Introduce application v0.2 and independent disk format v1. Use little-endian
  integer encoding and explicit offsets, never native structure writes.
- Reserve page 0 for metadata and store densely packed records in subsequent
  pages. A 16-byte data header plus 58 records of 70 bytes leaves 20 unused bytes.
  The format document specifies every field and all required zero padding.
- Use a length-prefixed 64-byte name payload, so exactly 64 bytes fit on disk.
  Restore a NUL terminator in memory. Move the existing UTF-8 validation into the
  record module so parser and storage APIs enforce the same schema.
- Keep the Pager independent of records and future index node layouts. Use a
  fixed pointer table, separately allocated stable buffers, dirty flags and no
  eviction. Cap the file at 4096 pages (16 MiB) and 237510 records.
- Use POSIX `pread`/`pwrite` loops instead of a shared seek cursor; handle short
  transfers and EINTR. Check file type/size before caching pages. New files use
  restrictive mode 0600; existing contents are never truncated on open.
- Fully validate nonempty files before execution. Check reserved bytes, counts,
  types, records and unique IDs. A temporary sorted ID array makes uniqueness
  validation O(N log N); it is discarded and does not accelerate any query.
- Keep the parser grammar and REPL input reader. Add an executor and pass an
  owned database from main to the borrowing REPL. Require a database path now
  that real files are opened, rather than silently choosing a default location.
- Append records in insertion order. SELECT and duplicate checks are sequential;
  SELECT_ALL is intentionally not sorted by key in this phase.
- Persist the initial empty metadata immediately, then buffer INSERT changes
  until normal close or an explicit C API `storage_sync`. Flush data pages before
  metadata, then fsync the file. A successful INSERT is not a durability promise.
- Latch synchronization failures to block further page access and prevent
  automatic write retries on close. Always free memory and report close failures.
  Failed validation uses discard without any flush. No rollback is implied.

### Verification actually performed

- GCC Debug build passed with the existing strict warnings and `-Werror`.
- All **8/8 CTest suites passed**: parser, repl, record, pager, storage,
  corruption, failures and cli. The parser suite was preserved unchanged;
  REPL/CLI expectations were adapted to execution while keeping input/error
  regression coverage.
- A separate sanitizer build passed **8/8 suites** with AddressSanitizer,
  UndefinedBehaviorSanitizer, leak detection and halt-on-error enabled. No
  sanitizer findings were reported. It ran outside the sandbox, using the
  previously established workaround for LeakSanitizer/ptrace restrictions.
- Binary codec tests compared exact expected bytes and exercised boundaries,
  invalid UTF-8/length/age and nonzero padding. Storage tests inserted 175 records
  across four data pages, reopened and checked every record, then checked minimum
  and maximum IDs and a 64-byte name.
- A valid full-size fixture exercised the actual 4096-page/237510-row boundary:
  the last allowed INSERT succeeded, its duplicate was rejected, one additional
  distinct ID was rejected and reopen recovered the last row. Fixture generation
  deliberately avoided performing a quadratic 237510-INSERT workload.
- Corruption tests modified metadata, page fields, record payloads, padding and
  IDs, and tried partial-page/whole-page truncations and appended pages. Every
  rejected file was compared byte for byte to confirm it was not repaired or
  rewritten. Pager tests also checked a file beyond the configured page limit.
- Deterministic Linux linker wrappers injected allocation failures at each
  opening allocation, append allocation failure, permission denial, read errors,
  short reads/writes, EINTR, ENOSPC after partial progress, zero writes, fsync
  failures and close failures. Checks confirmed error propagation, unchanged
  counts after failed append allocation, clean read-only close, and no writes
  retried after failed synchronization. This avoids relying on the host actually
  running out of disk space or memory. Wrappers exist only in the test executable.
- Two real CLI processes were run against the same temporary database. Session 1
  inserted Alexis (1, age 21) and Carlos (2, age 22) and closed normally. Session 2
  returned Alexis for SELECT 1 and both rows for SELECT_ALL. File size was 8192
  bytes. The automated CLI suite also verified saving via EOF without a final
  newline. Temporary demonstration files were removed afterwards.
- `git diff --check` found no whitespace errors in tracked changes. Valgrind was
  not run; no performance benchmark or crash-recovery result is claimed.

### Files and learning checkpoint

New modules: `encoding.h`, `status.h/.c`, `record.c`, `pager.h/.c`, `storage.h/.c`
and `executor.h/.c`. Updated CMake, record/REPL headers, parser, REPL and main.
New tests cover record, pager, storage, corruption and injected failures, with a
shared temporary-file helper. Updated the existing REPL and CLI tests. Added
`docs/storage-format.md` and updated README/architecture/log documentation.

Study byte order and serialization versus struct layout; page addressing and
slot capacity; cache ownership and dirty-page tracking; system-call return values,
short transfers and EINTR; invariants at untrusted file boundaries; and the
difference between normal-close persistence and atomic/crash-safe transactions.
A successful fsync is not a substitute for WAL or a recovery protocol spanning
multiple modified pages.

Suggested commit:
`feat(storage): add paged persistence, record serialization, and failure tests`.

Stop at phase 2. When requested, phase 3 will design the B+ tree node format,
separator semantics, search, leaf/internal splits and structural validation,
reusing the pager. The phase 2 disk format may require an explicit versioned
migration when those layouts and invariants change.

## 2026-10-14 — Phase 3: persistent secondary B+ tree

### Baseline and design

Read `recursos/Proyect_Fase3.md`, the main specification and existing source,
format, architecture and log. The repository was clean on main, tracking
origin/main. All eight phase 2 suites passed before changes. No commits, remote
operations or edits to the specifications were performed.

Chose a secondary index mapping uint32 IDs to existing data-page/slot locations.
Full records remain in 70-byte slots. Reused the Pager and codec. Format v2 is
required because index pages interleave with data pages and root ID is now active;
metadata also tracks the last data page. V1 is rejected without writes. Migration
was deferred rather than attempting to rewrite a user's file in place. The old
format document is retained as a historical reference.

Leaf capacity is (4096-16)/12 = 340; internal capacity is floor((4096-20)/8) = 509.
No artificially small capacities are used. Separators are exact right-subtree
minima; equality routes right. Leaf links follow sorted tree order. Root identity
is stored once in metadata. Data pages retain their prior layout and validation.

### Implementation and error consistency

Added BTree search, recursive insertion, leaf/internal/root splits, ordered scans,
structural validation and tree dumping. SELECT always uses the tree. SELECT_ALL
now sorts by ID through leaf links, while record pages retain insertion order.
Added .stats and .btree in parser/executor/REPL. Stats separate counts derived
from persistent pages from session-only successful split events.

The Pager gained a nonnested memory savepoint, not a replacement implementation.
Before-images are captured lazily on the first write to an existing page; rollback
restores bytes/dirty flags and removes newly allocated pages. An ID list keeps
commit/rollback work proportional to changed pages. Storage restores BTree root
and counters on failure, and publishes metadata/row counters only after all
fallible insertion steps succeed. Sync inside a savepoint is rejected.

This covers ordinary allocation/capacity failures during split propagation. It
is not disk atomicity or WAL. The normal-close fsync policy, no concurrent access
and poisoned Pager after write/sync failure remain. Existing unflushed successful
inserts survive a later rolled-back insertion.

### Verification actually performed

- Debug and Release compilation succeeded under the strict warning options.
- All **11/11 CTest entries passed** in Debug and in an ASan/UBSan build with
  leak detection and halt-on-error enabled, with no findings. LeakSanitizer ran
  outside the sandbox to avoid its previously established ptrace restriction.
- Preserved the eight suites, updating legitimate version/layout/capacity/order
  and .stats expectations. Added btree, btree_large and btree_corruption entries.
  Binary-format fixtures necessarily changed from v1; legacy rejection is now
  tested separately using a genuine empty v1 metadata page.
- Inserted 10000 and 100000 rows in ascending, descending and deterministically
  shuffled order. Compared every successful lookup, missing odd key and full
  sorted traversal against a simple independent model before and after reopen.
  Continued insertion after reopen, including UINT32_MAX, was also checked.
- At 100000 rows, ascending order produced height 3 / 588 leaves / 3 internal
  nodes, with 587 leaf, 1 internal and 2 root split events. Descending produced
  height 3 / 584 leaves / 3 internal nodes. Random order produced height 2 /
  449 leaves / 1 internal node. Thus actual internal overflow and root growth
  were exercised, even though the random case alone did not reach that depth.
- Indexed INSERT capacity testing reached 176900 ascending records before the
  next insertion hit the total page budget; the rejected append preserved all
  invariants and reopen succeeded. The previous 237510 figure remains only a
  data-only upper bound because index pages now consume the same file budget.
- Injected failure at each allocation of inserts following 0, 340, 348 and 86870
  rows: 3, 5, 3 and 7 failure positions respectively. These exercise new data pages,
  leaf/root splits and internal/root propagation. Each failed insertion restored
  counts/root/session metrics and left the closed file byte-for-byte unchanged.
- Added a follow-up regression for restoring dirty flags after earlier unflushed
  inserts; the updated failures suite passed again in Debug and with sanitizers.
- Corrupted a genuine three-level tree to test cycles, aliased/out-of-range
  references, bad separators, unequal depths, occupancy, leaf chains, record
  locations and orphan pages. Rejected files remained unchanged. Prior record,
  header, truncation, I/O and memory-failure regressions continue to pass.
- Executed two CLI processes: insert IDs 30,10,20, observe sorted results 10,20,30,
  inspect .stats/.btree, close, reopen and recover the same rows/tree. The dump
  showed 10->(2,1), 20->(2,2), 30->(2,0), confirming that records retain insertion
  order while leaf keys sort. Session split counters reset on reopen.
- Valgrind was not run. No crash recovery, concurrency or deletion claim is made.

### Benchmark

Added a standalone benchmark target excluded from CTest. It compares indexed
lookup with a benchmark-only phase 2 sequential scan adapted to mixed-page v2
files. Both caches are fully populated, both methods use the same 2000 queries
(75% hits), each is warmed up, five timed trials alternate method order, and
result checksums agree. Creation/open/validation/fsync are excluded from timing.

Executed on Arch Linux x86_64, kernel 7.2.9-arch1-1, Intel i5-1235U, GCC 16.2.1,
Release -O3 without sanitizers. Median indexed/sequential batch milliseconds:
1000 rows: 0.170638 / 0.675523; 10000: 0.233652 / 7.107775;
100000: 0.571701 / 156.828272. Conditions, limitations and the exact CSV are in
benchmarks/. These are cached CPU measurements, not disk-latency or production
throughput results. The machine was not CPU-pinned or frequency-isolated.

### Files, study and next step

New: include/btree.h, src/btree.c, tests/test_btree.c,
tests/test_btree_corruption.c, benchmarks/lookup.c, benchmark documentation/CSV,
docs/btree-design.md and the historical v1 format reference. Updated Pager,
Storage, parser/executor/REPL interfaces, version/diagnostics, CMake, existing
tests, README, architecture, storage format and this log.

Study fanout versus height, upper/lower bounds, exact separator semantics,
leaf versus internal split promotion, secondary-index record references,
leaf-chain scans, tree/data coverage invariants and the difference between
in-memory rollback and durable crash-safe transactions.

Suggested commit:
`feat(index): add persistent B+ tree, rollback, validation, and lookup benchmark`.

Stop at phase 3. Await the user's phase 4 instructions; do not implement updates,
deletes or deletion rebalancing automatically.

## 2026-10-15 — Phase 4: updates, logical deletion and filtered execution

### Inspection and design

The first attempt found `recursos/Proyect_Fase4.md` empty and stopped after
verifying all 11 existing tests. After the user saved its contents, read the full
specification, current interfaces and code, format and architecture documents.
The workspace already contained the uncommitted phase 3 implementation; those
changes were retained. No commits, remote operations or modifications to user
databases were performed.

Selected a per-data-page tombstone bitmap in former reserved bytes 8..15. Each
slot has one bit, least significant bit first; only bits for occupied slots may
be set. This preserves 70-byte records, 58 slots/page and all tree capacities.
Metadata offset 36 stores active count separately from total slots at offset 20.
Because v2 requires these bytes to be zero, bumped the format to v3 instead of
silently accepting incompatible data. V1/v2 are rejected unchanged. Preserved
`docs/storage-format-v2.md` as a historical reference; migration remains absent.

### Implementation

- UPDATE uses the B+ tree to find an active row and rewrites name/age in the same
  slot and ID. Only the data page is dirtied. Missing/deleted IDs have distinct
  controlled statuses, and invalid payloads are rejected before mutation.
- DELETE sets the slot bit and decrements active count. Payload, total count,
  index key/location and tree links remain. Repeated deletion returns DB_DELETED.
  SELECT reports deleted state without exposing payload; ordered scans omit it.
- INSERT of the same deleted ID replaces its payload and clears its bit, using
  the existing slot and single index entry. This works even at the page limit.
  Active duplicates remain errors; tombstones are not reused for unrelated IDs.
- All mutations use the existing in-memory savepoint. UPDATE needs one page
  before-image; DELETE/reactivation need data and metadata before-images. Failure
  restores dirty flags/bytes and leaves scalar counts unpublished. No B+ tree
  structural changes occur for these operations.
- Added WHERE age predicates with =, !=, <, <=, >, >= and numeric range 0..150.
  The parser requires separate whitespace-delimited tokens and rejects malformed
  operators/extra expressions. Executor applies the predicate to active rows
  delivered by the ID-ordered scan. There is no age index or optimizer.
- Refactored Executor to return ExecutionResult (DbStatus, row count, stats) and
  use optional row/node callbacks. It performs no terminal output. BTree exposes
  a borrowed BTreeNodeView visitor; Storage forwards inspection without printing.
  REPL handles all formatting and reports zero matches as OK: 0 rows.
- .stats now separates total allocated records, active/deleted rows, total index
  entries and data/index pages. .btree still displays retained deleted keys.
- Open validates bitmap boundaries, active counters and payloads including
  tombstones, while preserving all prior tree/data invariants. The record codec
  itself is unchanged because deletion state resides outside the record payload.

### Verification actually executed

- The 11 phase 3 tests passed before changes and after initial integration.
- Added queries, mixed, tombstones and cli_queries, retaining the original 11.
  The final Debug run passed **15/15 tests** with strict compiler warnings.
- The sanitizer configuration passed **15/15 tests** with ASan, UBSan,
  detect_leaks=1 and halt-on-error enabled, with no findings. The run used the
  established execution-outside-sandbox workaround for LeakSanitizer/ptrace.
- Release compilation also succeeded, including the standalone benchmark target.
  No phase 4 benchmark run/measurement or Valgrind result is claimed.
- Queries tested UPDATE existing/missing/deleted IDs, invalid payloads, repeated
  deletion, reinsertion, empty scans, every age operator, numeric bounds,
  malformed commands, no-terminal structured execution, callback errors and
  close/reopen behavior. IDs 0 and UINT32_MAX and ages 0 and 150 were exercised.
- Mixed tests executed **30000 operations over 3000 IDs** against an independent
  model, checking values, deleted/missing states, row counts, all filter operators,
  sorted results and tree invariants. Six reopen cycles rechecked the model.
- Tombstone tests exercised slots 0,7,8,31,32,56,57 and compared every index page
  byte-for-byte before/after UPDATE/DELETE/reactivation. Index pages, page counts
  and split counters stayed unchanged. Bitmap/count corruption, unused/high bits,
  invalid deleted payloads and a genuine v2 image were rejected without writes.
- Allocation failures were injected at each UPDATE/DELETE/reactivation snapshot;
  after rejection and close, file bytes matched the baseline. New-operation
  write/fsync failures propagated and poisoned the Pager, preventing retries.
  Earlier split/memory/I/O regressions remain. Capacity tests now also delete and
  reactivate the same ID at the page limit without adding an entry or slot.
- Separate CLI processes demonstrated persistence: session 1 inserted IDs 3,1,2,
  updated 1 to Alexis Perez/22, deleted 2 and filtered age>=21, returning IDs 1,3.
  .stats reported total=3, active=2, deleted=1, index_entries=3. Session 2 retrieved
  the updated row, reported ID 2 as deleted, reactivated it as Carlos returned/23,
  and filtered age<=23, returning IDs 1,2. Stats became active=3, deleted=0 with
  the same three entries/pages. CLI tests also confirmed reactivation saved at EOF.
- Checked that Executor/Storage/BTree contain no terminal output calls. Source
  formatting checks were run; no whitespace issues were found.

### Limitations and learning checkpoint

Logical deletion neither recovers space nor erases bytes. Tree occupancy includes
historical deleted IDs. Age predicates scan the entire ID index and skip deleted
rows, so high tombstone counts still cost work. No physical deletion, merge,
redistribution, compaction or new-ID slot reuse exists.

Successful mutations are accepted in memory; file writes happen at normal close
or explicit sync. Partial writes can leave a bitmap inconsistent with its active
counter. Memory rollback and fsync do not provide atomic crash recovery, WAL,
transactions or concurrent access. Output errors after accepted mutations do not
roll them back. Open-time corruption checks cannot detect every consistent bit
change without checksums. Old files require an older executable; migration is not
included. Phase 3 benchmark data remain historical, not phase 4 claims.

Study logical versus physical deletion, visibility versus structural index
membership, stable record references, active versus allocated row counts, scan
predicates versus indexed search, callback ownership and streaming errors,
structured execution results and memory rollback versus durable transactions.

New files: tests/test_queries.c, tests/test_mixed.c, tests/test_tombstones.c,
tests/test_cli_queries.cmake and docs/storage-format-v2.md. Updated Storage,
Executor, parser, status, REPL/main, structured BTree inspection interfaces,
CMake/version, applicable existing tests and the required documentation. Pager
and record serialization algorithms were reused without phase 4 changes.

Suggested commit:
`feat(query): add updates, tombstones, age filters, and structured execution results`.

Stop after phase 4. Phase 5 requires a new user instruction.

## Phase 5 — final engineering validation (2026-10-15)

### Starting state and audit

Started from clean commit `1766422` (`Act, Del and queries`). Read the main and
phase 5 specifications, all production modules/headers, CMake, format/design
notes and existing tests/benchmarks. Ran the original suite first: 15/15 Debug
entries passed in 8.51 s. The ignored `indexed.db` was left untouched. Findings
and severity are consolidated in [audit.md](audit.md).

No engine correctness defect requiring an algorithm change was found in the
reviewed paths. Preserve Storage, Pager, codec, Parser, Executor and B+ tree
algorithms and disk v3. Update application/CMake labels to v0.5. Avoid a large
refactor or speculative performance work in a validation milestone.

### Implemented evidence

Preserve all 15 entries and add four: input_stress, tree_mutations, mixed_stress,
process_model. Original mixed mode retains 30000 operations/3000 IDs; the new
mode runs 60000/10000 with 12 close/reopen cycles. Existing tree scenarios still
cover 10000 and 100000 rows in ascending, descending and shuffled order; all
successful/missing point lookups and complete ordered payloads are compared.

The new height-3 mutation scenario starts with 100000 rows and checks 33334
logical deletes, 13333 non-key updates and 11112 reactivations. Complete point and
scan results match a formula model before and after reopening; pages and tree
node counts remain unchanged. The Python dictionary oracle uses ten fresh engine
processes: 10000 initial UTF-8 rows, four 5000-operation mutation sessions and
five complete-state snapshots including all six predicates. A real read-only
file-mode denial probe passed under the ordinary user. Input stress adds 50000
seeded parser mutations and 50000 codec mutations, with failure-output preservation
and successful canonical round trips. These bounded experiments are not exhaustive
fuzzing or proof of correctness.

### Builds and tools actually run

- Debug: 19/19 CTest entries passed, 10.70 s.
- Release: 19/19 passed, 5.53 s; both standalone benchmarks compiled.
- ASan/UBSan/LeakSanitizer: 19/19 passed, 34.22 s, no reported findings. Executed
  outside the tool sandbox because ptrace restrictions prevent leak detection.
- Clean source-only copy in a temporary directory: fresh Debug/Release builds and
  19/19 Release CTest passed; no prior build tree, Git data, local specification,
  user DB or precompiled artifact was present. Copy/build trees were removed.
- Strict compiler warnings remain enabled; checks are active with NDEBUG.
- Valgrind and perf were unavailable. GNU gprof 2.47 was available and used.

The 19 entries contain many assertions/scenarios; this is not an assertion count.
Stress test runtime and memory fit the available host. The capacity suite
intentionally reaches the configured 4096-page bound and checks controlled failure.

### Benchmarks and profiling

Added benchmarks/engine.c and summarize.py, independent of CTest. Four sizes
(1000/10000/50000/100000), one complete warm-up trial then five measured trials,
11 operations per size: 220 raw samples, 44 summary rows. Exact query comparisons,
reference checksums, full mutation-model verification and final reopen checks
validate results. Comparable lookup and scan/filter ordering alternates; cache
state, units, seeds, batch sizes, timing boundaries and dispersion are explicit.

On this host /tmp is tmpfs. Thus pwrite/fsync timings are real filesystem calls
to memory, not SSD latency or a durable commit benchmark. The cached 100000-row
lookup batch (2000 identical queries) measured medians 0.917055 ms indexed versus
287.190818 ms sequential, with MAD 0.018260 and 4.461099 ms respectively. Preserve
all sizes/operations in the raw CSV and summary rather than reporting only the
favorable ratio. Historical phase 3 numbers remain intact and are not treated
as a directly comparable regression baseline.

The separate -pg profile attributes 60.25% sampled self time to benchmark-only
sequential lookup, 19.25% to record_deserialize, then scan/search/UTF-8 work.
The whole-harness profile includes validation and warm-up; it is not equivalent
to timed engine-only latency. No production microoptimization was justified or
applied. See [benchmarking.md](benchmarking.md) and the preserved flat profile.

### Deliverables and handoff

Rewrote README for an external reviewer with architecture, usage/build/tests,
benchmarks, decisions, limitations, future work and license status. Added
[testing.md](testing.md), [benchmarking.md](benchmarking.md),
[limitations.md](limitations.md), [audit.md](audit.md), examples/demo.txt, raw and
summary measurements and a profile. Updated architecture, format and tree notes;
no format migration or major feature was added. Python is test/analysis-only,
required when BUILD_TESTING is enabled; the engine has no Python dependency.

All phase 5 mandatory acceptance criteria are supported by the executed evidence.
The optional unavailable tools are explicitly reported. No CI service, remote
configuration, license selection, repository publication, CV or application was
created. No commits were made. The project is prepared for external technical
review as an educational portfolio project, not production deployment. The owner
should understand index invariants, portable encoding, tombstone semantics,
cache-versus-durability guarantees and benchmark methodology before presenting it.
Stop at phase 5 and await the owner's review.

Suggested commit:
`test: complete phase 5 validation, reproducible benchmarks, and portfolio docs`

## 2026-10-23 — GitHub release, security review and portfolio showcase

This is a **post-Phase-5 release note**, not a retroactive change to the original
2026-10-15 engineering evidence. Since Phase 5, the owner merged the public
repository improvements, MIT license, GitHub Actions workflows and a static
recruiter website. The C engine is still application **v0.5** with disk
**format v3**; no new persistent-storage capabilities or ACID claims were added.

- GitHub repository: [Alexisvop/miniBD](https://github.com/Alexisvop/miniBD).
- Public portfolio website (owner-confirmed):
  [minibd.pages.dev](https://minibd.pages.dev/).
- Engine CI: Linux Debug, Release and ASan/UBSan, 19/19 CTest entries per job on
  main merge commit `1216f86f1633075b37daa8f891fa9340a555793e`.
- Website CI: reproducible npm install, npm dependency audit, three educational
  tree tests, TypeScript and Vite build checks on the same commit.
- The interactive tree and command terminal are **client-side educational
  simulations**, not the C engine running in the browser. Benchmark values
  originate from the saved Phase 5 CSV and retain warm-cache qualifications.

A current repository documentation pass cross-checked the tracked files and
all Markdown relative links, corrected a stale statement in the archived
benchmark overview, and updated the live deployment/CI/security references.
See [documentation status](documentation-status.md) for the review's boundaries.
Older versioned storage files and dated phase reports remain intentionally
historical and were not rewritten to describe v3.
