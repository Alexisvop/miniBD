#define _POSIX_C_SOURCE 200809L
#include "executor.h"
#include "encoding.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define QUERIES 2000U
#define SCANS 20U
#define TRIALS 5U

#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "Benchmark check failed at line %d: %s\n", __LINE__, #condition); \
    exit(EXIT_FAILURE); } } while (0)

static double now(void)
{
    struct timespec stamp;
    REQUIRE(clock_gettime(CLOCK_MONOTONIC, &stamp) == 0);
    return (double)stamp.tv_sec + (double)stamp.tv_nsec / 1e9;
}

static uint64_t checksum(const Record *row)
{
    uint64_t sum = (uint64_t)row->id * 151U + row->age;
    for (const unsigned char *p = (const unsigned char *)row->name; *p != 0; ++p)
        sum = sum * 33U + *p; /* Defined unsigned wraparound. */
    return sum;
}

static void emit(uint32_t count, uint32_t trial, const char *operation,
                 uint32_t operations, double elapsed, uint64_t sum)
{
    if (trial != 0)
        REQUIRE(printf("%u,%u,%s,%u,%.6f,%" PRIu64 "\n", count, trial, operation,
                       operations, elapsed * 1000.0, sum) > 0);
}

/* Benchmark-only scan of the same v3 file, not an alternative production API. */
static DbStatus sequential_select(Pager *pager, uint32_t key, Record *row)
{
    for (uint32_t id = 1; id < pager_page_count(pager); ++id) {
        const uint8_t *page;
        DbStatus status = pager_get(pager, id, &page);
        if (status != DB_OK) return status;
        if (decode_u32(page) != 2U) continue;
        for (uint32_t slot = 0; slot < decode_u32(page + 4); ++slot) {
            const uint8_t *bytes = page + 16U + slot * MINIDB_RECORD_SIZE;
            if (decode_u32(bytes) == key) {
                if ((page[8U + slot / 8U] & (uint8_t)(1U << (slot % 8U))) != 0) return DB_DELETED;
                return record_deserialize(bytes, row) ? DB_OK : DB_INVALID_FORMAT;
            }
        }
    }
    return DB_NOT_FOUND;
}

static double lookup(Storage *storage, Pager *pager, const uint32_t *queries,
                     bool indexed, uint64_t expected_sum)
{
    uint64_t sum = 0;
    double start = now();
    for (uint32_t i = 0; i < QUERIES; ++i) {
        Record row;
        DbStatus status = indexed ? storage_select(storage, queries[i], &row) :
                                    sequential_select(pager, queries[i], &row);
        REQUIRE(status == (queries[i] % 2U == 0 ? DB_OK : DB_NOT_FOUND));
        sum += status == DB_OK ? checksum(&row) : UINT32_MAX;
    }
    double elapsed = now() - start;
    REQUIRE(sum == expected_sum);
    return elapsed;
}

typedef struct { uint64_t sum; uint32_t count; } ScanSum;
static DbStatus consume(const Record *row, void *context)
{
    ScanSum *sum = context;
    sum->sum += checksum(row);
    ++sum->count;
    return DB_OK;
}

static double scan(Storage *storage, bool filtered, uint32_t repetitions,
                   uint32_t expected_count, uint64_t expected_sum)
{
    Command command = {.type = COMMAND_SELECT_ALL,
        .filter = {filtered ? AGE_FILTER_GE : AGE_FILTER_NONE, 75}};
    ScanSum sum = {0};
    ExecutionSink sink = {.record = consume, .context = &sum};
    double start = now();
    for (uint32_t i = 0; i < repetitions; ++i) {
        ExecutionResult result = executor_execute(storage, &command, &sink);
        REQUIRE(result.status == DB_OK && result.rows == expected_count);
    }
    double elapsed = now() - start;
    REQUIRE(sum.count == expected_count * repetitions && sum.sum == expected_sum * repetitions);
    return elapsed;
}

static void verify(Storage *storage, const Record *model, uint32_t count, bool deleted)
{
    uint32_t active = 0;
    for (uint32_t i = 0; i < count; ++i) {
        Record row;
        bool hidden = deleted && i % 4U == 0;
        REQUIRE(storage_select(storage, i * 2U, &row) == (hidden ? DB_DELETED : DB_OK));
        if (!hidden) {
            REQUIRE(row.id == model[i].id && row.age == model[i].age && strcmp(row.name, model[i].name) == 0);
            ++active;
        }
    }
    StorageStats stats;
    REQUIRE(storage_validate(storage, &stats) == DB_OK);
    REQUIRE(stats.records == count && stats.tree.entries == count && stats.active_records == active);
}

