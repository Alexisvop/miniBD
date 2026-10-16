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
/* Fault flags are read by linker --wrap callbacks reached through other translation
 * units. Keep the injected state observable even under Release optimization. */
static volatile bool fail_read;
static volatile unsigned int injected_read_failures;
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
        ++injected_read_failures;
        errno = EIO;
        return -1;
    }
    if (transfer_limit != 0 && count > transfer_limit) {
        count = transfer_limit;
    }
    return __real_pread(fd, buffer, count, offset);
}

/* On glibc with _FORTIFY_SOURCE, optimized pread() calls may become
 * __pread_chk(). Intercept that variant too in this test-only binary. */
ssize_t __wrap___pread_chk(int fd, void *buffer, size_t count, off_t offset, size_t buffer_size)
{
    if (count > buffer_size) {
        errno = EOVERFLOW;
        return -1;
    }
    return __wrap_pread(fd, buffer, count, offset);
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

static uint8_t *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL && fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    CHECK(size >= 0 && fseek(file, 0, SEEK_SET) == 0);
    *length = (size_t)size;
    uint8_t *bytes = malloc(*length);
    CHECK(bytes != NULL && fread(bytes, 1, *length, file) == *length);
    CHECK(fclose(file) == 0);
    return bytes;
}

static void test_insert_rollback(uint32_t count)
{
    char path[64];
    temporary_path(path);
    Storage *storage;
    CHECK(storage_open(path, &storage) == DB_OK);
    for (uint32_t i = 0; i < count; ++i) {
        Record row = {i, "Rollback", 42};
        CHECK(storage_insert(storage, &row) == DB_OK);
    }
    CHECK(storage_close(storage) == DB_OK);
    size_t baseline_size;
    uint8_t *baseline = read_file(path, &baseline_size);
    bool succeeded = false;
    unsigned int failed_allocations = 0;
    for (int budget = 0; budget < 20; ++budget) {
        CHECK(storage_open(path, &storage) == DB_OK);
        StorageStats before, after;
        CHECK(storage_validate(storage, &before) == DB_OK);
        Record row = {count, "Rollback", 42};
        allocation_budget = budget;
        DbStatus result = storage_insert(storage, &row);
        allocation_budget = -1;
        CHECK(storage_validate(storage, &after) == DB_OK);
        if (result == DB_OK) {
            CHECK(after.records == count + 1U);
            if (count == 340) CHECK(after.leaf_splits == 1 && after.root_splits == 1);
            if (count == 86870) CHECK(after.internal_splits == 1 && after.root_splits == 1);
            CHECK(storage_close(storage) == DB_OK);
            succeeded = true;
            break;
        }
        CHECK(result == DB_NO_MEMORY);
        ++failed_allocations;
        CHECK(after.records == count && after.pages == before.pages);
        CHECK(after.tree.height == before.tree.height && after.tree.leaves == before.tree.leaves);
        CHECK(after.leaf_splits == 0 && after.internal_splits == 0 && after.root_splits == 0);
        CHECK(storage_select(storage, count, &row) == DB_NOT_FOUND);
        if (count > 0) CHECK(storage_select(storage, count - 1U, &row) == DB_OK);
        CHECK(storage_close(storage) == DB_OK);
        size_t actual_size;
        uint8_t *actual = read_file(path, &actual_size);
        CHECK(actual_size == baseline_size && memcmp(actual, baseline, actual_size) == 0);
        free(actual);
    }
    CHECK(succeeded && failed_allocations >= 3);
    free(baseline);
    CHECK(unlink(path) == 0);
    printf("Rollback: %u prior records, %u allocation failure positions verified.\n", count, failed_allocations);
}

