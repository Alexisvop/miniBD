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