static void trial(uint32_t count, uint32_t number, const uint32_t *order,
                  const uint32_t *queries, uint64_t expected_lookup)
{
    char path[] = "/tmp/minidb-engine-benchmark-XXXXXX";
    int fd = mkstemp(path);
    REQUIRE(fd >= 0 && close(fd) == 0);
    Record *model = calloc(count, sizeof(*model));
    REQUIRE(model != NULL);
    for (uint32_t i = 0; i < count; ++i)
        model[i] = (Record){i * 2U, "Benchmark", (uint8_t)(i % 151U)};
    Storage *storage;
    REQUIRE(storage_open(path, &storage) == DB_OK);
    double start = now();
    for (uint32_t i = 0; i < count; ++i) REQUIRE(storage_insert(storage, &model[order[i]]) == DB_OK);
    emit(count, number, "insert", count, now() - start, count);
    verify(storage, model, count, false);
    start = now();
    REQUIRE(storage_sync(storage) == DB_OK);
    emit(count, number, "sync_insert", 1, now() - start, count);
    REQUIRE(storage_close(storage) == DB_OK);
    start = now();
    REQUIRE(storage_open(path, &storage) == DB_OK);
    emit(count, number, "open_validate", 1, now() - start, count);
    Pager *pager;
    REQUIRE(pager_open(path, &pager) == DB_OK);
    for (uint32_t i = 0; i < pager_page_count(pager); ++i) {
        const uint8_t *page;
        REQUIRE(pager_get(pager, i, &page) == DB_OK);
    }
    /* Full output comparisons are outside the timed lookup loops. */
    for (uint32_t i = 0; i < QUERIES; ++i) {
        Record a, b;
        DbStatus x = storage_select(storage, queries[i], &a);
        DbStatus y = sequential_select(pager, queries[i], &b);
        REQUIRE(x == y);
        if (x == DB_OK) REQUIRE(a.id == b.id && a.age == b.age && strcmp(a.name, b.name) == 0);
    }
    (void)lookup(storage, pager, queries, true, expected_lookup);
    (void)lookup(storage, pager, queries, false, expected_lookup);
    for (uint32_t method = 0; method < 2U; ++method) {
        bool indexed = (method + number) % 2U == 0;
        double elapsed = lookup(storage, pager, queries, indexed, expected_lookup);
        emit(count, number, indexed ? "indexed_lookup" : "sequential_lookup", QUERIES, elapsed, expected_lookup);
    }
    REQUIRE(pager_close(pager) == DB_OK); /* No stale secondary cache during mutations. */
    for (uint32_t i = 0; i < count; ++i) {
        strcpy(model[i].name, "Updated");
        model[i].age = (uint8_t)((i + 31U) % 151U);
    }
    start = now();
    for (uint32_t i = 0; i < count; ++i) REQUIRE(storage_update(storage, &model[order[i]]) == DB_OK);
    emit(count, number, "update", count, now() - start, count);
    verify(storage, model, count, false);
    start = now();
    for (uint32_t i = 0; i < count; i += 4U) REQUIRE(storage_delete(storage, i * 2U) == DB_OK);
    emit(count, number, "logical_delete", (count + 3U) / 4U, now() - start, (count + 3U) / 4U);
    verify(storage, model, count, true);
    uint64_t sums[2] = {0};
    uint32_t counts[2] = {0};
    for (uint32_t i = 0; i < count; ++i) if (i % 4U != 0) {
        sums[0] += checksum(&model[i]); ++counts[0];
        if (model[i].age >= 75U) { sums[1] += checksum(&model[i]); ++counts[1]; }
    }
    for (uint32_t method = 0; method < 2U; ++method) {
        uint32_t filter = (method + number) % 2U;
        (void)scan(storage, filter != 0, 1, counts[filter], sums[filter]);
        double elapsed = scan(storage, filter != 0, SCANS, counts[filter], sums[filter]);
        emit(count, number, filter != 0 ? "age_filter" : "ordered_scan", SCANS, elapsed, sums[filter] * SCANS);
    }
    start = now();
    for (uint32_t i = 0; i < count; i += 4U) REQUIRE(storage_insert(storage, &model[i]) == DB_OK);
    emit(count, number, "reactivate", (count + 3U) / 4U, now() - start, (count + 3U) / 4U);
    start = now();
    REQUIRE(storage_sync(storage) == DB_OK);
    emit(count, number, "sync_changes", 1, now() - start, count);
    REQUIRE(storage_close(storage) == DB_OK);
    REQUIRE(storage_open(path, &storage) == DB_OK);
    verify(storage, model, count, false);
    REQUIRE(storage_close(storage) == DB_OK && unlink(path) == 0);
    free(model);
}

int main(void)
{
    REQUIRE(puts("records,trial,operation,operations,elapsed_ms,checksum") >= 0);
    const uint32_t sizes[] = {1000, 10000, 50000, 100000};
    for (size_t size = 0; size < sizeof(sizes) / sizeof(sizes[0]); ++size) {
        uint32_t count = sizes[size];
        uint32_t *order = malloc((size_t)count * sizeof(*order));
        REQUIRE(order != NULL);
        for (uint32_t i = 0; i < count; ++i) order[i] = i;
        uint32_t random = 0x12345678U;
        for (uint32_t i = count - 1U; i > 0; --i) {
            random = random * 1664525U + 1013904223U;
            uint32_t j = random % (i + 1U), swap = order[i];
            order[i] = order[j]; order[j] = swap;
        }
        uint32_t queries[QUERIES];
        uint64_t expected_lookup = 0;
        random = 0xabcdef01U;
        for (uint32_t i = 0; i < QUERIES; ++i) {
            random = random * 1664525U + 1013904223U;
            queries[i] = (random % count) * 2U + (i % 4U == 0 ? 1U : 0U);
            Record row = {queries[i], "Benchmark", (uint8_t)((queries[i] / 2U) % 151U)};
            expected_lookup += i % 4U == 0 ? UINT32_MAX : checksum(&row);
        }
        /* Trial zero warms the complete workload and is deliberately not emitted. */
        for (uint32_t number = 0; number <= TRIALS; ++number)
            trial(count, number, order, queries, expected_lookup);
        free(order);
    }
    return fflush(stdout) == EOF ? EXIT_FAILURE : EXIT_SUCCESS;
}
