#define _POSIX_C_SOURCE 200809L
#include "pager.h"
#include "test_support.h"

#include <fcntl.h>
#include <sys/stat.h>

int main(void)
{
    char path[64];
    temporary_path(path);
    CHECK(unlink(path) == 0);
    Pager *pager;
    CHECK(pager_open(path, &pager) == DB_OK);
    CHECK(pager_page_count(pager) == 0);
    const uint8_t *read_page;
    CHECK(pager_get(pager, 0, &read_page) == DB_LIMIT_REACHED);
    uint8_t *page;
    uint32_t id;
    CHECK(pager_allocate(pager, &id, &page) == DB_OK && id == 0);
    for (size_t i = 0; i < MINIDB_PAGE_SIZE; ++i) {
        CHECK(page[i] == 0);
    }
    page[0] = 0xab;
    page[4095] = 0xcd;
    uint8_t *first = page;
    CHECK(pager_allocate(pager, &id, &page) == DB_OK && id == 1);
    page[0] = 0xef;
    CHECK(pager_get(pager, 0, &read_page) == DB_OK && read_page == first);
    struct stat info;
    CHECK(stat(path, &info) == 0 && info.st_size == 0);
    CHECK(pager_sync(pager) == DB_OK);
    CHECK(stat(path, &info) == 0 && info.st_size == 8192);
    CHECK(pager_sync(pager) == DB_OK);
    CHECK(pager_close(pager) == DB_OK);
    CHECK(pager_open(path, &pager) == DB_OK);
    CHECK(pager_page_count(pager) == 2);
    CHECK(pager_get(pager, 0, &read_page) == DB_OK);
    CHECK(read_page[0] == 0xab && read_page[4095] == 0xcd);
    CHECK(pager_get(pager, 1, &read_page) == DB_OK && read_page[0] == 0xef);
    CHECK(pager_write(pager, 1, &page) == DB_OK);
    page[10] = 42;
    CHECK(pager_close(pager) == DB_OK);
    CHECK(pager_open(path, &pager) == DB_OK);
    CHECK(pager_get(pager, 1, &read_page) == DB_OK && read_page[10] == 42);
    CHECK(pager_get(pager, 2, &read_page) == DB_LIMIT_REACHED);
    CHECK(pager_get(pager, UINT32_MAX, &read_page) == DB_LIMIT_REACHED);

    for (uint32_t i = 2; i < MINIDB_MAX_PAGES; ++i) {
        CHECK(pager_allocate(pager, &id, &page) == DB_OK && id == i);
    }
    CHECK(pager_allocate(pager, &id, &page) == DB_LIMIT_REACHED);
    CHECK(pager_page_count(pager) == MINIDB_MAX_PAGES);
    CHECK(pager_discard(pager) == DB_OK);
    CHECK(stat(path, &info) == 0 && info.st_size == 8192);

    CHECK(truncate(path, 4097) == 0);
    CHECK(pager_open(path, &pager) == DB_INVALID_FORMAT && pager == NULL);
    CHECK(truncate(path, (off_t)(MINIDB_MAX_PAGES + 1U) * MINIDB_PAGE_SIZE) == 0);
    CHECK(pager_open(path, &pager) == DB_LIMIT_REACHED && pager == NULL);
    CHECK(truncate(path, MINIDB_PAGE_SIZE) == 0);
    CHECK(pager_open(path, &pager) == DB_OK);
    /* Simulate a file truncated after fstat, before the first read. */
    CHECK(truncate(path, 0) == 0);
    CHECK(pager_get(pager, 0, &read_page) == DB_INVALID_FORMAT);
    CHECK(pager_discard(pager) == DB_OK);
    CHECK(unlink(path) == 0);
    CHECK(pager_open("/dev/null", &pager) == DB_INVALID_FORMAT);
    CHECK(pager_open("/dev/null/child.db", &pager) == DB_IO_ERROR);
    puts("Pager: caching, dirty writes, reopen, truncation and full page limit passed.");
    return 0;
}
