#include "storage.h"
#include "encoding.h"

#include <stdlib.h>
#include <string.h>

#define META_TYPE 1U
#define DATA_TYPE 2U
#define META_USED_BYTES 32U

struct Storage {
    Pager *pager;
    uint32_t record_count;
};

static bool all_zero(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (data[i] != 0) {
            return false;
        }
    }
    return true;
}

static int compare_ids(const void *left, const void *right)
{
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

static DbStatus validate_data(Storage *storage)
{
    uint32_t *ids = NULL;
    if (storage->record_count > 0) {
        ids = malloc((size_t)storage->record_count * sizeof(*ids));
        if (ids == NULL) {
            return DB_NO_MEMORY;
        }
    }
    uint32_t seen = 0;
    DbStatus status = DB_OK;
    for (uint32_t id = 1; id < pager_page_count(storage->pager); ++id) {
        const uint8_t *page;
        status = pager_get(storage->pager, id, &page);
        if (status != DB_OK) {
            break;
        }
        uint32_t expected = storage->record_count - seen;
        if (expected > MINIDB_RECORDS_PER_PAGE) {
            expected = MINIDB_RECORDS_PER_PAGE;
        }
        size_t used = MINIDB_DATA_HEADER_SIZE + (size_t)expected * MINIDB_RECORD_SIZE;
        if (decode_u32(page) != DATA_TYPE || decode_u32(page + 4) != expected ||
            !all_zero(page + 8, 8) || !all_zero(page + used, MINIDB_PAGE_SIZE - used)) {
            status = DB_INVALID_FORMAT;
            break;
        }
        for (uint32_t slot = 0; slot < expected; ++slot) {
            Record record;
            const uint8_t *bytes = page + MINIDB_DATA_HEADER_SIZE + slot * MINIDB_RECORD_SIZE;
            if (!record_deserialize(bytes, &record)) {
                status = DB_INVALID_FORMAT;
                break;
            }
            ids[seen++] = record.id;
        }
        if (status != DB_OK) {
            break;
        }
    }
    /* A temporary sorted ID array detects duplicate corruption, not an index.
     * All queries and duplicate checks during INSERT remain sequential. */
    if (status == DB_OK && seen > 1) {
        qsort(ids, seen, sizeof(*ids), compare_ids);
        for (uint32_t i = 1; i < seen; ++i) {
            if (ids[i - 1] == ids[i]) {
                status = DB_INVALID_FORMAT;
                break;
            }
        }
    }
    free(ids);
    return status;
}

static DbStatus validate_storage(Storage *storage)
{
    const uint8_t *meta;
    DbStatus status = pager_get(storage->pager, 0, &meta);
    if (status != DB_OK) {
        return status;
    }
    if (memcmp(meta, "MINIDB\r\n", 8) != 0) {
        return DB_INVALID_FORMAT;
    }
    if (decode_u32(meta + 8) != MINIDB_FORMAT_VERSION) {
        return DB_UNSUPPORTED_VERSION;
    }
    uint32_t pages = decode_u32(meta + 16);
    uint32_t records = decode_u32(meta + 20);
    if (decode_u32(meta + 12) != MINIDB_PAGE_SIZE ||
        pages != pager_page_count(storage->pager) ||
        records > MINIDB_MAX_RECORDS ||
        pages != 1U + (records + MINIDB_RECORDS_PER_PAGE - 1U) / MINIDB_RECORDS_PER_PAGE ||
        decode_u32(meta + 24) != 0 || decode_u32(meta + 28) != META_TYPE ||
        !all_zero(meta + META_USED_BYTES, MINIDB_PAGE_SIZE - META_USED_BYTES)) {
        return DB_INVALID_FORMAT;
    }
    storage->record_count = records;
    return validate_data(storage);
}

static DbStatus initialize_storage(Storage *storage)
{
    uint32_t id;
    uint8_t *meta;
    DbStatus status = pager_allocate(storage->pager, &id, &meta);
    if (status != DB_OK) {
        return status;
    }
    memcpy(meta, "MINIDB\r\n", 8);
    encode_u32(meta + 8, MINIDB_FORMAT_VERSION);
    encode_u32(meta + 12, MINIDB_PAGE_SIZE);
    encode_u32(meta + 16, 1);
    encode_u32(meta + 28, META_TYPE);
    return pager_sync(storage->pager);
}

DbStatus storage_open(const char *path, Storage **output)
{
    *output = NULL;
    Storage *storage = calloc(1, sizeof(*storage));
    if (storage == NULL) {
        return DB_NO_MEMORY;
    }
    DbStatus status = pager_open(path, &storage->pager);
    if (status != DB_OK) {
        free(storage);
        return status;
    }
    status = pager_page_count(storage->pager) == 0
        ? initialize_storage(storage) : validate_storage(storage);
    if (status != DB_OK) {
        DbStatus close_status = pager_discard(storage->pager);
        free(storage);
        return close_status == DB_OK ? status : close_status;
    }
    *output = storage;
    return DB_OK;
}

DbStatus storage_select(Storage *storage, uint32_t id, Record *output)
{
    const uint8_t *meta;
    DbStatus ready = pager_get(storage->pager, 0, &meta);
    if (ready != DB_OK) {
        return ready;
    }
    for (uint32_t page_id = 1; page_id < pager_page_count(storage->pager); ++page_id) {
        const uint8_t *page;
        DbStatus status = pager_get(storage->pager, page_id, &page);
        if (status != DB_OK) {
            return status;
        }
        uint32_t count = decode_u32(page + 4);
        for (uint32_t slot = 0; slot < count; ++slot) {
            const uint8_t *bytes = page + MINIDB_DATA_HEADER_SIZE + slot * MINIDB_RECORD_SIZE;
            if (decode_u32(bytes) == id) {
                return record_deserialize(bytes, output) ? DB_OK : DB_INVALID_FORMAT;
            }
        }
    }
    return DB_NOT_FOUND;
}

DbStatus storage_insert(Storage *storage, const Record *record)
{
    uint8_t bytes[MINIDB_RECORD_SIZE];
    if (!record_serialize(record, bytes)) {
        return DB_INVALID_RECORD;
    }
    Record existing;
    DbStatus status = storage_select(storage, record->id, &existing);
    if (status == DB_OK) {
        return DB_DUPLICATE_ID;
    }
    if (status != DB_NOT_FOUND) {
        return status;
    }
    if (storage->record_count == MINIDB_MAX_RECORDS) {
        return DB_LIMIT_REACHED;
    }
    uint8_t *meta;
    status = pager_write(storage->pager, 0, &meta);
    if (status != DB_OK) {
        return status;
    }
    uint32_t slot = storage->record_count % MINIDB_RECORDS_PER_PAGE;
    uint32_t page_id = 1U + storage->record_count / MINIDB_RECORDS_PER_PAGE;
    uint8_t *page;
    status = slot == 0 ? pager_allocate(storage->pager, &page_id, &page)
                       : pager_write(storage->pager, page_id, &page);
    if (status != DB_OK) {
        return status;
    }
    /* All fallible work is complete before publishing the appended record. */
    encode_u32(page, DATA_TYPE);
    memcpy(page + MINIDB_DATA_HEADER_SIZE + slot * MINIDB_RECORD_SIZE, bytes, sizeof(bytes));
    encode_u32(page + 4, slot + 1U);
    ++storage->record_count;
    encode_u32(meta + 16, pager_page_count(storage->pager));
    encode_u32(meta + 20, storage->record_count);
    return DB_OK;
}

DbStatus storage_scan(Storage *storage, RecordVisitor visitor, void *context)
{
    const uint8_t *meta;
    DbStatus ready = pager_get(storage->pager, 0, &meta);
    if (ready != DB_OK) {
        return ready;
    }
    for (uint32_t id = 1; id < pager_page_count(storage->pager); ++id) {
        const uint8_t *page;
        DbStatus status = pager_get(storage->pager, id, &page);
        if (status != DB_OK) {
            return status;
        }
        for (uint32_t slot = 0; slot < decode_u32(page + 4); ++slot) {
            Record record;
            if (!record_deserialize(page + MINIDB_DATA_HEADER_SIZE + slot * MINIDB_RECORD_SIZE, &record)) {
                return DB_INVALID_FORMAT;
            }
            status = visitor(&record, context);
            if (status != DB_OK) {
                return status;
            }
        }
    }
    return DB_OK;
}

uint32_t storage_record_count(const Storage *storage)
{
    return storage->record_count;
}

DbStatus storage_sync(Storage *storage)
{
    return pager_sync(storage->pager);
}

DbStatus storage_close(Storage *storage)
{
    DbStatus status = pager_close(storage->pager);
    free(storage);
    return status;
}
