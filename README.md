# MiniDB

An educational relational database engine being built from scratch in C17.
The project explores systems programming and database internals as preparation
for database engine engineering internships, including Oracle MDC. It does not
use SQLite or another database backend and is not a production database.

**Current milestone: phase 1 — CLI and parser.** No records are stored, no
database file is opened, and no queries are executed yet. The later milestones
in [the specification](Proyect_espec.md) require separate implementation steps.

## Implemented

- Interactive REPL with `.help`, `.exit`, and clean end-of-file handling.
- Parsing of `INSERT`, `SELECT`, and `SELECT_ALL` into typed commands.
- Unsigned 32-bit IDs, validated UTF-8 names, and bounded ages.
- Recovery after malformed commands, oversized lines, and embedded NUL bytes.
- Explicit stream error handling and diagnostics on standard error.
- C17 build with strict warnings, CTest tests, and optional ASan/UBSan.

## Build on Arch Linux

Required tools: GCC, CMake 3.16 or newer, and Ninja (or Make). GDB is optional.
There are no third-party runtime libraries; the CLI uses the C library and
POSIX `isatty` to detect interactive terminals.

Install missing development tools if needed:

```sh
sudo pacman -S --needed base-devel cmake ninja gdb
```

From this directory (commands also work in Fish):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/minidb
```

The executable runs directly from the build directory; no system installation
is needed. To use Make instead, choose `-G "Unix Makefiles"` in a separate build
directory. The warning flags target GCC and Clang on POSIX systems.

## Usage

```text
minidb> INSERT 1 "Alexis" 21
PARSED: INSERT 1 | Alexis | 21 (storage not implemented)
minidb> SELECT 1
PARSED: SELECT 1 (storage not implemented)
minidb> SELECT_ALL
PARSED: SELECT_ALL (storage not implemented)
minidb> .help
...
minidb> .exit
```

`./build/minidb database.db` accepts the future database path and explicitly
reports that the path is not opened or created in this phase. `--help` shows
command-line usage. There are no persistence or duplicate-key checks yet.

The language is deliberately small and is **not SQL**:

| Input | Rules |
| --- | --- |
| `INSERT <id> "<name>" <age>` | All three fields are required. |
| `SELECT <id>` | Exactly one ID is required. |
| `SELECT_ALL` | No arguments. |
| `.help`, `.exit` | No arguments. |
| ID | Decimal digits only, from 0 to 4294967295; leading zeroes are accepted. |
| Name | 1–64 UTF-8 bytes excluding the terminator, enclosed in double quotes. Spaces are preserved. |
| Age | Decimal digits only, from 0 to 150 inclusive. |

Commands are case-sensitive. Whitespace separates tokens; leading/trailing
whitespace and blank lines are accepted. Names cannot contain ASCII controls,
DEL, or double quotes. There is no escape syntax; a backslash is a literal
character. Invalid UTF-8, signed numbers, decimals, extra arguments, and SQL
semicolons are rejected. The 64-byte name limit counts bytes, not characters:
32 repetitions of `é` occupy 64 bytes.

Each input line can contain at most 4096 bytes before LF (CR counts toward
that limit). Longer lines and lines containing NUL are discarded in full.
EOF also terminates a final command without a newline. Ctrl-D at an empty
terminal prompt ends the session. Ctrl-C retains the operating system default.

Piped input suppresses the banner and prompt:

```sh
printf 'INSERT 1 "Alexis" 21\nSELECT 1\n.exit\n' | ./build/minidb
```

Invalid commands print `ERROR` and allow the session to continue. Exit status
is 0 for `.exit`/EOF, including sessions with invalid commands; it is 1 for
invalid process arguments or detected I/O failures. This is not a batch query
runner with transaction or fail-fast semantics.

## Architecture

```text
main (arguments and terminal detection)
  -> REPL (bounded input, diagnostics, help, exit, parse preview)
       -> parser (text -> Command + ParseStatus)
            -> Record (in-memory fields only)
```

The parser performs no I/O or allocation. The REPL borrows its streams, which
also allows integration tests to supply temporary files. There is no executor,
pager, storage engine, or B+ tree yet. See
[architecture and ownership](docs/architecture.md) and the
[engineering log](docs/engineering-log.md) for the rationale and next steps.

## Tests and sanitizers

CTest runs three suites:

- `parser`: command types and values, missing/extra arguments, numeric overflow,
  64/65-byte names, multibyte UTF-8 boundaries, invalid encodings, and preservation
  of the output object on errors.
- `repl`: help/exit, recovery, CRLF, EOF, interactive prompts, exact input limit,
  oversized input, embedded NUL, and input/output stream errors.
- `cli`: real executable session, argument validation, `--help`, and confirmation
  that the supplied database path is not created.

Checks stay active in Release builds; tests do not rely on C `assert`.

```sh
cmake -S . -B build-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMINIDB_SANITIZERS=ON
cmake --build build-sanitize
env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize --output-on-failure
```

Verified on 2026-10-14 with GCC 16.2.1, CMake 4.4.4, and Ninja 1.13.2 on
Arch Linux x86_64: **3/3 suites passed in Debug and with ASan/UBSan and leak
detection enabled**, with no sanitizer findings in those runs. LeakSanitizer
initially failed inside the execution sandbox due to its ptrace restrictions;
the successful leak check ran outside that sandbox. In restricted environments,
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` retains address checks but does
not verify leaks. Valgrind was unavailable and was not run.

These tests validate phase 1 only. Persistence, B+ tree invariants, 100,000-row
workloads, and benchmarks have not been implemented or measured.

## Debugging

Build with `-DCMAKE_BUILD_TYPE=Debug`, then:

```sh
gdb --args ./build/minidb database.db
```

At the GDB prompt:

```text
break parser_parse
run
```

Enter `INSERT 1 "Alexis" 21` at the MiniDB prompt. At the breakpoint, use
`print input`, `next`, `bt`, `finish`, and `continue` to inspect parsing and
return to the REPL. `print *output` is meaningful after a successful parse,
not before it. GDB 18.1 was verified with a breakpoint and stack trace in the
parser test, followed by normal completion; ptrace required running outside
the sandbox. Debug the normal build rather than combining GDB with LSan.

VS Code can use its integrated terminal for these commands. With the optional
C/C++ debugging extension, select `build/minidb` as the program, `gdb` as the
debugger, and the workspace as the working directory.

## Roadmap and limitations

1. **Phase 1 (implemented):** CLI, parser, validation, and initial tests.
2. **Phase 2 (next):** explicit binary serialization, versioned file metadata,
   a 4096-byte pager, sequential retrieval, and persistence tests.
3. **Phase 3:** persistent B+ tree with node splits and invariant checks.
4. **Phase 4:** indexed execution, updates, documented deletion strategy, and
   structural inspection commands.
5. **Phase 5:** expanded verification, reproducible benchmarks, and quality work.

No storage format is defined in phase 1. `Record` is an in-memory structure and
must never be written directly as a serialized record. The future format will
specify offsets, endianness, metadata validation, and synchronization policy.
Likewise, B+ tree layout and algorithms will be documented when implemented.

The eventual scope is one fixed-schema table, without full SQL, joins,
concurrent writers, ACID transactions, WAL, or crash recovery. There are no
durability or power-loss guarantees. No third-party source code is vendored.

Git is initialized locally. Build artifacts and test databases are ignored;
no commits or remotes were created. Suggested first commit after review:
`feat(cli): add phase-one REPL, validated parser, and CTest coverage`.
