#ifndef MINIDB_PARSER_H
#define MINIDB_PARSER_H

#include "record.h"

typedef enum {
    COMMAND_INSERT,
    COMMAND_UPDATE,
    COMMAND_DELETE,
    COMMAND_SELECT,
    COMMAND_SELECT_ALL,
    COMMAND_HELP,
    COMMAND_STATS,
    COMMAND_BTREE,
    COMMAND_EXIT
} CommandType;

typedef enum {
    AGE_FILTER_NONE, AGE_FILTER_EQ, AGE_FILTER_NE, AGE_FILTER_LT,
    AGE_FILTER_LE, AGE_FILTER_GT, AGE_FILTER_GE
} AgeOperator;
typedef struct { AgeOperator op; uint8_t value; } AgeFilter;

typedef struct {
    CommandType type;
    Record record; /* SELECT/DELETE use only record.id. UPDATE preserves this ID. */
    AgeFilter filter; /* SELECT_ALL only. */
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
