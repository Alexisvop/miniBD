#ifndef MINIDB_PAGER_H
#define MINIDB_PAGER_H

#include "status.h"
#include <stdint.h>

#define MINIDB_PAGE_SIZE 4096U
#define MINIDB_MAX_PAGES 4096U

typedef struct Pager Pager;

/* Single-process ownership. No locking. Empty files have zero pages.
 * Open failure sets *output to NULL; no existing content is truncated. */
DbStatus pager_open(const char *path, Pager **output);
uint32_t pager_page_count(const Pager *pager);
/* Borrowed page buffers remain valid until close/discard; never free them. */
DbStatus pager_get(Pager *pager, uint32_t id, const uint8_t **output);
DbStatus pager_write(Pager *pager, uint32_t id, uint8_t **output);
DbStatus pager_allocate(Pager *pager, uint32_t *id, uint8_t **output);
/* Writes dirty data pages, then page zero, then fsync. Not transactional.
 * A failed sync poisons the pager: no further accesses or write retries. */
DbStatus pager_sync(Pager *pager);
/* In-memory savepoint for one logical mutation. No disk transaction or nesting.
 * Rollback restores bytes/dirty flags and removes pages allocated since begin.
 * Mutable buffers must be acquired through pager_write AFTER begin. */
DbStatus pager_begin(Pager *pager);
void pager_commit(Pager *pager);
void pager_rollback(Pager *pager);
/* Both consume the handle and free all memory even on error. */
DbStatus pager_close(Pager *pager);
DbStatus pager_discard(Pager *pager); /* Close without writing, e.g. invalid file. */

#endif
