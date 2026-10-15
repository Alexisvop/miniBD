#ifndef MINIDB_TEST_SUPPORT_H
#define MINIDB_TEST_SUPPORT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static inline void temporary_path(char path[64])
{
    strcpy(path, "/tmp/minidb-test-XXXXXX");
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    CHECK(close(fd) == 0);
}

#endif
