#include "repl.h"
#include "parser.h"
#include "executor.h"

#include <stddef.h>
#include <inttypes.h>

typedef enum { LINE_OK, LINE_EOF, LINE_INVALID, LINE_ERROR } LineStatus;

/* Consume the entire line even when invalid, so its suffix is never executed. */
static LineStatus read_line(FILE *input, char *buffer)
{
    size_t length = 0;
    bool invalid = false;
    int ch;
    while ((ch = fgetc(input)) != EOF && ch != '\n') {
        if (ch == 0 || length == MINIDB_INPUT_MAX_BYTES) {
            invalid = true;
        } else {
            buffer[length++] = (char)ch;
        }
    }
    buffer[length] = '\0';
    if (ferror(input)) {
        return LINE_ERROR;
    }
    if (invalid) {
        return LINE_INVALID;
    }
    return ch == EOF && length == 0 ? LINE_EOF : LINE_OK;
}

static DbStatus print_record(const Record *record, void *context)
{
    FILE *output = context;
    return fprintf(output, "%" PRIu32 " | %s | %u\n", record->id, record->name,
                   (unsigned int)record->age) < 0 ? DB_IO_ERROR : DB_OK;
}

static DbStatus print_node(const BTreeNodeView *node, void *context)
{
    FILE *output = context;
    if (fprintf(output, "%*spage=%" PRIu32 " %s%s keys=%" PRIu32,
                (int)(node->depth * 2U), "", node->id, node->leaf ? "leaf" : "internal",
                node->root ? " root" : "", node->count) < 0) return DB_IO_ERROR;
    if (node->leaf) {
        if (fprintf(output, " next=%" PRIu32, node->next) < 0) return DB_IO_ERROR;
        for (uint32_t i = 0; i < node->count; ++i) {
            if (fprintf(output, " %" PRIu32 "->(%" PRIu32 ",%" PRIu32 ")", node->keys[i],
                        node->locations[i].page, node->locations[i].slot) < 0) return DB_IO_ERROR;
        }
    } else {
        if (fprintf(output, " child0=%" PRIu32, node->children[0]) < 0) return DB_IO_ERROR;
        for (uint32_t i = 0; i < node->count; ++i) {
            if (fprintf(output, " [%" PRIu32 "]->%" PRIu32, node->keys[i], node->children[i + 1U]) < 0)
                return DB_IO_ERROR;
        }
    }
    return fputc('\n', output) == EOF ? DB_IO_ERROR : DB_OK;
}

static void print_result(FILE *output, CommandType type, const ExecutionResult *result)
{
    if (type == COMMAND_INSERT || type == COMMAND_UPDATE || type == COMMAND_DELETE) {
        const char *verb = type == COMMAND_INSERT ? "inserted" : type == COMMAND_UPDATE ? "updated" : "deleted";
        fprintf(output, "OK: 1 row %s\n", verb);
    } else if (type == COMMAND_SELECT_ALL && result->rows == 0) {
        fputs("OK: 0 rows\n", output);
    } else if (type == COMMAND_STATS) {
        const StorageStats *stats = &result->stats;
        fprintf(output,
            "Persistent structure: records=%" PRIu32 " active=%" PRIu32 " deleted=%" PRIu32
            " index_entries=%" PRIu32 " pages=%" PRIu32 " data_pages=%" PRIu32 " index_pages=%" PRIu32
            " internal_nodes=%" PRIu32 " leaf_nodes=%" PRIu32 " height=%" PRIu32 "\n"
            "Session splits: leaf=%" PRIu64 " internal=%" PRIu64 " root=%" PRIu64 "\n",
            stats->records, stats->active_records, stats->deleted_records, stats->tree.entries, stats->pages,
            stats->pages - 1U - stats->tree.leaves - stats->tree.internal_nodes,
            stats->tree.leaves + stats->tree.internal_nodes, stats->tree.internal_nodes,
            stats->tree.leaves, stats->tree.height, stats->leaf_splits, stats->internal_splits, stats->root_splits);
    }
}

static void print_help(FILE *output)
{
    fputs("Commands (case-sensitive):\n"
          "  INSERT <id> \"<name>\" <age>\n"
          "  UPDATE <id> \"<name>\" <age>\n"
          "  DELETE <id>\n"
          "  SELECT <id>\n"
          "  SELECT_ALL [WHERE age <op> <value>]\n"
          "  Operators: = != < <= > >= (separate tokens with whitespace)\n"
          "  .stats\n"
          "  .btree\n"
          "  .help\n"
          "  .exit\n"
          "ID: 0..4294967295; age: 0..150; name: 1..64 UTF-8 bytes.\n"
          "Names require double quotes; quote escaping is not supported.\n"
          "Input limit: 4096 bytes per line, excluding the newline.\n"
          "DELETE retains storage/index entries; INSERT reactivates the same deleted ID.\n"
          "Phase 4: updates and logical deletion; changes are synchronized at normal close.\n",
          output);
}

int repl_run(FILE *input, FILE *output, FILE *errors, bool interactive, Storage *storage)
{
    char line[MINIDB_INPUT_MAX_BYTES + 1U];
    if (interactive) {
        fputs("MiniDB v0.4\nEducational Database Engine\n"
              "Type .help for usage. Changes are saved at normal close.\n", output);
    }
    for (;;) {
        if (interactive) {
            fputs("minidb> ", output);
        }
        if (fflush(output) == EOF || ferror(output) || ferror(errors)) {
            return 1;
        }
        LineStatus line_status = read_line(input, line);
        if (line_status == LINE_EOF) {
            return 0;
        }
        if (line_status == LINE_ERROR) {
            fputs("ERROR: failed to read input\n", errors);
            return 1;
        }
        if (line_status == LINE_INVALID) {
            fputs("ERROR: input exceeds 4096 bytes or contains a NUL byte\n", errors);
            continue;
        }
        Command command;
        ParseStatus status = parser_parse(line, &command);
        if (status == PARSE_EMPTY) {
            continue;
        }
        if (status != PARSE_OK) {
            fprintf(errors, "ERROR: %s\n", parser_status_message(status));
            continue;
        }
        switch (command.type) {
        case COMMAND_EXIT: return 0;
        case COMMAND_HELP: print_help(output); break;
        case COMMAND_STATS:
        case COMMAND_BTREE:
        case COMMAND_INSERT:
        case COMMAND_UPDATE:
        case COMMAND_DELETE:
        case COMMAND_SELECT:
        case COMMAND_SELECT_ALL: {
            ExecutionSink sink = {print_record, print_node, output};
            ExecutionResult result = executor_execute(storage, &command, &sink);
            if (result.status == DB_OK) print_result(output, command.type, &result);
            else {
                fprintf(errors, "ERROR: %s\n", db_status_message(result.status));
                if (result.status != DB_DUPLICATE_ID && result.status != DB_NOT_FOUND &&
                    result.status != DB_DELETED && result.status != DB_INVALID_COMMAND &&
                    result.status != DB_INVALID_RECORD && result.status != DB_LIMIT_REACHED) {
                    return 1;
                }
            }
            break;
        }
        }
    }
}
