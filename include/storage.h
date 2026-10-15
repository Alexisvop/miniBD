#ifndef MINIDB_STORAGE_H
#define MINIDB_STORAGE_H

#include "pager.h"
#include "record.h"

#define MINIDB_FORMAT_VERSION 1U
#define MINIDB_DATA_HEADER_SIZE 16U
#define MINIDB_RECORDS_PER_PAGE ((MINIDB_PAGE_SIZE - MINIDB_DATA_HEADER_SIZE) / MINIDB_RECORD_SIZE)
#define MINIDB_MAX_RECORDS ((MINIDB_MAX_PAGES - 1U) * MINIDB_RECORDS_PER_PAGE)

typedef struct Storage Storage;
/* Visitor borrows a record for one call. Return an error to stop scanning.
 * Visitors must not mutate or close the storage being scanned. */
typedef DbStatus (*RecordVisitor)(const Record *record, void *context);

DbStatus storage_open(const char *path, Storage **output);
DbStatus storage_insert(Storage *storage, const Record *record);
DbStatus storage_select(Storage *storage, uint32_t id, Record *output);
/* Physical insertion order, not sorted by ID. */
DbStatus storage_scan(Storage *storage, RecordVisitor visitor, void *context);
uint32_t storage_record_count(const Storage *storage);
DbStatus storage_sync(Storage *storage);
/* Consumes the handle even if synchronization/close fails. */
DbStatus storage_close(Storage *storage);

#endif
