#define _POSIX_C_SOURCE 200809L
#include "executor.h"
#include "test_support.h"

typedef struct { uint32_t ids[16]; uint32_t count; bool fail; } Rows;
static DbStatus collect(const Record *record, void *context)
{
    Rows *rows = context;
    if (rows->fail) return DB_IO_ERROR;
    CHECK(rows->count < 16);
    rows->ids[rows->count++] = record->id;
    return DB_OK;
}

static DbStatus reject_node(const BTreeNodeView *node, void *context)
{
    (void)context;
    CHECK(node->leaf && node->root && node->count == 5);
    return DB_IO_ERROR;
}

static ExecutionResult execute(Storage *storage, const char *text, Rows *rows)
{
    Command command;
    CHECK(parser_parse(text, &command) == PARSE_OK);
    ExecutionSink sink = {.record = collect, .context = rows};
    return executor_execute(storage, &command, rows == NULL ? NULL : &sink);
}

static void expect(Storage *storage, const char *text, DbStatus status, uint32_t count)
{
    ExecutionResult result = execute(storage, text, NULL);
    CHECK(result.status == status && result.rows == count);
}

static void check_parser(void)
{
    const char *invalid[] = {
        "UPDATE", "UPDATE 1", "UPDATE 1 \"A\"", "UPDATE -1 \"A\" 1",
        "UPDATE 4294967296 \"A\" 1", "UPDATE 1 \"\" 1", "UPDATE 1 \"A\" 151",
        "UPDATE 1 \"A\" -1", "UPDATE 1 \"A\" 1 2", "DELETE", "DELETE +1", "DELETE 1 extra",
        "SELECT_ALL WHERE", "SELECT_ALL WHERE age", "SELECT_ALL WHERE age >=",
        "SELECT_ALL WHERE name = 21", "SELECT_ALL WHERE Age = 21", "SELECT_ALL where age = 21",
        "SELECT_ALL WHERE age == 21", "SELECT_ALL WHERE age <> 21", "SELECT_ALL WHERE age ! 21",
        "SELECT_ALL WHERE age = -1", "SELECT_ALL WHERE age = 151", "SELECT_ALL WHERE age = +1",
        "SELECT_ALL WHERE age = 1.2", "SELECT_ALL WHERE age = 4294967296",
        "SELECT_ALL WHERE age = 999999999999999999999999", "SELECT_ALL WHERE age >=21",
        "SELECT_ALL WHERE age = 21 extra", "SELECT_ALL WHERE age = 21 AND age < 30",
        "SELECT 1 WHERE age = 21", "UPDATE 1 \"A\" 21 WHERE age = 21"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        Command command = {.type = COMMAND_SELECT, .record = {.id = 77}};
        CHECK(parser_parse(invalid[i], &command) != PARSE_OK);
        CHECK(command.type == COMMAND_SELECT && command.record.id == 77);
    }
    Command command;
    CHECK(parser_parse("UPDATE 4294967295 \"Jos\xc3\xa9\" 150", &command) == PARSE_OK);
    CHECK(command.type == COMMAND_UPDATE && command.record.id == UINT32_MAX && command.record.age == 150);
    CHECK(strcmp(command.record.name, "Jos\xc3\xa9") == 0);
    CHECK(parser_parse("DELETE 0", &command) == PARSE_OK && command.type == COMMAND_DELETE);
    CHECK(parser_parse(" SELECT_ALL\tWHERE\tage\t>=\t00021\r ", &command) == PARSE_OK);
    CHECK(command.filter.op == AGE_FILTER_GE && command.filter.value == 21);
}

