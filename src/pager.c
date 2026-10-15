#define _POSIX_C_SOURCE 200809L
#include "pager.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    uint8_t bytes[MINIDB_PAGE_SIZE];
    bool dirty;
} CachedPage;

struct Pager {
    int fd;
    uint32_t page_count;
    bool failed;
    CachedPage *pages[MINIDB_MAX_PAGES];
};

static DbStatus io_status(void)
{
    return errno == EACCES || errno == EPERM || errno == EROFS
        ? DB_PERMISSION_DENIED : DB_IO_ERROR;
}

DbStatus pager_open(const char *path, Pager **output)
{
    *output = NULL;
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NONBLOCK, 0600);
    if (fd < 0) {
        return io_status();
    }
    struct stat info;
    DbStatus status = DB_OK;
    if (fstat(fd, &info) < 0) {
        status = DB_IO_ERROR;
    } else if (!S_ISREG(info.st_mode) || info.st_size < 0 ||
               info.st_size % (off_t)MINIDB_PAGE_SIZE != 0) {
        status = DB_INVALID_FORMAT;
    } else if ((uintmax_t)info.st_size / MINIDB_PAGE_SIZE > MINIDB_MAX_PAGES) {
        status = DB_LIMIT_REACHED;
    }
    if (status != DB_OK) {
        if (close(fd) < 0) {
            return DB_IO_ERROR;
        }
        return status;
    }
    Pager *pager = calloc(1, sizeof(*pager));
    if (pager == NULL) {
        return close(fd) < 0 ? DB_IO_ERROR : DB_NO_MEMORY;
    }
    pager->fd = fd;
    pager->page_count = (uint32_t)((uintmax_t)info.st_size / MINIDB_PAGE_SIZE);
    *output = pager;
    return DB_OK;
}

uint32_t pager_page_count(const Pager *pager)
{
    return pager->page_count;
}

static DbStatus load_page(Pager *pager, uint32_t id)
{
    if (pager->failed) {
        return DB_IO_ERROR;
    }
    if (id >= pager->page_count) {
        return DB_LIMIT_REACHED;
    }
    if (pager->pages[id] != NULL) {
        return DB_OK;
    }
    CachedPage *page = calloc(1, sizeof(*page));
    if (page == NULL) {
        return DB_NO_MEMORY;
    }
    size_t done = 0;
    while (done < MINIDB_PAGE_SIZE) {
        off_t offset = (off_t)id * (off_t)MINIDB_PAGE_SIZE + (off_t)done;
        ssize_t count = pread(pager->fd, page->bytes + done, MINIDB_PAGE_SIZE - done, offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            free(page);
            return count == 0 ? DB_INVALID_FORMAT : DB_IO_ERROR;
        }
        done += (size_t)count;
    }
    pager->pages[id] = page;
    return DB_OK;
}

DbStatus pager_get(Pager *pager, uint32_t id, const uint8_t **output)
{
    DbStatus status = load_page(pager, id);
    if (status == DB_OK) {
        *output = pager->pages[id]->bytes;
    }
    return status;
}

DbStatus pager_write(Pager *pager, uint32_t id, uint8_t **output)
{
    DbStatus status = load_page(pager, id);
    if (status == DB_OK) {
        pager->pages[id]->dirty = true;
        *output = pager->pages[id]->bytes;
    }
    return status;
}

DbStatus pager_allocate(Pager *pager, uint32_t *id, uint8_t **output)
{
    if (pager->failed) {
        return DB_IO_ERROR;
    }
    if (pager->page_count == MINIDB_MAX_PAGES) {
        return DB_LIMIT_REACHED;
    }
    CachedPage *page = calloc(1, sizeof(*page));
    if (page == NULL) {
        return DB_NO_MEMORY;
    }
    page->dirty = true;
    *id = pager->page_count++;
    pager->pages[*id] = page;
    *output = page->bytes;
    return DB_OK;
}

static DbStatus flush_page(Pager *pager, uint32_t id)
{
    CachedPage *page = pager->pages[id];
    if (page == NULL || !page->dirty) {
        return DB_OK;
    }
    size_t done = 0;
    while (done < MINIDB_PAGE_SIZE) {
        off_t offset = (off_t)id * (off_t)MINIDB_PAGE_SIZE + (off_t)done;
        ssize_t count = pwrite(pager->fd, page->bytes + done, MINIDB_PAGE_SIZE - done, offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return DB_IO_ERROR;
        }
        done += (size_t)count;
    }
    return DB_OK;
}

DbStatus pager_sync(Pager *pager)
{
    if (pager->failed) {
        return DB_IO_ERROR;
    }
    bool dirty = false;
    /* Metadata last is a convention, not a guarantee of crash atomicity. */
    for (uint32_t i = 1; i <= pager->page_count; ++i) {
        uint32_t id = i == pager->page_count ? 0 : i;
        if (pager->pages[id] == NULL || !pager->pages[id]->dirty) {
            continue;
        }
        dirty = true;
        if (flush_page(pager, id) != DB_OK) {
            pager->failed = true;
            return DB_IO_ERROR;
        }
    }
    if (dirty) {
        int result;
        do {
            result = fsync(pager->fd);
        } while (result < 0 && errno == EINTR);
        if (result < 0) {
            pager->failed = true;
            return DB_IO_ERROR;
        }
        for (uint32_t i = 0; i < pager->page_count; ++i) {
            if (pager->pages[i] != NULL) {
                pager->pages[i]->dirty = false;
            }
        }
    }
    return DB_OK;
}

DbStatus pager_discard(Pager *pager)
{
    DbStatus status = close(pager->fd) < 0 ? DB_IO_ERROR : DB_OK;
    for (uint32_t i = 0; i < pager->page_count; ++i) {
        free(pager->pages[i]);
    }
    free(pager);
    return status;
}

DbStatus pager_close(Pager *pager)
{
    DbStatus status = pager_sync(pager);
    DbStatus close_status = pager_discard(pager);
    return status == DB_OK ? close_status : status;
}
