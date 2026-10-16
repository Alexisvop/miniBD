#define _POSIX_C_SOURCE 200809L
#include "executor.h"
#include "test_support.h"

static uint32_t key_count = 3000U;

typedef struct { bool exists, active; Record record; } ModelRow;
typedef struct { ModelRow *model; uint32_t next; uint32_t matches; AgeFilter filter; } ModelScan;

static bool expected_match(uint8_t age, AgeFilter filter)
{
    /* Independent table instead of the Executor's operator switch. */
    const bool comparison[] = {true, age == filter.value, age != filter.value, age < filter.value,
                               age <= filter.value, age > filter.value, age >= filter.value};
    return comparison[filter.op];
}

static DbStatus compare_record(const Record *record, void *context)
{
    ModelScan *scan = context;
    while (scan->next < key_count && (!scan->model[scan->next].active ||
           !expected_match(scan->model[scan->next].record.age, scan->filter))) ++scan->next;
    CHECK(scan->next < key_count);
    const Record *expected = &scan->model[scan->next].record;
    CHECK(record->id == expected->id && record->age == expected->age && strcmp(record->name, expected->name) == 0);
    ++scan->next;
    ++scan->matches;
    return DB_OK;
}

static void verify(Storage *storage, ModelRow *model)
{
    uint32_t total = 0, active = 0;
    for (uint32_t i = 0; i < key_count; ++i) {
        Record record;
        DbStatus expected = !model[i].exists ? DB_NOT_FOUND : model[i].active ? DB_OK : DB_DELETED;
        CHECK(storage_select(storage, i, &record) == expected);
        if (model[i].exists) ++total;
        if (model[i].active) {
            ++active;
            CHECK(record.age == model[i].record.age && strcmp(record.name, model[i].record.name) == 0);
        }
    }
    StorageStats stats;
    CHECK(storage_validate(storage, &stats) == DB_OK);
    CHECK(stats.records == total && stats.tree.entries == total && stats.active_records == active);
    CHECK(stats.deleted_records == total - active);
    for (unsigned int op = AGE_FILTER_NONE; op <= AGE_FILTER_GE; ++op) {
        AgeFilter filter = {(AgeOperator)op, 75};
        ModelScan scan = {model, 0, 0, filter};
        Command command = {.type = COMMAND_SELECT_ALL, .filter = filter};
        ExecutionSink sink = {.record = compare_record, .context = &scan};
        ExecutionResult result = executor_execute(storage, &command, &sink);
        uint32_t matches = 0;
        for (uint32_t i = 0; i < key_count; ++i)
            if (model[i].active && expected_match(model[i].record.age, filter)) ++matches;
        CHECK(result.status == DB_OK && result.rows == matches && scan.matches == matches);
    }
}

int main(int argc, char **argv)
{
    bool stress = argc == 2 && strcmp(argv[1], "--stress") == 0;
    uint32_t operations = stress ? 60000U : 30000U;
    if (stress) key_count = 10000U;
    char path[64];
    temporary_path(path);
    Storage *storage;
    CHECK(storage_open(path, &storage) == DB_OK);
    ModelRow *model = calloc(key_count, sizeof(*model));
    CHECK(model != NULL);
    uint32_t random = 0x5341a78fU;
    for (uint32_t step = 0; step < operations; ++step) {
        random = random * 1664525U + 1013904223U;
        uint32_t key = (random >> 8U) % key_count;
        unsigned int operation = (random >> 24U) % 4U;
        Command command = {.record = {.id = key, .age = (uint8_t)((random >> 16U) % 151U)}};
        CHECK(snprintf(command.record.name, sizeof(command.record.name), "row-%u-revision-%u", key, step) > 0);
        DbStatus expected;
        if (operation == 0) {
            command.type = COMMAND_INSERT;
            expected = model[key].active ? DB_DUPLICATE_ID : DB_OK;
            if (expected == DB_OK) model[key] = (ModelRow){true, true, command.record};
        } else {
            expected = !model[key].exists ? DB_NOT_FOUND : model[key].active ? DB_OK : DB_DELETED;
            command.type = operation == 1 ? COMMAND_UPDATE : operation == 2 ? COMMAND_DELETE : COMMAND_SELECT;
            if (expected == DB_OK && operation == 1) model[key].record = command.record;
            if (expected == DB_OK && operation == 2) model[key].active = false;
        }
        ExecutionResult result = executor_execute(storage, &command, NULL);
        CHECK(result.status == expected && result.rows == (expected == DB_OK ? 1U : 0U));
        if (step % 1000U == 0) verify(storage, model);
        if (step % 5000U == 4999U) {
            CHECK(storage_close(storage) == DB_OK);
            CHECK(storage_open(path, &storage) == DB_OK);
            verify(storage, model);
        }
    }
    verify(storage, model);
    CHECK(storage_close(storage) == DB_OK);
    CHECK(unlink(path) == 0);
    free(model);
    printf("Mixed model: %u operations, %u IDs, %u reopen cycles and all age filters passed.\n",
           operations, key_count, operations / 5000U);
    return 0;
}