int main(void)
{
    check_parser();
    char path[64];
    temporary_path(path);
    Storage *storage;
    CHECK(storage_open(path, &storage) == DB_OK);
    expect(storage, "SELECT_ALL WHERE age >= 0", DB_OK, 0);
    expect(storage, "UPDATE 10 \"A\" 1", DB_NOT_FOUND, 0);
    expect(storage, "DELETE 10", DB_NOT_FOUND, 0);
    const char *inserts[] = {
        "INSERT 30 \"Carlos\" 22", "INSERT 10 \"Alexis\" 0", "INSERT 50 \"Old\" 150",
        "INSERT 20 \"Twenty\" 20", "INSERT 40 \"Twenty one\" 21"
    };
    for (size_t i = 0; i < sizeof(inserts) / sizeof(inserts[0]); ++i) expect(storage, inserts[i], DB_OK, 1);
    expect(storage, "UPDATE 10 \"Alexis Perez\" 22", DB_OK, 1);
    expect(storage, "DELETE 20", DB_OK, 1);
    expect(storage, "DELETE 20", DB_DELETED, 0);
    expect(storage, "SELECT 20", DB_DELETED, 0);
    expect(storage, "UPDATE 20 \"No resurrection\" 20", DB_DELETED, 0);
    Record record = {999, "unchanged", 42};
    CHECK(storage_select(storage, 20, &record) == DB_DELETED && record.id == 999);
    expect(storage, "INSERT 20 \"Reactivated\" 21", DB_OK, 1);
    expect(storage, "INSERT 20 \"Duplicate\" 21", DB_DUPLICATE_ID, 0);
    expect(storage, "DELETE 20", DB_OK, 1);
    expect(storage, "DELETE 40", DB_OK, 1);
    ExecutionResult result = execute(storage, ".stats", NULL);
    CHECK(result.status == DB_OK && result.stats.records == 5 && result.stats.active_records == 3);
    CHECK(result.stats.deleted_records == 2 && result.stats.tree.entries == 5 && result.stats.pages == 3);
    const char *operators[] = {"=", "!=", "<", "<=", ">", ">="};
    const uint32_t expected[] = {2, 1, 0, 2, 1, 3};
    for (size_t i = 0; i < 6; ++i) {
        char query[80];
        CHECK(snprintf(query, sizeof(query), "SELECT_ALL WHERE age %s 22", operators[i]) > 0);
        Rows rows = {0};
        result = execute(storage, query, &rows);
        CHECK(result.status == DB_OK && result.rows == expected[i] && rows.count == expected[i]);
        for (uint32_t j = 1; j < rows.count; ++j) CHECK(rows.ids[j - 1U] < rows.ids[j]);
    }
    expect(storage, "SELECT_ALL WHERE age < 0", DB_OK, 0);
    expect(storage, "SELECT_ALL WHERE age > 150", DB_OK, 0);
    expect(storage, "SELECT_ALL WHERE age <= 150", DB_OK, 3);
    Rows rows = {0};
    result = execute(storage, "SELECT_ALL", &rows);
    CHECK(result.status == DB_OK && result.rows == 3 && rows.ids[0] == 10 && rows.ids[1] == 30 && rows.ids[2] == 50);
    rows.fail = true;
    result = execute(storage, "SELECT_ALL", &rows);
    CHECK(result.status == DB_IO_ERROR && result.rows == 0);
    Command command = {.type = COMMAND_SELECT_ALL, .filter = {(AgeOperator)99, 21}};
    CHECK(executor_execute(storage, &command, NULL).status == DB_INVALID_COMMAND);
    command.filter = (AgeFilter){AGE_FILTER_GE, 151};
    CHECK(executor_execute(storage, &command, NULL).status == DB_INVALID_COMMAND);
    command.type = COMMAND_DELETE; command.filter.value = 21;
    CHECK(executor_execute(storage, &command, NULL).status == DB_INVALID_COMMAND);
    command = (Command){.type = COMMAND_UPDATE, .record = {10, "Invalid", 151}};
    CHECK(executor_execute(storage, &command, NULL).status == DB_INVALID_RECORD);
    command = (Command){.type = COMMAND_BTREE};
    ExecutionSink bad_node = {.node = reject_node};
    CHECK(executor_execute(storage, &command, &bad_node).status == DB_IO_ERROR);
    CHECK(executor_execute(storage, &command, NULL).status == DB_OK);
    command.type = (CommandType)999;
    CHECK(executor_execute(storage, &command, NULL).status == DB_INVALID_COMMAND);
    CHECK(executor_execute(storage, NULL, NULL).status == DB_INVALID_COMMAND);
    CHECK(storage_close(storage) == DB_OK);

    CHECK(storage_open(path, &storage) == DB_OK);
    CHECK(storage_select(storage, 10, &record) == DB_OK && record.age == 22 && strcmp(record.name, "Alexis Perez") == 0);
    expect(storage, "SELECT 20", DB_DELETED, 0);
    expect(storage, "SELECT 40", DB_DELETED, 0);
    expect(storage, "SELECT_ALL", DB_OK, 3);
    expect(storage, "DELETE 10", DB_OK, 1);
    expect(storage, "DELETE 30", DB_OK, 1);
    expect(storage, "DELETE 50", DB_OK, 1);
    expect(storage, "SELECT_ALL", DB_OK, 0);
    result = execute(storage, ".stats", NULL);
    CHECK(result.status == DB_OK && result.stats.active_records == 0 && result.stats.deleted_records == 5);
    expect(storage, "INSERT 20 \"Back\" 0", DB_OK, 1);
    expect(storage, "INSERT 0 \"Min\" 0", DB_OK, 1);
    expect(storage, "INSERT 4294967295 \"Max\" 150", DB_OK, 1);
    expect(storage, "DELETE 0", DB_OK, 1);
    expect(storage, "UPDATE 4294967295 \"Max changed\" 0", DB_OK, 1);
    CHECK(storage_close(storage) == DB_OK);
    CHECK(storage_open(path, &storage) == DB_OK);
    expect(storage, "SELECT 0", DB_DELETED, 0);
    expect(storage, "SELECT 20", DB_OK, 1);
    expect(storage, "SELECT_ALL WHERE age = 0", DB_OK, 2);
    CHECK(storage_close(storage) == DB_OK);
    CHECK(unlink(path) == 0);
    puts("Queries: structured results, updates, tombstones, reactivation, six filters and persistence passed.");
    return 0;
}
