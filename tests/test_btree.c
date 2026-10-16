#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include "test_support.h"

#include <inttypes.h>

typedef struct { uint32_t next; uint32_t count; } Model;

static DbStatus compare_model(const Record *record, void *context)
{
    Model *model = context;
    CHECK(model->next < model->count && record->id == model->next * 2U);
    char expected[65];
    CHECK(snprintf(expected, sizeof(expected), "row-%u", model->next) > 0);
    CHECK(strcmp(record->name, expected) == 0 && record->age == model->next % 151U);
    ++model->next;
    return DB_OK;
}

static void verify_records(Storage *storage, uint32_t count)
{
    Model model = {0, count};
    CHECK(storage_scan(storage, compare_model, &model) == DB_OK && model.next == count);
    for (uint32_t i = 0; i < count; ++i) {
        Record record;
        CHECK(storage_select(storage, i * 2U, &record) == DB_OK);
        Model single = {i, count};
        CHECK(compare_model(&record, &single) == DB_OK);
        CHECK(storage_select(storage, i * 2U + 1U, &record) == DB_NOT_FOUND);
    }
}

static void run_pattern(uint32_t count, unsigned int pattern)
{
    char path[64];
    temporary_path(path);
    uint32_t *keys = malloc((size_t)count * sizeof(*keys));
    CHECK(keys != NULL);
    for (uint32_t i = 0; i < count; ++i) keys[i] = pattern == 1 ? count - i - 1U : i;
    if (pattern == 2) {
        uint32_t random = 0x12345678U;
        for (uint32_t i = count - 1U; i > 0; --i) {
            random = random * 1664525U + 1013904223U;
            uint32_t j = random % (i + 1U);
            uint32_t swap = keys[i]; keys[i] = keys[j]; keys[j] = swap;
        }
    }
    Storage *storage;
    CHECK(storage_open(path, &storage) == DB_OK);
    StorageStats stats;
    CHECK(storage_validate(storage, &stats) == DB_OK);
    CHECK(stats.records == 0 && stats.tree.height == 1 && stats.tree.leaves == 1);
    for (uint32_t i = 0; i < count; ++i) {
        Record record = {.id = keys[i] * 2U, .age = (uint8_t)(keys[i] % 151U)};
        CHECK(snprintf(record.name, sizeof(record.name), "row-%u", keys[i]) > 0);
        CHECK(storage_insert(storage, &record) == DB_OK);
        if (i == 0 || i == 339 || i == 340 || i % 997U == 0 || i + 1U == count) {
            CHECK(storage_validate(storage, &stats) == DB_OK);
            CHECK(stats.records == i + 1U && stats.tree.entries == i + 1U);
        }
        if (i % 997U == 0) CHECK(storage_insert(storage, &record) == DB_DUPLICATE_ID);
    }
    verify_records(storage, count);
    CHECK(storage_validate(storage, &stats) == DB_OK);
    CHECK(stats.leaf_splits > 0 && stats.root_splits > 0);
    if (count >= 100000 && pattern != 2) {
        CHECK(stats.tree.height >= 3 && stats.internal_splits > 0 && stats.root_splits >= 2);
    }
    printf("pattern=%u records=%u height=%u leaves=%u internal=%u splits=%" PRIu64 "/%" PRIu64 "/%" PRIu64 "\n",
           pattern, count, stats.tree.height, stats.tree.leaves, stats.tree.internal_nodes,
           stats.leaf_splits, stats.internal_splits, stats.root_splits);
    CHECK(storage_close(storage) == DB_OK);
    CHECK(storage_open(path, &storage) == DB_OK);
    StorageStats reopened;
    CHECK(storage_validate(storage, &reopened) == DB_OK);
    CHECK(reopened.tree.height == stats.tree.height && reopened.tree.leaves == stats.tree.leaves);
    CHECK(reopened.leaf_splits == 0 && reopened.internal_splits == 0 && reopened.root_splits == 0);
    verify_records(storage, count);
    Record maximum = {UINT32_MAX, "Maximum", 150};
    CHECK(storage_insert(storage, &maximum) == DB_OK);
    CHECK(storage_select(storage, UINT32_MAX, &maximum) == DB_OK);
    CHECK(storage_validate(storage, &reopened) == DB_OK);
    CHECK(storage_close(storage) == DB_OK);
    CHECK(unlink(path) == 0);
    free(keys);
}

int main(int argc, char **argv)
{
    uint32_t count = argc == 2 && strcmp(argv[1], "--large") == 0 ? 100000U : 10000U;
    for (unsigned int pattern = 0; pattern < 3; ++pattern) run_pattern(count, pattern);
    puts("B+ tree: reference model, all lookups, ordered scans, splits and reopen passed.");
    return 0;
}
