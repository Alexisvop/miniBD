#include "storage.h"
#include "encoding.h"

#include <stdlib.h>
#include <string.h>

#define META_TYPE 1U
#define DATA_TYPE 2U
#define META_USED_BYTES 40U

struct Storage {
    Pager *pager;
    BTree tree;
    uint32_t record_count;
    uint32_t last_data;
    uint32_t active_count;
};

static bool all_zero(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (data[i] != 0) return false;
    }
    return true;
}

/* Bit i is stored at byte 8 + i/8, with the least significant bit first. */
static bool slot_deleted(const uint8_t *page, uint32_t slot)
{
    return (page[8U + slot / 8U] & (uint8_t)(1U << (slot % 8U))) != 0;
}

static void set_deleted(uint8_t *page, uint32_t slot, bool deleted)
{
    uint8_t mask = (uint8_t)(1U << (slot % 8U));
    if (deleted) page[8U + slot / 8U] |= mask;
    else page[8U + slot / 8U] &= (uint8_t)~mask;
}

static DbStatus read_record(Storage *storage, uint32_t key, RecordLocation location, Record *output, bool *deleted)
{
    if (location.page == 0 || location.page >= pager_page_count(storage->pager) ||
        location.slot >= MINIDB_RECORDS_PER_PAGE) return DB_INVALID_FORMAT;
    const uint8_t *page;
    DbStatus status = pager_get(storage->pager, location.page, &page);
    if (status != DB_OK) return status;
    if (decode_u32(page) != DATA_TYPE || location.slot >= decode_u32(page + 4)) return DB_INVALID_FORMAT;
    Record record;
    if (!record_deserialize(page + 16U + location.slot * MINIDB_RECORD_SIZE, &record) || record.id != key)
        return DB_INVALID_FORMAT;
    *output = record;
    *deleted = slot_deleted(page, location.slot);
    return DB_OK;
}

static DbStatus validate_entry(uint32_t key, RecordLocation location, void *context)
{
    Record record;
    bool deleted;
    return read_record(context, key, location, &record, &deleted);
}

static DbStatus validate_storage(Storage *storage, BTreeStats *stats)
{
    const uint8_t *meta;
    DbStatus status = pager_get(storage->pager, 0, &meta);
    if (status != DB_OK) return status;
    if (memcmp(meta, "MINIDB\r\n", 8) != 0) return DB_INVALID_FORMAT;
    if (decode_u32(meta + 8) != MINIDB_FORMAT_VERSION) return DB_UNSUPPORTED_VERSION;
    uint32_t pages = decode_u32(meta + 16);
    uint32_t records = decode_u32(meta + 20);
    uint32_t last_data = decode_u32(meta + 32);
    uint32_t active = decode_u32(meta + 36);
    if (decode_u32(meta + 12) != MINIDB_PAGE_SIZE || pages != pager_page_count(storage->pager) ||
        pages < 2 || records > MINIDB_MAX_RECORDS || active > records || decode_u32(meta + 28) != META_TYPE ||
        !all_zero(meta + META_USED_BYTES, MINIDB_PAGE_SIZE - META_USED_BYTES)) return DB_INVALID_FORMAT;

    uint8_t seen[MINIDB_MAX_PAGES] = {1};
    uint32_t total = 0, active_seen = 0, actual_last = 0, previous_count = MINIDB_RECORDS_PER_PAGE;
    for (uint32_t id = 1; id < pages; ++id) {
        const uint8_t *page;
        status = pager_get(storage->pager, id, &page);
        if (status != DB_OK) return status;
        uint32_t type = decode_u32(page);
        if (type == DATA_TYPE) {
            seen[id] = 1;
            uint32_t count = decode_u32(page + 4);
            if (count == 0 || count > MINIDB_RECORDS_PER_PAGE || previous_count != MINIDB_RECORDS_PER_PAGE) return DB_INVALID_FORMAT;
            for (uint32_t slot = count; slot < 64U; ++slot) {
                if (slot_deleted(page, slot)) return DB_INVALID_FORMAT;
            }
            size_t used = 16U + (size_t)count * MINIDB_RECORD_SIZE;
            if (!all_zero(page + used, MINIDB_PAGE_SIZE - used)) return DB_INVALID_FORMAT;
            for (uint32_t slot = 0; slot < count; ++slot) {
                Record record;
                if (!record_deserialize(page + 16U + slot * MINIDB_RECORD_SIZE, &record)) return DB_INVALID_FORMAT;
                if (!slot_deleted(page, slot)) ++active_seen;
            }
            total += count;
            previous_count = count;
            actual_last = id;
        } else if (type != BTREE_LEAF_TYPE && type != BTREE_INTERNAL_TYPE) {
            return DB_INVALID_FORMAT;
        }
    }
    if (total != records || actual_last != last_data || active_seen != active) return DB_INVALID_FORMAT;
    BTree candidate = storage->tree;
    candidate.root = decode_u32(meta + 24);
    status = btree_validate(&candidate, seen, validate_entry, storage, stats);
    if (status != DB_OK) return status;
    if (stats->entries != records) return DB_INVALID_FORMAT;
    for (uint32_t id = 1; id < pages; ++id) {
        if (seen[id] == 0) return DB_INVALID_FORMAT; /* Unreachable index page. */
    }
    storage->record_count = records;
    storage->active_count = active;
    storage->last_data = last_data;
    storage->tree.root = candidate.root;
    return DB_OK;
}

