#ifndef MINIDB_PARSER_H
#define MINIDB_PARSER_H

#include "record.h"

typedef enum {
    COMMAND_INSERT,
    COMMAND_SELECT,
    COMMAND_SELECT_ALL,
    COMMAND_HELP,
    COMMAND_EXIT
} CommandType;

typedef struct {
    CommandType type;
    Record record; /* SELECT uses only record.id. */
} Command;

typedef enum {
    PARSE_OK,
    PARSE_EMPTY,
    PARSE_UNKNOWN_COMMAND,
    PARSE_INVALID_SYNTAX,
    PARSE_OUT_OF_RANGE,
    PARSE_INVALID_NAME
} ParseStatus;

/* Requires non-NULL pointers and NUL-terminated input. No allocation or I/O.
 * The caller owns both objects; output is modified only on PARSE_OK. */
ParseStatus parser_parse(const char *input, Command *output);
const char *parser_status_message(ParseStatus status);

#endif
