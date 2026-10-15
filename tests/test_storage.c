#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include "encoding.h"
#include "test_support.h"

#include <sys/stat.h>

typedef struct { uint32_t next; } ScanState;

static DbStatus check_order(const Record *record, void *context)
{
    ScanState *state = context;
    CHECK(record->id == 1000U - state->next);
    ++state->next;
    return DB_OK;
}

static DbStatus stop_scan(const Record *record, void *context)
{
    (void)record;
    unsigned int *calls = context;
    ++*calls;
    return DB_IO_ERROR;
}

static void test_full_database(const char *path)
{
    /* Generate a valid, nearly-full fixture directly to avoid quadratic INSERT
     * setup. Then exercise the public INSERT API at the actual capacity. */
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    uint8_t page[MINIDB_PAGE_SIZE] = {0};
    memcpy(page, "MINIDB\r\n", 8);
    encode_u32(page + 8, 1);
    encode_u32(page + 12, MINIDB_PAGE_SIZE);
    encode_u32(page + 16, MINIDB_MAX_PAGES);
    encode_u32(page + 20, MINIDB_MAX_RECORDS - 1U);
    encode_u32(page + 28, 1);
    CHECK(fwrite(page, 1, sizeof(page), file) == sizeof(page));
    uint32_t next = 0;
    for (uint32_t id = 1; id < MINIDB_MAX_PAGES; ++id) {
        memset(page, 0, sizeof(page));
        encode_u32(page, 2);
        uint32_t count = MINIDB_RECORDS_PER_PAGE - (id == MINIDB_MAX_PAGES - 1U ? 1U : 0U);
        encode_u32(page + 4, count);
        for (uint32_t slot = 0; slot < count; ++slot) {
            Record record = {next++, "Capacity", 42};
            CHECK(record_serialize(&record, page + MINIDB_DATA_HEADER_SIZE + slot * MINIDB_RECORD_SIZE));
        }
        CHECK(fwrite(page, 1, sizeof(page), file) == sizeof(page));
    }
    CHECK(next == MINIDB_MAX_RECORDS - 1U);
    CHECK(fclose(file) == 0);
    Storage *storage;
    CHECK(storage_open(path, &storage) == DB_OK);
    Record last = {MINIDB_MAX_RECORDS - 1U, "Last", 150};
    CHECK(storage_insert(storage, &last) == DB_OK);
    CHECK(storage_record_count(storage) == MINIDB_MAX_RECORDS);
    CHECK(storage_insert(storage, &last) == DB_DUPLICATE_ID);
    last.id = MINIDB_MAX_RECORDS;
    CHECK(storage_insert(storage, &last) == DB_LIMIT_REACHED);
    CHECK(storage_close(storage) == DB_OK);
    CHECK(storage_open(path, &storage) == DB_OK);
    CHECK(storage_record_count(storage) == MINIDB_MAX_RECORDS);
    CHECK(storage_select(storage, MINIDB_MAX_RECORDS - 1U, &last) == DB_OK);
    CHECK(strcmp(last.name, "Last") == 0);
    CHECK(storage_select(storage, MINIDB_MAX_RECORDS, &last) == DB_NOT_FOUND);
    CHECK(storage_close(storage) == DB_OK);
}

int main(void)
{
    char path[64];
    temporary_path(path); /* Existing zero-byte files are initialized too. */
    Storage *storage;
    CHECK(storage_open(path, &storage) == DB_OK);
    CHECK(storage_record_count(storage) == 0);
    struct stat info;
    CHECK(stat(path, &info) == 0 && info.st_size == MINIDB_PAGE_SIZE);
    unsigned int calls = 0;
    CHECK(storage_scan(storage, stop_scan, &calls) == DB_OK && calls == 0);
    Record row = {0, "unchanged", 0};
    CHECK(storage_select(storage, 1, &row) == DB_NOT_FOUND);
    CHECK(strcmp(row.name, "unchanged") == 0);
    CHECK(storage_close(storage) == DB_OK);
    CHECK(storage_open(path, &storage) == DB_OK);

    const uint32_t rows = MINIDB_RECORDS_PER_PAGE * 3U + 1U;
    for (uint32_t i = 0; i < rows; ++i) {
        row = (Record){1000U - i, "Jos\xc3\xa9", (uint8_t)(i % 151U)};
        CHECK(storage_insert(storage, &row) == DB_OK);
    }
    CHECK(storage_record_count(storage) == rows);
    row = (Record){1000, "Duplicate", 1};
    CHECK(storage_insert(storage, &row) == DB_DUPLICATE_ID);
    row = (Record){UINT32_MAX, "", 1};
    CHECK(storage_insert(storage, &row) == DB_INVALID_RECORD);
    memset(row.name, 'X', sizeof(row.name));
    CHECK(storage_insert(storage, &row) == DB_INVALID_RECORD);
    CHECK(storage_record_count(storage) == rows);
    ScanState scan = {0};
    CHECK(storage_scan(storage, check_order, &scan) == DB_OK && scan.next == rows);
    CHECK(storage_scan(storage, stop_scan, &calls) == DB_IO_ERROR && calls == 1);
    CHECK(storage_sync(storage) == DB_OK);
    CHECK(stat(path, &info) == 0 && info.st_size == 5 * (off_t)MINIDB_PAGE_SIZE);
    CHECK(storage_close(storage) == DB_OK);
    CHECK(storage_open(path, &storage) == DB_OK);
    CHECK(storage_record_count(storage) == rows);
    for (uint32_t i = 0; i < rows; ++i) {
        CHECK(storage_select(storage, 1000U - i, &row) == DB_OK);
        CHECK(strcmp(row.name, "Jos\xc3\xa9") == 0 && row.age == i % 151U);
    }
    CHECK(storage_select(storage, 1001, &row) == DB_NOT_FOUND);
    row = (Record){0, "Minimum ID", 0};
    CHECK(storage_insert(storage, &row) == DB_OK);
    row.id = UINT32_MAX;
    memset(row.name, 'a', 64); row.name[64] = '\0';
    CHECK(storage_insert(storage, &row) == DB_OK);
    CHECK(storage_close(storage) == DB_OK);
    CHECK(storage_open(path, &storage) == DB_OK);
    CHECK(storage_select(storage, UINT32_MAX, &row) == DB_OK && strlen(row.name) == 64);
    CHECK(storage_select(storage, 0, &row) == DB_OK && strcmp(row.name, "Minimum ID") == 0);
    CHECK(storage_close(storage) == DB_OK);
    test_full_database(path);
    CHECK(unlink(path) == 0);
    puts("Storage: empty, multi-page persistence, scans, duplicates and 237510-row capacity passed.");
    return 0;
}
