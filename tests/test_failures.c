#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include "test_support.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/types.h>

/* Link-time wrappers affect references in this test/core, not libc internals. */
void *__real_calloc(size_t count, size_t size);
void *__real_malloc(size_t size);
int __real_open(const char *path, int flags, ...);
ssize_t __real_pread(int fd, void *buffer, size_t count, off_t offset);
ssize_t __real_pwrite(int fd, const void *buffer, size_t count, off_t offset);
int __real_fsync(int fd);
int __real_close(int fd);

static int allocation_budget = -1;
static int write_budget = -1;
static bool deny_open;
static bool fail_read;
static bool fail_sync;
static bool fail_close;
static bool zero_write;
static bool interrupt_read;
static bool interrupt_write;
static bool interrupt_sync;
static size_t transfer_limit;
static unsigned int writes;
static unsigned int syncs;

static bool allocation_fails(void)
{
    if (allocation_budget < 0) {
        return false;
    }
    if (allocation_budget == 0) {
        return true;
    }
    --allocation_budget;
    return false;
}

void *__wrap_calloc(size_t count, size_t size)
{
    return allocation_fails() ? NULL : __real_calloc(count, size);
}

void *__wrap_malloc(size_t size)
{
    return allocation_fails() ? NULL : __real_malloc(size);
}

int __wrap_open(const char *path, int flags, ...)
{
    if (deny_open) {
        errno = EACCES;
        return -1;
    }
    if ((flags & O_CREAT) != 0) {
        va_list args;
        va_start(args, flags);
        int mode = va_arg(args, int);
        va_end(args);
        return __real_open(path, flags, mode);
    }
    return __real_open(path, flags);
}

ssize_t __wrap_pread(int fd, void *buffer, size_t count, off_t offset)
{
    if (interrupt_read) {
        interrupt_read = false;
        errno = EINTR;
        return -1;
    }
    if (fail_read) {
        errno = EIO;
        return -1;
    }
    if (transfer_limit != 0 && count > transfer_limit) {
        count = transfer_limit;
    }
    return __real_pread(fd, buffer, count, offset);
}

ssize_t __wrap_pwrite(int fd, const void *buffer, size_t count, off_t offset)
{
    ++writes;
    if (interrupt_write) {
        interrupt_write = false;
        errno = EINTR;
        return -1;
    }
    if (write_budget == 0) {
        errno = ENOSPC;
        return -1;
    }
    if (write_budget > 0) {
        --write_budget;
    }
    if (zero_write) {
        return 0;
    }
    if (transfer_limit != 0 && count > transfer_limit) {
        count = transfer_limit;
    }
    return __real_pwrite(fd, buffer, count, offset);
}

int __wrap_fsync(int fd)
{
    ++syncs;
    if (interrupt_sync) {
        interrupt_sync = false;
        errno = EINTR;
        return -1;
    }
    if (fail_sync) {
        errno = EIO;
        return -1;
    }
    return __real_fsync(fd);
}

int __wrap_close(int fd)
{
    int result = __real_close(fd);
    if (fail_close) {
        errno = EIO;
        return -1;
    }
    return result;
}

static DbStatus unused_visitor(const Record *record, void *context)
{
    (void)record;
    (void)context;
    CHECK(false);
    return DB_OK;
}

int main(void)
{
    char path[64];
    temporary_path(path);
    Storage *storage;
    for (int budget = 0; budget < 3; ++budget) {
        allocation_budget = budget;
        CHECK(storage_open(path, &storage) == DB_NO_MEMORY && storage == NULL);
        allocation_budget = -1;
    }
    deny_open = true;
    CHECK(storage_open(path, &storage) == DB_PERMISSION_DENIED && storage == NULL);
    deny_open = false;

    CHECK(storage_open(path, &storage) == DB_OK);
    Record row = {1, "Fault testing", 42};
    allocation_budget = 0;
    CHECK(storage_insert(storage, &row) == DB_NO_MEMORY);
    allocation_budget = -1;
    CHECK(storage_record_count(storage) == 0);
    CHECK(storage_insert(storage, &row) == DB_OK);
    interrupt_write = true;
    interrupt_sync = true;
    transfer_limit = 31;
    CHECK(storage_close(storage) == DB_OK);
    CHECK(!interrupt_write && !interrupt_sync);

    /* Existing DB: Storage, Pager, metadata, temporary IDs, data page. */
    for (int budget = 0; budget < 5; ++budget) {
        allocation_budget = budget;
        CHECK(storage_open(path, &storage) == DB_NO_MEMORY && storage == NULL);
        allocation_budget = -1;
    }
    fail_read = true;
    CHECK(storage_open(path, &storage) == DB_IO_ERROR && storage == NULL);
    fail_read = false;
    interrupt_read = true;
    CHECK(storage_open(path, &storage) == DB_OK && !interrupt_read);
    CHECK(storage_select(storage, 1, &row) == DB_OK);
    unsigned int previous_writes = writes;
    unsigned int previous_syncs = syncs;
    CHECK(storage_close(storage) == DB_OK);
    CHECK(writes == previous_writes && syncs == previous_syncs);
    transfer_limit = 0;

    /* A later fsync failure must propagate, poison access and prevent retries. */
    CHECK(storage_open(path, &storage) == DB_OK);
    row.id = 2;
    CHECK(storage_insert(storage, &row) == DB_OK);
    fail_sync = true;
    CHECK(storage_sync(storage) == DB_IO_ERROR);
    fail_sync = false;
    previous_writes = writes;
    previous_syncs = syncs;
    CHECK(storage_insert(storage, &row) == DB_IO_ERROR);
    CHECK(storage_select(storage, 1, &row) == DB_IO_ERROR);
    CHECK(storage_scan(storage, unused_visitor, NULL) == DB_IO_ERROR);
    CHECK(storage_close(storage) == DB_IO_ERROR);
    CHECK(writes == previous_writes && syncs == previous_syncs);

    /* Partial-write failure and a zero-byte write are both fatal. */
    for (unsigned int attempt = 0; attempt < 2; ++attempt) {
        CHECK(truncate(path, 0) == 0);
        CHECK(storage_open(path, &storage) == DB_OK);
        CHECK(storage_insert(storage, &row) == DB_OK);
        transfer_limit = 31;
        write_budget = attempt == 0 ? 1 : -1;
        zero_write = attempt == 1;
        CHECK(storage_sync(storage) == DB_IO_ERROR);
        previous_writes = writes;
        write_budget = -1;
        zero_write = false;
        transfer_limit = 0;
        CHECK(storage_close(storage) == DB_IO_ERROR);
        CHECK(writes == previous_writes);
    }

    CHECK(truncate(path, 0) == 0);
    write_budget = 0;
    CHECK(storage_open(path, &storage) == DB_IO_ERROR && storage == NULL);
    write_budget = -1;
    CHECK(storage_open(path, &storage) == DB_OK);
    CHECK(storage_insert(storage, &row) == DB_OK);
    write_budget = 0;
    CHECK(storage_close(storage) == DB_IO_ERROR);
    write_budget = -1;

    CHECK(truncate(path, 0) == 0);
    CHECK(storage_open(path, &storage) == DB_OK);
    fail_close = true;
    CHECK(storage_close(storage) == DB_IO_ERROR);
    fail_close = false;
    CHECK(unlink(path) == 0);
    puts("Failures: allocation, permissions, short I/O, EINTR, ENOSPC, sync and close passed.");
    return 0;
}
