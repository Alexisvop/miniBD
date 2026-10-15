#ifndef MINIDB_RECORD_H
#define MINIDB_RECORD_H

#include <stdint.h>

#define MINIDB_NAME_MAX_BYTES 64U
#define MINIDB_AGE_MAX 150U

/* In-memory representation only; this is NOT an on-disk format. */
typedef struct {
    uint32_t id;
    char name[MINIDB_NAME_MAX_BYTES + 1U];
    uint8_t age;
} Record;

#endif
