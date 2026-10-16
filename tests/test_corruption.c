#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include "test_support.h"

static void reject_unchanged(const char *path, const uint8_t *bytes, size_t length, DbStatus expected)
{
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fwrite(bytes, 1, length, file) == length);
    CHECK(fclose(file) == 0);
    Storage *storage;
    CHECK(storage_open(path, &storage) == expected && storage == NULL);
    uint8_t copy[4 * MINIDB_PAGE_SIZE];
    file = fopen(path, "rb");
    CHECK(file != NULL);
    CHECK(fread(copy, 1, sizeof(copy), file) == length);
    CHECK(!ferror(file));
    CHECK(fgetc(file) == EOF && !ferror(file));
    CHECK(memcmp(copy, bytes, length) == 0);
    CHECK(fclose(file) == 0);
}

int main(void)
{
    char path[64];
    temporary_path(path);
    Storage *storage;
    CHECK(storage_open(path, &storage) == DB_OK);
    Record row = {1, "Alexis", 21};
    CHECK(storage_insert(storage, &row) == DB_OK);
    row.id = 2;
    CHECK(storage_insert(storage, &row) == DB_OK);
    CHECK(storage_close(storage) == DB_OK);
    uint8_t original[4 * MINIDB_PAGE_SIZE] = {0};
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    CHECK(fread(original, 1, sizeof(original), file) == 3 * MINIDB_PAGE_SIZE);
    CHECK(!ferror(file));
    CHECK(fclose(file) == 0);

    const struct { size_t offset; uint8_t value; DbStatus status; } cases[] = {
        {0, 'X', DB_INVALID_FORMAT}, {8, 1, DB_UNSUPPORTED_VERSION},
        {12, 1, DB_INVALID_FORMAT}, {16, 4, DB_INVALID_FORMAT},
        {20, 3, DB_INVALID_FORMAT}, {20, 0, DB_INVALID_FORMAT},
        {24, 0, DB_INVALID_FORMAT}, {28, 2, DB_INVALID_FORMAT},
        {36, 1, DB_INVALID_FORMAT}, {4095, 1, DB_INVALID_FORMAT},
        {8192, 3, DB_INVALID_FORMAT}, {8196, 59, DB_INVALID_FORMAT},
        {8196, 0, DB_INVALID_FORMAT}, {8200, 1, DB_INVALID_FORMAT},
        {8208 + 4, 151, DB_INVALID_FORMAT}, {8208 + 5, 0, DB_INVALID_FORMAT},
        {8208 + 5, 65, DB_INVALID_FORMAT}, {8208 + 6, 0xff, DB_INVALID_FORMAT},
        {8208 + 6, 0, DB_INVALID_FORMAT}, {8208 + 69, 1, DB_INVALID_FORMAT},
        {8208 + 70, 1, DB_INVALID_FORMAT},
        {8208 + 140, 1, DB_INVALID_FORMAT}, {12287, 1, DB_INVALID_FORMAT}
    };
    uint8_t modified[sizeof(original)];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        memcpy(modified, original, sizeof(original));
        modified[cases[i].offset] = cases[i].value;
        reject_unchanged(path, modified, 3 * MINIDB_PAGE_SIZE, cases[i].status);
    }
    const size_t lengths[] = {1, 4095, 4096, 4097, 8192, 12287, 16384};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        reject_unchanged(path, original, lengths[i], DB_INVALID_FORMAT);
    }
    memset(modified, 0, MINIDB_PAGE_SIZE);
    reject_unchanged(path, modified, MINIDB_PAGE_SIZE, DB_INVALID_FORMAT);
    CHECK(unlink(path) == 0);
    puts("Corruption: headers, records, padding, duplicate IDs and truncations rejected unchanged.");
    return 0;
}
