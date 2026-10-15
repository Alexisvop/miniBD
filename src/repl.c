#include "repl.h"
#include "parser.h"

#include <inttypes.h>
#include <stddef.h>

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

static void print_help(FILE *output)
{
    fputs("Commands (case-sensitive):\n"
          "  INSERT <id> \"<name>\" <age>\n"
          "  SELECT <id>\n"
          "  SELECT_ALL\n"
          "  .help\n"
          "  .exit\n"
          "ID: 0..4294967295; age: 0..150; name: 1..64 UTF-8 bytes.\n"
          "Names require double quotes; quote escaping is not supported.\n"
          "Input limit: 4096 bytes per line, excluding the newline.\n"
          "Phase 1: commands are parsed only; no records are stored or retrieved.\n",
          output);
}

int repl_run(FILE *input, FILE *output, FILE *errors, bool interactive)
{
    char line[MINIDB_INPUT_MAX_BYTES + 1U];
    if (interactive) {
        fputs("MiniDB v0.1\nEducational Database Engine\n"
              "Phase 1: parser only. Type .help for usage.\n", output);
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
        case COMMAND_INSERT:
            fprintf(output, "PARSED: INSERT %" PRIu32 " | %s | %u (storage not implemented)\n",
                    command.record.id, command.record.name, (unsigned int)command.record.age);
            break;
        case COMMAND_SELECT:
            fprintf(output, "PARSED: SELECT %" PRIu32 " (storage not implemented)\n",
                    command.record.id);
            break;
        case COMMAND_SELECT_ALL:
            fputs("PARSED: SELECT_ALL (storage not implemented)\n", output);
            break;
        }
    }
}
