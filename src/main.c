#define _POSIX_C_SOURCE 200809L
#include "repl.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        int result = printf("Usage: %s [database.db]\n"
                            "Phase 1: an optional database path is accepted but never opened.\n"
                            "Use .help inside the REPL for commands.\n", argv[0]);
        return result < 0 || fflush(stdout) == EOF ? 1 : 0;
    }
    if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
        fprintf(stderr, "Usage: %s [database.db]\n", argv[0]);
        return 1;
    }
    if (argc == 2) {
        if (fprintf(stderr, "NOTE: phase 1 does not open or create '%s'.\n", argv[1]) < 0) {
            return 1;
        }
    }
    int result = repl_run(stdin, stdout, stderr,
                          isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0);
    if (fflush(stdout) == EOF || fflush(stderr) == EOF) {
        result = 1;
    }
    return result;
}
