#define _POSIX_C_SOURCE 200809L
#include "record.h"
#include "test_support.h"

int main(void)
{
    Record original = {0x12345678U, "Jos\xc3\xa9", 21};
    uint8_t bytes[MINIDB_RECORD_SIZE];
    CHECK(record_serialize(&original, bytes));
    const uint8_t prefix[] = {0x78, 0x56, 0x34, 0x12, 21, 5, 'J', 'o', 's', 0xc3, 0xa9};
    CHECK(memcmp(bytes, prefix, sizeof(prefix)) == 0);
    for (size_t i = sizeof(prefix); i < sizeof(bytes); ++i) {
        CHECK(bytes[i] == 0);
    }
    Record decoded = {0};
    CHECK(record_deserialize(bytes, &decoded));
    CHECK(decoded.id == original.id && decoded.age == original.age);
    CHECK(strcmp(decoded.name, original.name) == 0);

    original.id = UINT32_MAX;
    original.age = MINIDB_AGE_MAX;
    memset(original.name, 'A', MINIDB_NAME_MAX_BYTES);
    original.name[MINIDB_NAME_MAX_BYTES] = '\0';
    CHECK(record_serialize(&original, bytes));
    CHECK(bytes[5] == 64 && bytes[69] == 'A');
    CHECK(record_deserialize(bytes, &decoded));
    CHECK(decoded.id == UINT32_MAX && decoded.age == 150);
    CHECK(strcmp(original.name, decoded.name) == 0);

    original.age = 151;
    uint8_t saved[MINIDB_RECORD_SIZE];
    memcpy(saved, bytes, sizeof(saved));
    CHECK(!record_serialize(&original, bytes));
    CHECK(memcmp(saved, bytes, sizeof(saved)) == 0);
    original.age = 0;
    memset(original.name, 'X', sizeof(original.name));
    CHECK(!record_valid(&original));
    CHECK(!record_serialize(&original, bytes));
    strcpy(original.name, "");
    CHECK(!record_valid(&original));
    strcpy(original.name, "a\"b");
    CHECK(!record_valid(&original));

    const struct { size_t offset; uint8_t value; } corruptions[] = {
        {4, 151}, {5, 0}, {5, 65}, {5, 255}, {6, 0}, {6, 0xff}, {6, '\t'}, {6, '"'}
    };
    for (size_t i = 0; i < sizeof(corruptions) / sizeof(corruptions[0]); ++i) {
        memcpy(bytes, saved, sizeof(bytes));
        bytes[corruptions[i].offset] = corruptions[i].value;
        decoded.id = 123;
        CHECK(!record_deserialize(bytes, &decoded));
        CHECK(decoded.id == 123);
    }
    original = (Record){0, "A", 0};
    CHECK(record_serialize(&original, bytes));
    bytes[69] = 1;
    CHECK(!record_deserialize(bytes, &decoded));
    puts("Record: exact little-endian bytes, boundaries, round trips and corruption passed.");
    return 0;
}