static DbStatus initialize_storage(Storage *storage)
{
    uint32_t id;
    uint8_t *meta;
    DbStatus status = pager_allocate(storage->pager, &id, &meta);
    if (status != DB_OK) return status;
    status = btree_create(&storage->tree, storage->pager);
    if (status != DB_OK) return status;
    memcpy(meta, "MINIDB\r\n", 8);
    encode_u32(meta + 8, MINIDB_FORMAT_VERSION);
    encode_u32(meta + 12, MINIDB_PAGE_SIZE);
    encode_u32(meta + 16, pager_page_count(storage->pager));
    encode_u32(meta + 24, storage->tree.root);
    encode_u32(meta + 28, META_TYPE);
    return pager_sync(storage->pager);
}

DbStatus storage_open(const char *path, Storage **output)
{
    *output = NULL;
    Storage *storage = calloc(1, sizeof(*storage));
    if (storage == NULL) return DB_NO_MEMORY;
    DbStatus status = pager_open(path, &storage->pager);
    if (status != DB_OK) {
        free(storage);
        return status;
    }
    storage->tree.pager = storage->pager;
    BTreeStats stats;
    status = pager_page_count(storage->pager) == 0 ? initialize_storage(storage) : validate_storage(storage, &stats);
    if (status != DB_OK) {
        DbStatus close_status = pager_discard(storage->pager);
        free(storage);
        return close_status == DB_OK ? status : close_status;
    }
    *output = storage;
    return DB_OK;
}

/* Raw lookup is internal: callers must distinguish tombstones before publishing. */
static DbStatus locate_record(Storage *storage, uint32_t id, RecordLocation *location,
                              Record *record, bool *deleted)
{
    DbStatus status = btree_search(&storage->tree, id, location);
    return status == DB_OK ? read_record(storage, id, *location, record, deleted) : status;
}

DbStatus storage_select(Storage *storage, uint32_t id, Record *output)
{
    RecordLocation location;
    Record record;
    bool deleted;
    DbStatus status = locate_record(storage, id, &location, &record, &deleted);
    if (status != DB_OK) return status;
    if (deleted) return DB_DELETED;
    *output = record;
    return DB_OK;
}

/* The index/location never changes. NULL bytes means keep the existing payload. */
static DbStatus modify_slot(Storage *storage, RecordLocation location,
                            const uint8_t *bytes, bool deleted)
{
    DbStatus status = pager_begin(storage->pager);
    if (status != DB_OK) return status;
    uint8_t *page, *meta = NULL;
    status = pager_write(storage->pager, location.page, &page);
    if (status != DB_OK) goto rollback;
    bool was_deleted = slot_deleted(page, location.slot);
    uint32_t active = storage->active_count;
    if (was_deleted != deleted) {
        status = pager_write(storage->pager, 0, &meta);
        if (status != DB_OK) goto rollback;
        active = deleted ? active - 1U : active + 1U;
    }
    if (bytes != NULL) memcpy(page + 16U + location.slot * MINIDB_RECORD_SIZE, bytes, MINIDB_RECORD_SIZE);
    set_deleted(page, location.slot, deleted);
    if (meta != NULL) encode_u32(meta + 36, active);
    pager_commit(storage->pager);
    storage->active_count = active;
    return DB_OK;
rollback:
    pager_rollback(storage->pager);
    return status;
}

