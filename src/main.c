#define _POSIX_C_SOURCE 200809L
#include "repl.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        int result = printf("Usage: %s database.db\n"
                            "Phase 2: persistent sequential storage; changes are saved at normal close.\n"
                            "Use .help inside the REPL for commands.\n", argv[0]);
        return result < 0 || fflush(stdout) == EOF ? 1 : 0;
    }
    if (argc != 2 || argv[1][0] == '-' || argv[1][0] == '\0') {
        fprintf(stderr, "Usage: %s database.db\n", argv[0]);
        return 1;
    }
    Storage *storage;
    DbStatus status = storage_open(argv[1], &storage);
    if (status != DB_OK) {
        fprintf(stderr, "ERROR: cannot open '%s': %s\n", argv[1], db_status_message(status));
        return 1;
    }
    int result = repl_run(stdin, stdout, stderr,
                          isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0, storage);
    status = storage_close(storage);
    if (status != DB_OK) {
        fprintf(stderr, "ERROR: database close failed: %s; data may be incomplete\n",
                db_status_message(status));
        result = 1;
    }
    if (fflush(stdout) == EOF || fflush(stderr) == EOF) {
        result = 1;
    }
    return result;
}
