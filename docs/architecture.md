# Phase 1 architecture

## Data flow and responsibilities

`main.c` validates process arguments, accepts an optional future database path,
detects whether both standard input and output are terminals, and delegates
to `repl_run`. Only terminal detection uses POSIX (`isatty`); all input parsing
and line reading use C17 library facilities.

`repl.c` consumes one complete line at a time, invokes the parser, reports
diagnostics, and dispatches `.help`, `.exit`, or a parse preview. Its private
`read_line` function uses buffered `fgetc`: it bounds memory while still draining
invalid lines. A single `fgets` call would require additional logic to avoid
executing the remainder of an oversized command. NUL bytes are rejected before
the parser receives a C string. Input errors are distinct from ordinary EOF.

`parser.c` converts a NUL-terminated string to a `Command`. It recognizes exact
tokens, consumes arguments in order, and requires the end of the line after the
last argument. It accepts blank lines without dispatching anything. Parser error
codes separate unknown commands, malformed syntax, invalid names, and numeric
range errors from successful commands.

`record.h` declares a fixed in-memory schema. Names have 65 bytes of capacity:
64 payload bytes plus the C terminator. Age is a `uint8_t`, but parsing enforces
the narrower educational range 0–150 before conversion. No disk layout follows
from the layout or size of this structure.

## Important functions and invariants

- `parser_parse`: constructs a local zero-initialized command and publishes it
  only after all validation succeeds. Failed parsing leaves the caller's output
  unchanged; callers must inspect the status before using a command.
- `parse_number`: accepts only decimal digits and checks the next multiplication
  and addition before performing them. It is private and used with limits
  `UINT32_MAX` and 150 (both at least 9, as required by its subtraction check).
  Negative signs, overflow, and non-whitespace suffixes are rejected.
- `parse_name`: locates the closing quote, checks the byte count, validates UTF-8,
  and then copies the payload with an explicit NUL terminator.
- `valid_utf8`: checks continuation bytes, minimum encoding lengths, Unicode
  surrogate exclusions, and the U+10FFFF upper bound. It does not normalize text
  or calculate display width. ASCII control characters and DEL are rejected.
- `read_line`: stores at most 4096 bytes and always terminates its buffer. An
  invalid line is consumed through LF or EOF without executing any prefix/suffix.
- `repl_run`: blank/invalid lines are recoverable; input/output errors end the
  session with a nonzero result. Output is flushed before reading the next line.

## Ownership and complexity

The caller owns `Command` and input strings. No parser pointers escape to the
command; names are copied. `repl_run` borrows streams and does not close them.
Its line buffer and command live on the stack. Production code has no explicit
heap allocations or mutable global state. The C stream implementation may
allocate its own buffers; temporary test streams are closed by the tests.

Parsing is linear in input length; input reading is linear even for rejected
oversized lines. Memory for a session is bounded independently of line length.
There is no execution complexity or index search complexity to report yet.

## Alternatives and extension points

A hand-written cursor parser keeps delimiters and errors visible without adding
a lexer generator or runtime dependency. Unlike `atoi`, the numeric helper can
distinguish malformed input and overflow. Unlike unbounded line allocation, the
bounded reader has a predictable memory limit.

Meta-commands share the parser so they get the same whitespace and trailing-token
validation. The small dispatch switch remains in the REPL because phase 1 has no
execution behavior to abstract. Phase 2 will add storage and an executor; command
previews can then be replaced with calls returning structured execution results.
Storage, pager, and B+ tree modules are intentionally deferred until their phase.
