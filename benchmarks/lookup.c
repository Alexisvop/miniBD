#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include "encoding.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define QUERIES 2000U
#define TRIALS 5U

static void require_status(DbStatus status)
{
    if (status != DB_OK) {
        fprintf(stderr, "Benchmark failed: %s\n", db_status_message(status));
        exit(EXIT_FAILURE);
    }
}

static double now(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (double)time.tv_sec + (double)time.tv_nsec / 1e9;
}

/* Benchmark-only phase-2 scan, adapted to skip mixed index pages. */
static DbStatus sequential_select(Pager *pager, uint32_t key, Record *record)
{
    for (uint32_t id = 1; id < pager_page_count(pager); ++id) {
        const uint8_t *page;
        DbStatus status = pager_get(pager, id, &page);
        if (status != DB_OK) return status;
        if (decode_u32(page) != 2) continue;
        for (uint32_t slot = 0; slot < decode_u32(page + 4); ++slot) {
            const uint8_t *bytes = page + 16U + slot * MINIDB_RECORD_SIZE;
            if (decode_u32(bytes) == key) return record_deserialize(bytes, record) ? DB_OK : DB_INVALID_FORMAT;
        }
    }
    return DB_NOT_FOUND;
}

static double measure(Storage *storage, Pager *pager, const uint32_t *queries, bool indexed, uint64_t *sum)
{
    uint64_t checksum = 0;
    double start = now();
    for (uint32_t i = 0; i < QUERIES; ++i) {
        Record record;
        DbStatus status = indexed ? storage_select(storage, queries[i], &record)
                                  : sequential_select(pager, queries[i], &record);
        if (status == DB_OK) checksum += (uint64_t)record.id + record.age + strlen(record.name);
        else if (status == DB_NOT_FOUND) checksum += UINT32_MAX;
        else require_status(status);
    }
    double elapsed = now() - start;
    *sum = checksum;
    return elapsed * 1000.0;
}

static int compare_double(const void *left, const void *right)
{
    double a = *(const double *)left, b = *(const double *)right;
    return (a > b) - (a < b);
}

static void benchmark(uint32_t count)
{
    char path[] = "/tmp/minidb-benchmark-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0 || close(fd) != 0) {
        perror("temporary file");
        exit(EXIT_FAILURE);
    }
    Storage *storage;
    require_status(storage_open(path, &storage));
    uint32_t *keys = malloc((size_t)count * sizeof(*keys));
    if (keys == NULL) require_status(DB_NO_MEMORY);
    for (uint32_t i = 0; i < count; ++i) keys[i] = i * 2U;
    uint32_t random = 0x12345678U;
    for (uint32_t i = count - 1U; i > 0; --i) {
        random = random * 1664525U + 1013904223U;
        uint32_t j = random % (i + 1U), swap = keys[i]; keys[i] = keys[j]; keys[j] = swap;
    }
    for (uint32_t i = 0; i < count; ++i) {
        Record record = {keys[i], "Benchmark", 42};
        require_status(storage_insert(storage, &record));
    }
    free(keys);
    require_status(storage_close(storage));
    require_status(storage_open(path, &storage));
    Pager *pager;
    require_status(pager_open(path, &pager));
    /* Both handles are exclusively read-only throughout measurement. */
    for (uint32_t i = 0; i < pager_page_count(pager); ++i) {
        const uint8_t *page;
        require_status(pager_get(pager, i, &page));
    }
    uint32_t queries[QUERIES];
    random = 0xabcdef01U;
    for (uint32_t i = 0; i < QUERIES; ++i) {
        random = random * 1664525U + 1013904223U;
        queries[i] = (random % count) * 2U + (i % 4U == 0 ? 1U : 0U);
    }
    double indexed[TRIALS], sequential[TRIALS];
    uint64_t index_sum, scan_sum;
    (void)measure(storage, pager, queries, true, &index_sum);
    (void)measure(storage, pager, queries, false, &scan_sum);
    if (index_sum != scan_sum) require_status(DB_INVALID_FORMAT);
    for (uint32_t trial = 0; trial < TRIALS; ++trial) {
        if (trial % 2U == 0) {
            indexed[trial] = measure(storage, pager, queries, true, &index_sum);
            sequential[trial] = measure(storage, pager, queries, false, &scan_sum);
        } else {
            sequential[trial] = measure(storage, pager, queries, false, &scan_sum);
            indexed[trial] = measure(storage, pager, queries, true, &index_sum);
        }
        if (index_sum != scan_sum) require_status(DB_INVALID_FORMAT);
    }
    qsort(indexed, TRIALS, sizeof(*indexed), compare_double);
    qsort(sequential, TRIALS, sizeof(*sequential), compare_double);
    printf("%u,%u,%u,%.6f,%.6f,%.2f,%" PRIu64 "\n", count, QUERIES, TRIALS,
           indexed[TRIALS / 2U], sequential[TRIALS / 2U],
           sequential[TRIALS / 2U] / indexed[TRIALS / 2U], index_sum);
    require_status(pager_close(pager));
    require_status(storage_close(storage));
    if (unlink(path) != 0) {
        perror("unlink");
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    puts("records,queries,trials,indexed_median_ms,sequential_median_ms,ratio,checksum");
    const uint32_t sizes[] = {1000, 10000, 100000};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) benchmark(sizes[i]);
    return fflush(stdout) == EOF ? EXIT_FAILURE : EXIT_SUCCESS;
}
