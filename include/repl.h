#ifndef MINIDB_REPL_H
#define MINIDB_REPL_H

#include <stdbool.h>
#include <stdio.h>

#define MINIDB_INPUT_MAX_BYTES 4096U

/* Streams are borrowed and remain open. Returns 0 on EOF/.exit, 1 on I/O error.
 * Invalid commands are recoverable and do not change the exit status. */
int repl_run(FILE *input, FILE *output, FILE *errors, bool interactive);

#endif