DbStatus storage_update(Storage *storage, const Record *record)
{
    uint8_t bytes[MINIDB_RECORD_SIZE];
    if (!record_serialize(record, bytes)) return DB_INVALID_RECORD;
    RecordLocation location;
    Record current;
    bool deleted;
    DbStatus status = locate_record(storage, record->id, &location, &current, &deleted);
    if (status != DB_OK) return status;
    if (deleted) return DB_DELETED;
    return modify_slot(storage, location, bytes, false);
}

DbStatus storage_delete(Storage *storage, uint32_t id)
{
    RecordLocation location;
    Record current;
    bool deleted;
    DbStatus status = locate_record(storage, id, &location, &current, &deleted);
    if (status != DB_OK) return status;
    if (deleted) return DB_DELETED;
    return modify_slot(storage, location, NULL, true);
}

DbStatus storage_insert(Storage *storage, const Record *record)
{
    uint8_t bytes[MINIDB_RECORD_SIZE];
    if (!record_serialize(record, bytes)) return DB_INVALID_RECORD;
    RecordLocation location;
    DbStatus status = btree_search(&storage->tree, record->id, &location);
    if (status == DB_OK) {
        Record current;
        bool deleted;
        status = read_record(storage, record->id, location, &current, &deleted);
        if (status != DB_OK) return status;
        return deleted ? modify_slot(storage, location, bytes, false) : DB_DUPLICATE_ID;
    }
    if (status != DB_NOT_FOUND) return status;
    if (storage->record_count == MINIDB_MAX_RECORDS) return DB_LIMIT_REACHED;
    status = pager_begin(storage->pager);
    if (status != DB_OK) return status;
    BTree saved_tree = storage->tree;
    uint8_t *meta, *page;
    status = pager_write(storage->pager, 0, &meta);
    if (status != DB_OK) goto rollback;
    location.slot = storage->record_count % MINIDB_RECORDS_PER_PAGE;
    location.page = storage->last_data;
    status = location.slot == 0 ? pager_allocate(storage->pager, &location.page, &page)
                               : pager_write(storage->pager, location.page, &page);
    if (status != DB_OK) goto rollback;
    encode_u32(page, DATA_TYPE);
    memcpy(page + 16U + location.slot * MINIDB_RECORD_SIZE, bytes, sizeof(bytes));
    encode_u32(page + 4, location.slot + 1U);
    status = btree_insert(&storage->tree, record->id, location);
    if (status != DB_OK) goto rollback;
    encode_u32(meta + 16, pager_page_count(storage->pager));
    encode_u32(meta + 20, storage->record_count + 1U);
    encode_u32(meta + 24, storage->tree.root);
    encode_u32(meta + 32, location.page);
    encode_u32(meta + 36, storage->active_count + 1U);
    pager_commit(storage->pager);
    ++storage->record_count;
    ++storage->active_count;
    storage->last_data = location.page;
    return DB_OK;
rollback:
    pager_rollback(storage->pager);
    storage->tree = saved_tree;
    return status;
}

typedef struct { Storage *storage; RecordVisitor visitor; void *context; } Scan;

static DbStatus visit_record(uint32_t key, RecordLocation location, void *context)
{
    Scan *scan = context;
    Record record;
    bool deleted;
    DbStatus status = read_record(scan->storage, key, location, &record, &deleted);
    if (status != DB_OK || deleted) return status;
    return scan->visitor(&record, scan->context);
}

DbStatus storage_scan(Storage *storage, RecordVisitor visitor, void *context)
{
    Scan scan = {storage, visitor, context};
    return btree_scan(&storage->tree, visit_record, &scan);
}

DbStatus storage_validate(Storage *storage, StorageStats *output)
{
    BTreeStats stats;
    DbStatus status = validate_storage(storage, &stats);
    if (status != DB_OK) return status;
    *output = (StorageStats){
        .active_records = storage->active_count,
        .deleted_records = storage->record_count - storage->active_count,
        .records = storage->record_count, .pages = pager_page_count(storage->pager),
        .tree = stats, .leaf_splits = storage->tree.leaf_splits,
        .internal_splits = storage->tree.internal_splits, .root_splits = storage->tree.root_splits
    };
    return DB_OK;
}

DbStatus storage_inspect(Storage *storage, BTreeNodeVisitor visitor, void *context)
{
    return btree_inspect(&storage->tree, visitor, context);
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
