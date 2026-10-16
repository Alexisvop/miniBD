#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include "encoding.h"
#include "test_support.h"

static uint8_t *read_file(const char *path, size_t size)
{
    uint8_t *bytes = malloc(size);
    FILE *file = fopen(path, "rb");
    CHECK(bytes != NULL && file != NULL && fread(bytes, 1, size, file) == size);
    CHECK(fclose(file) == 0);
    return bytes;
}

static void reject_unchanged(const char *path, const uint8_t *bytes, size_t size, DbStatus expected)
{
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL && fwrite(bytes, 1, size, file) == size && fclose(file) == 0);
    Storage *storage;
    CHECK(storage_open(path, &storage) == expected && storage == NULL);
    uint8_t *actual = read_file(path, size);
    CHECK(memcmp(bytes, actual, size) == 0);
    free(actual);
}

int main(void)
{
    char path[64];
    temporary_path(path);
    Storage *storage;
    CHECK(storage_open(path, &storage) == DB_OK);
    for (uint32_t i = 0; i < 700; ++i) {
        Record record = {i, "Original", 21};
        CHECK(storage_insert(storage, &record) == DB_OK);
    }
    StorageStats before, after;
    CHECK(storage_validate(storage, &before) == DB_OK && before.tree.height == 2);
    CHECK(storage_sync(storage) == DB_OK);
    size_t size = (size_t)before.pages * MINIDB_PAGE_SIZE;
    uint8_t *original = read_file(path, size);
    const uint32_t slots[] = {0, 7, 8, 31, 32, 56, 57};
    for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i) CHECK(storage_delete(storage, slots[i]) == DB_OK);
    Record record = {30, "Updated", 150};
    CHECK(storage_update(storage, &record) == DB_OK);
    record = (Record){0, "Reactivated", 0};
    CHECK(storage_insert(storage, &record) == DB_OK);
    CHECK(storage_validate(storage, &after) == DB_OK && after.active_records == 694 && after.deleted_records == 6);
    CHECK(after.records == 700 && after.tree.entries == 700 && after.pages == before.pages);
    CHECK(after.leaf_splits == before.leaf_splits && after.root_splits == before.root_splits);
    CHECK(storage_close(storage) == DB_OK);
    uint8_t *updated = read_file(path, size);
    for (uint32_t id = 1; id < before.pages; ++id) {
        size_t offset = (size_t)id * MINIDB_PAGE_SIZE;
        if (decode_u32(original + offset) != 2)
            CHECK(memcmp(original + offset, updated + offset, MINIDB_PAGE_SIZE) == 0);
    }
    CHECK(decode_u32(updated + 20) == 700 && decode_u32(updated + 36) == 694);
    size_t first_data = 2U * MINIDB_PAGE_SIZE;
    CHECK(updated[first_data + 8] == 0x80 && updated[first_data + 9] == 1);
    CHECK(updated[first_data + 11] == 0x80 && updated[first_data + 12] == 1);
    CHECK(updated[first_data + 15] == 3); /* Slots 56 and 57, bits 58..63 zero. */
    CHECK(storage_open(path, &storage) == DB_OK);
    CHECK(storage_select(storage, 57, &record) == DB_DELETED);
    CHECK(storage_select(storage, 0, &record) == DB_OK && strcmp(record.name, "Reactivated") == 0);
    CHECK(storage_validate(storage, &after) == DB_OK && after.deleted_records == 6);
    CHECK(storage_close(storage) == DB_OK);

    size_t last_data = (size_t)decode_u32(updated + 32) * MINIDB_PAGE_SIZE;
    uint8_t *corrupt = malloc(size);
    CHECK(corrupt != NULL);
    const struct { size_t offset; uint8_t value; } changes[] = {
        {36, 0}, {40, 1}, {first_data + 15, 7}, /* Bit 58 is never a valid slot. */
        {first_data + 15, 0x83}, {last_data + 8, 0x10}, /* Unoccupied slot 4. */
        {first_data + 8, 0}, /* Cleared tombstone disagrees with active count. */
        {first_data + 16U + 57U * 70U + 4U, 151} /* Deleted payload still validated. */
    };
    for (size_t i = 0; i < sizeof(changes) / sizeof(changes[0]); ++i) {
        memcpy(corrupt, updated, size);
        corrupt[changes[i].offset] = changes[i].value;
        reject_unchanged(path, corrupt, size, DB_INVALID_FORMAT);
    }
    /* Valid prior v2 image: bitmap and active-count fields were reserved zero. */
    memcpy(corrupt, original, size);
    encode_u32(corrupt + 8, 2);
    encode_u32(corrupt + 36, 0);
    reject_unchanged(path, corrupt, size, DB_UNSUPPORTED_VERSION);
    free(original); free(updated); free(corrupt);
    CHECK(unlink(path) == 0);
    puts("Tombstones: exact bitmap bits, unchanged index, persistent state, invalid bits/counts and v2 rejection passed.");
    return 0;
}
