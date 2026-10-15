#include "executor.h"

#include <inttypes.h>

static DbStatus print_record(const Record *record, void *context)
{
    FILE *output = context;
    return fprintf(output, "%" PRIu32 " | %s | %u\n", record->id,
                   record->name, (unsigned int)record->age) < 0 ? DB_IO_ERROR : DB_OK;
}

DbStatus executor_execute(Storage *storage, const Command *command, FILE *output)
{
    switch (command->type) {
    case COMMAND_INSERT: {
        DbStatus status = storage_insert(storage, &command->record);
        if (status != DB_OK) {
            return status;
        }
        return fputs("OK: 1 row inserted\n", output) == EOF ? DB_IO_ERROR : DB_OK;
    }
    case COMMAND_SELECT: {
        Record record;
        DbStatus status = storage_select(storage, command->record.id, &record);
        return status == DB_OK ? print_record(&record, output) : status;
    }
    case COMMAND_SELECT_ALL:
        return storage_scan(storage, print_record, output);
    case COMMAND_HELP:
    case COMMAND_EXIT:
        return DB_INVALID_RECORD;
    }
    return DB_INVALID_RECORD;
}
