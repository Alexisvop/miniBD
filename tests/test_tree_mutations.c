#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include "test_support.h"

#define ROWS 100000U

/* Formula model is independent of storage layout, bitmap and tree navigation. */
static bool active(uint32_t id) { return id % 3U != 0 || id % 9U == 0; }
static Record expected(uint32_t id)
{
    Record row = {id, "Original", (uint8_t)(id % 151U)};
    if (id % 3U != 0 && id % 5U == 0) {
        strcpy(row.name, "Updated Jos\xc3\xa9");
        row.age = 150;
    }
    if (id % 9U == 0) {
        strcpy(row.name, "Reactivated");
        row.age = 0;
    }
    return row;
}

static DbStatus compare(const Record *row, void *context)
{
    uint32_t *next = context;
    while (*next < ROWS && !active(*next)) ++*next;
    CHECK(*next < ROWS);
    Record model = expected(*next);
    CHECK(row->id == model.id && row->age == model.age && strcmp(row->name, model.name) == 0);
    ++*next;
    return DB_OK;
}

static void verify(Storage *storage, const StorageStats *before)
{
    StorageStats after;
    CHECK(storage_validate(storage, &after) == DB_OK);
    CHECK(after.tree.height == 3 && after.pages == before->pages);
    CHECK(after.tree.entries == ROWS && after.tree.leaves == before->tree.leaves);
    CHECK(after.tree.internal_nodes == before->tree.internal_nodes);
    uint32_t visible = 0;
    for (uint32_t id = 0; id < ROWS; ++id) {
        Record row = {UINT32_MAX, "Untouched", 1};
        CHECK(storage_select(storage, id, &row) == (active(id) ? DB_OK : DB_DELETED));
        if (active(id)) {
            Record model = expected(id);
            CHECK(row.id == id && row.age == model.age && strcmp(row.name, model.name) == 0);
            ++visible;
        } else CHECK(row.id == UINT32_MAX && strcmp(row.name, "Untouched") == 0);
    }
    CHECK(after.active_records == visible && after.deleted_records == ROWS - visible);
    uint32_t next = 0;
    CHECK(storage_scan(storage, compare, &next) == DB_OK);
    while (next < ROWS && !active(next)) ++next;
    CHECK(next == ROWS);
}

int main(void)
{
    char path[64];
    temporary_path(path);
    Storage *storage;
    CHECK(storage_open(path, &storage) == DB_OK);
    for (uint32_t id = 0; id < ROWS; ++id) {
        Record row = {id, "Original", (uint8_t)(id % 151U)};
        CHECK(storage_insert(storage, &row) == DB_OK);
    }
    StorageStats before;
    CHECK(storage_validate(storage, &before) == DB_OK && before.tree.height == 3);
    for (uint32_t id = 0; id < ROWS; ++id) {
        if (id % 3U == 0) CHECK(storage_delete(storage, id) == DB_OK);
        else if (id % 5U == 0) {
            Record row = expected(id);
            CHECK(storage_update(storage, &row) == DB_OK);
        }
        if (id % 9U == 0) {
            Record row = expected(id);
            CHECK(storage_insert(storage, &row) == DB_OK);
        }
    }
    verify(storage, &before);
    CHECK(storage_close(storage) == DB_OK);
    CHECK(storage_open(path, &storage) == DB_OK);
    verify(storage, &before);
    CHECK(storage_close(storage) == DB_OK);
    CHECK(unlink(path) == 0);
    puts("100000-row height-3 tree: updates, deletions, reactivation, all rows and reopen passed.");
    return 0;
}
