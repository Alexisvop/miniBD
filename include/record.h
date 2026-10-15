#ifndef MINIDB_RECORD_H
#define MINIDB_RECORD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MINIDB_NAME_MAX_BYTES 64U
#define MINIDB_AGE_MAX 150U
#define MINIDB_RECORD_SIZE 70U

/* In-memory representation only; this is NOT an on-disk format. */
typedef struct {
    uint32_t id;
    char name[MINIDB_NAME_MAX_BYTES + 1U];
    uint8_t age;
} Record;

/* Shared by the parser and the disk codec. Length excludes the terminator. */
bool record_name_valid(const char *name, size_t length);
bool record_valid(const Record *record);
/* Fixed-size buffers are caller-owned. Failure leaves output unchanged. */
bool record_serialize(const Record *record, uint8_t output[MINIDB_RECORD_SIZE]);
bool record_deserialize(const uint8_t input[MINIDB_RECORD_SIZE], Record *output);

#endif
