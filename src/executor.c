#include "executor.h"

static bool matches_age(uint8_t age, AgeFilter filter)
{
    switch (filter.op) {
    case AGE_FILTER_NONE: return true;
    case AGE_FILTER_EQ: return age == filter.value;
    case AGE_FILTER_NE: return age != filter.value;
    case AGE_FILTER_LT: return age < filter.value;
    case AGE_FILTER_LE: return age <= filter.value;
    case AGE_FILTER_GT: return age > filter.value;
    case AGE_FILTER_GE: return age >= filter.value;
    }
    return false;
}

typedef struct {
    const ExecutionSink *sink;
    AgeFilter filter;
    uint32_t rows;
} Query;

static DbStatus deliver_record(const Record *record, void *context)
{
    Query *query = context;
    if (!matches_age(record->age, query->filter)) return DB_OK;
    if (query->sink != NULL && query->sink->record != NULL) {
        DbStatus status = query->sink->record(record, query->sink->context);
        if (status != DB_OK) return status;
    }
    ++query->rows;
    return DB_OK;
}

static DbStatus deliver_node(const BTreeNodeView *node, void *context)
{
    const ExecutionSink *sink = context;
    return sink != NULL && sink->node != NULL ? sink->node(node, sink->context) : DB_OK;
}

ExecutionResult executor_execute(Storage *storage, const Command *command, const ExecutionSink *sink)
{
    ExecutionResult result = {.status = DB_INVALID_COMMAND};
    if (command == NULL || storage == NULL) return result;
    if (command->filter.op < AGE_FILTER_NONE || command->filter.op > AGE_FILTER_GE ||
        (command->filter.op != AGE_FILTER_NONE &&
         (command->type != COMMAND_SELECT_ALL || command->filter.value > MINIDB_AGE_MAX))) return result;
    Query query = {sink, command->filter, 0};
    switch (command->type) {
    case COMMAND_INSERT:
        result.status = storage_insert(storage, &command->record);
        break;
    case COMMAND_UPDATE:
        result.status = storage_update(storage, &command->record);
        break;
    case COMMAND_DELETE:
        result.status = storage_delete(storage, command->record.id);
        break;
    case COMMAND_SELECT: {
        Record record;
        result.status = storage_select(storage, command->record.id, &record);
        if (result.status == DB_OK) result.status = deliver_record(&record, &query);
        result.rows = query.rows;
        return result;
    }
    case COMMAND_SELECT_ALL:
        result.status = storage_scan(storage, deliver_record, &query);
        result.rows = query.rows;
        return result;
    case COMMAND_STATS:
        result.status = storage_validate(storage, &result.stats);
        return result;
    case COMMAND_BTREE:
        /* A local copy avoids casting away constness on the callback context. */
        if (sink != NULL) {
            ExecutionSink local = *sink;
            result.status = storage_inspect(storage, deliver_node, &local);
        } else result.status = storage_inspect(storage, deliver_node, NULL);
        return result;
    case COMMAND_HELP:
    case COMMAND_EXIT:
        return result;
    default:
        return result;
    }
    result.rows = result.status == DB_OK ? 1U : 0U;
    return result;
}