static void test_change_failure(unsigned int operation)
{
    char path[64];
    temporary_path(path);
    Storage *storage;
    CHECK(storage_open(path, &storage) == DB_OK);
    Record row = {1, "Before", 21};
    CHECK(storage_insert(storage, &row) == DB_OK);
    if (operation == 2) CHECK(storage_delete(storage, 1) == DB_OK);
    CHECK(storage_close(storage) == DB_OK);
    size_t baseline_size;
    uint8_t *baseline = read_file(path, &baseline_size);
    int allocations = operation == 0 ? 1 : 2;
    for (int budget = 0; budget < allocations; ++budget) {
        CHECK(storage_open(path, &storage) == DB_OK);
        row = (Record){1, "After", 150};
        allocation_budget = budget;
        DbStatus status = operation == 0 ? storage_update(storage, &row) :
                          operation == 1 ? storage_delete(storage, 1) : storage_insert(storage, &row);
        allocation_budget = -1;
        CHECK(status == DB_NO_MEMORY);
        StorageStats stats;
        CHECK(storage_validate(storage, &stats) == DB_OK && stats.records == 1);
        CHECK(stats.active_records == (operation == 2 ? 0U : 1U));
        CHECK(storage_select(storage, 1, &row) == (operation == 2 ? DB_DELETED : DB_OK));
        if (operation != 2) CHECK(strcmp(row.name, "Before") == 0 && row.age == 21);
        CHECK(storage_close(storage) == DB_OK);
        size_t actual_size;
        uint8_t *actual = read_file(path, &actual_size);
        CHECK(actual_size == baseline_size && memcmp(actual, baseline, actual_size) == 0);
        free(actual);
    }
    free(baseline);
    CHECK(storage_open(path, &storage) == DB_OK);
    row = (Record){1, "After", 150};
    CHECK((operation == 0 ? storage_update(storage, &row) :
           operation == 1 ? storage_delete(storage, 1) : storage_insert(storage, &row)) == DB_OK);
    /* Disk failures occur at sync/close, not when changing cache bytes. */
    if (operation == 0) write_budget = 0;
    else fail_sync = true;
    CHECK(storage_sync(storage) == DB_IO_ERROR);
    write_budget = -1; fail_sync = false;
    unsigned int previous_writes = writes;
    CHECK(storage_update(storage, &row) == DB_IO_ERROR);
    CHECK(storage_delete(storage, 1) == DB_IO_ERROR);
    CHECK(storage_insert(storage, &row) == DB_IO_ERROR);
    CHECK(storage_close(storage) == DB_IO_ERROR && writes == previous_writes);
    CHECK(unlink(path) == 0);
}

int main(void)
{
    char path[64];
    temporary_path(path);
    Storage *storage;
    for (int budget = 0; budget < 4; ++budget) {
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

    /* Existing DB: Storage, Pager, metadata, leaf, data page. */
    for (int budget = 0; budget < 5; ++budget) {
        allocation_budget = budget;
        CHECK(storage_open(path, &storage) == DB_NO_MEMORY && storage == NULL);
        allocation_budget = -1;
    }
    injected_read_failures = 0;
    fail_read = true;
    CHECK(storage_open(path, &storage) == DB_IO_ERROR && storage == NULL);
    CHECK(injected_read_failures > 0); /* Ensure the fault reached the I/O wrapper. */
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
    /* Rollback must preserve dirty flags from earlier, unsynchronized inserts. */
    temporary_path(path);
    CHECK(storage_open(path, &storage) == DB_OK);
    row = (Record){10, "Previously accepted", 42};
    CHECK(storage_insert(storage, &row) == DB_OK);
    row.id = 11;
    allocation_budget = 2;
    CHECK(storage_insert(storage, &row) == DB_NO_MEMORY);
    allocation_budget = -1;
    CHECK(storage_close(storage) == DB_OK);
    CHECK(storage_open(path, &storage) == DB_OK);
    CHECK(storage_select(storage, 10, &row) == DB_OK);
    CHECK(storage_select(storage, 11, &row) == DB_NOT_FOUND);
    CHECK(storage_record_count(storage) == 1);
    CHECK(storage_close(storage) == DB_OK);
    CHECK(unlink(path) == 0);
    for (unsigned int operation = 0; operation < 3; ++operation) test_change_failure(operation);
    test_insert_rollback(0);
    test_insert_rollback(340);
    test_insert_rollback(348);
    test_insert_rollback(86870);
    puts("Failures: allocation, permissions, short I/O, EINTR, ENOSPC, sync and close passed.");
    return 0;
}
