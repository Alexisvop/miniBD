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
