#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include "encoding.h"
#include "test_support.h"

static void reject_unchanged(const char *path, const uint8_t *bytes, size_t size, DbStatus expected)
{
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL && fwrite(bytes, 1, size, file) == size && fclose(file) == 0);
    Storage *storage;
    CHECK(storage_open(path, &storage) == expected && storage == NULL);
    file = fopen(path, "rb");
    CHECK(file != NULL);
    uint8_t buffer[4096];
    for (size_t offset = 0; offset < size; offset += sizeof(buffer)) {
        CHECK(fread(buffer, 1, sizeof(buffer), file) == sizeof(buffer));
        CHECK(memcmp(buffer, bytes + offset, sizeof(buffer)) == 0);
    }
    CHECK(fgetc(file) == EOF && !ferror(file) && fclose(file) == 0);
}

int main(void)
{
    char path[64];
    temporary_path(path);
    Storage *storage;
    CHECK(storage_open(path, &storage) == DB_OK);
    for (uint32_t i = 0; i < 100000; ++i) {
        Record row = {i, "Corruption", 42};
        CHECK(storage_insert(storage, &row) == DB_OK);
    }
    StorageStats stats;
    CHECK(storage_validate(storage, &stats) == DB_OK && stats.tree.height == 3);
    CHECK(storage_close(storage) == DB_OK);
    size_t size = (size_t)stats.pages * MINIDB_PAGE_SIZE;
    uint8_t *original = malloc(size + MINIDB_PAGE_SIZE);
    uint8_t *modified = malloc(size + MINIDB_PAGE_SIZE);
    CHECK(original != NULL && modified != NULL);
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL && fread(original, 1, size, file) == size && fclose(file) == 0);
    size_t root = (size_t)decode_u32(original + 24) * MINIDB_PAGE_SIZE;
    size_t internal = (size_t)decode_u32(original + root + 16) * MINIDB_PAGE_SIZE;
    uint32_t leaf_id = decode_u32(original + internal + 16);
    size_t leaf = (size_t)leaf_id * MINIDB_PAGE_SIZE;
    uint32_t data_id = decode_u32(original + leaf + 20);
    const struct { size_t offset; uint32_t value; } cases[] = {
        {24, 0}, {24, data_id}, {32, 0},
        {root, 99}, {root + 4, BTREE_INTERNAL_CAPACITY + 1U}, {root + 4, 0},
        {root + 8, 1}, {root + 12, 1},
        {root + 16, (uint32_t)(root / MINIDB_PAGE_SIZE)}, /* Cycle. */
        {root + 16, stats.pages}, /* Out-of-range reference. */
        {root + 24, decode_u32(original + root + 16)}, /* Aliased child. */
        {root + 20, decode_u32(original + root + 20) + 1U}, /* Wrong separator. */
        {root + 16, leaf_id}, /* Unequal leaf depth. */
        {leaf + 4, BTREE_LEAF_CAPACITY + 1U}, {leaf + 4, 1},
        {leaf + 8, leaf_id}, {leaf + 8, 0}, {leaf + 8, stats.pages},
        {leaf + 12, 1},
        {leaf + 16, 1}, /* Key mismatch / duplicate next key. */
        {leaf + 28, 0}, /* Duplicate / unordered leaf keys. */
        {leaf + 20, (uint32_t)(root / MINIDB_PAGE_SIZE)}, /* Not a data page. */
        {leaf + 20, stats.pages}, {leaf + 24, MINIDB_RECORDS_PER_PAGE},
        {internal + 4, 1}, /* Underfull nonroot internal node. */
        {internal + 20, UINT32_MAX}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        memcpy(modified, original, size);
        encode_u32(modified + cases[i].offset, cases[i].value);
        reject_unchanged(path, modified, size, DB_INVALID_FORMAT);
    }
    memcpy(modified, original, size);
    memset(modified + size, 0, MINIDB_PAGE_SIZE);
    encode_u32(modified + size, BTREE_LEAF_TYPE);
    encode_u32(modified + 16, stats.pages + 1U);
    reject_unchanged(path, modified, size + MINIDB_PAGE_SIZE, DB_INVALID_FORMAT);

    /* A real empty phase-2 v1 file, not just a changed v2 version field. */
    memset(modified, 0, MINIDB_PAGE_SIZE);
    memcpy(modified, "MINIDB\r\n", 8);
    encode_u32(modified + 8, 1);
    encode_u32(modified + 12, MINIDB_PAGE_SIZE);
    encode_u32(modified + 16, 1);
    encode_u32(modified + 28, 1);
    reject_unchanged(path, modified, MINIDB_PAGE_SIZE, DB_UNSUPPORTED_VERSION);
    free(original);
    free(modified);
    CHECK(unlink(path) == 0);
    puts("B+ tree corruption: references, cycles, separators, depth, occupancy, locations, orphans and v1 passed.");
    return 0;
}
