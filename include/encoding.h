#ifndef MINIDB_ENCODING_H
#define MINIDB_ENCODING_H

#include <stdint.h>

/* Byte operations avoid host byte order and alignment assumptions. */
static inline uint32_t decode_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static inline void encode_u32(uint8_t *bytes, uint32_t value)
{
    for (unsigned int i = 0; i < 4; ++i) {
        bytes[i] = (uint8_t)(value >> (8U * i));
    }
}

#endif
