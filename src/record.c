#include "record.h"
#include "encoding.h"

#include <string.h>

/* Reject overlong encodings, surrogates, truncated sequences and > U+10FFFF. */
static bool valid_utf8(const unsigned char *text, size_t length)
{
    size_t i = 0;
    while (i < length) {
        unsigned char first = text[i++];
        if (first < 0x80U) {
            if (first < 0x20U || first == 0x7fU) {
                return false;
            }
            continue;
        }
        size_t extra;
        uint32_t codepoint;
        uint32_t minimum;
        if (first >= 0xc2U && first <= 0xdfU) {
            extra = 1; codepoint = first & 0x1fU; minimum = 0x80U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            extra = 2; codepoint = first & 0x0fU; minimum = 0x800U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            extra = 3; codepoint = first & 0x07U; minimum = 0x10000U;
        } else {
            return false;
        }
        if (length - i < extra) {
            return false;
        }
        for (size_t j = 0; j < extra; ++j) {
            unsigned char next = text[i++];
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            return false;
        }
    }
    return true;
}

bool record_name_valid(const char *name, size_t length)
{
    return length > 0 && length <= MINIDB_NAME_MAX_BYTES &&
           memchr(name, '"', length) == NULL &&
           valid_utf8((const unsigned char *)name, length);
}

bool record_valid(const Record *record)
{
    const char *end = memchr(record->name, '\0', sizeof(record->name));
    return end != NULL && record->age <= MINIDB_AGE_MAX &&
           record_name_valid(record->name, (size_t)(end - record->name));
}

bool record_serialize(const Record *record, uint8_t output[MINIDB_RECORD_SIZE])
{
    if (!record_valid(record)) {
        return false;
    }
    size_t length = strlen(record->name);
    memset(output, 0, MINIDB_RECORD_SIZE);
    encode_u32(output, record->id);
    output[4] = record->age;
    output[5] = (uint8_t)length;
    memcpy(output + 6, record->name, length);
    return true;
}

bool record_deserialize(const uint8_t input[MINIDB_RECORD_SIZE], Record *output)
{
    size_t length = input[5];
    if (input[4] > MINIDB_AGE_MAX ||
        !record_name_valid((const char *)input + 6, length)) {
        return false;
    }
    for (size_t i = 6 + length; i < MINIDB_RECORD_SIZE; ++i) {
        if (input[i] != 0) {
            return false;
        }
    }
    Record record = {0};
    record.id = decode_u32(input);
    record.age = input[4];
    memcpy(record.name, input + 6, length);
    *output = record;
    return true;
}
