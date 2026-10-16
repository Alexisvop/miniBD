#include "parser.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static void skip_space(const char **cursor)
{
    while (isspace((unsigned char)**cursor)) {
        ++*cursor;
    }
}

static bool token_equals(const char *start, size_t length, const char *expected)
{
    return strlen(expected) == length && memcmp(start, expected, length) == 0;
}

static ParseStatus parse_number(const char **cursor, uint32_t limit, uint32_t *value)
{
    uint32_t number = 0;
    const char *p = *cursor;
    if (*p < '0' || *p > '9') {
        return PARSE_INVALID_SYNTAX;
    }
    while (*p >= '0' && *p <= '9') {
        uint32_t digit = (uint32_t)(*p - '0');
        if (number > (limit - digit) / 10U) {
            return PARSE_OUT_OF_RANGE;
        }
        number = number * 10U + digit;
        ++p;
    }
    if (*p != '\0' && !isspace((unsigned char)*p)) {
        return PARSE_INVALID_SYNTAX;
    }
    *cursor = p;
    *value = number;
    return PARSE_OK;
}

static ParseStatus parse_name(const char **cursor, char *name)
{
    const char *p = *cursor;
    if (*p != '"') {
        return PARSE_INVALID_SYNTAX;
    }
    const char *start = ++p;
    while (*p != '\0' && *p != '"') {
        ++p;
    }
    if (*p != '"') {
        return PARSE_INVALID_SYNTAX;
    }
    size_t length = (size_t)(p - start);
    if (!record_name_valid(start, length)) {
        return PARSE_INVALID_NAME;
    }
    ++p;
    if (!isspace((unsigned char)*p)) {
        return PARSE_INVALID_SYNTAX;
    }
    memcpy(name, start, length);
    name[length] = '\0';
    *cursor = p;
    return PARSE_OK;
}

static bool consume_word(const char **cursor, const char *expected)
{
    const char *start = *cursor;
    const char *end = start;
    while (*end != '\0' && !isspace((unsigned char)*end)) ++end;
    if (!token_equals(start, (size_t)(end - start), expected)) return false;
    *cursor = end;
    skip_space(cursor);
    return true;
}

static ParseStatus parse_filter(const char **cursor, AgeFilter *filter)
{
    if (!consume_word(cursor, "WHERE") || !consume_word(cursor, "age")) return PARSE_INVALID_SYNTAX;
    const struct { const char *text; AgeOperator op; } operators[] = {
        {"=", AGE_FILTER_EQ}, {"!=", AGE_FILTER_NE}, {"<", AGE_FILTER_LT},
        {"<=", AGE_FILTER_LE}, {">", AGE_FILTER_GT}, {">=", AGE_FILTER_GE}
    };
    for (size_t i = 0; i < sizeof(operators) / sizeof(operators[0]); ++i) {
        if (consume_word(cursor, operators[i].text)) {
            uint32_t value;
            ParseStatus status = parse_number(cursor, MINIDB_AGE_MAX, &value);
            if (status != PARSE_OK) return status;
            filter->op = operators[i].op;
            filter->value = (uint8_t)value;
            skip_space(cursor);
            return PARSE_OK;
        }
    }
    return PARSE_INVALID_SYNTAX;
}

ParseStatus parser_parse(const char *input, Command *output)
{
    Command command = {0};
    const char *p = input;
    skip_space(&p);
    const char *start = p;
    while (*p != '\0' && !isspace((unsigned char)*p)) {
        ++p;
    }
    size_t length = (size_t)(p - start);
    if (length == 0) {
        return PARSE_EMPTY;
    }
    if (token_equals(start, length, "INSERT")) {
        command.type = COMMAND_INSERT;
    } else if (token_equals(start, length, "UPDATE")) {
        command.type = COMMAND_UPDATE;
    } else if (token_equals(start, length, "DELETE")) {
        command.type = COMMAND_DELETE;
    } else if (token_equals(start, length, "SELECT")) {
        command.type = COMMAND_SELECT;
    } else if (token_equals(start, length, "SELECT_ALL")) {
        command.type = COMMAND_SELECT_ALL;
    } else if (token_equals(start, length, ".stats")) {
        command.type = COMMAND_STATS;
    } else if (token_equals(start, length, ".btree")) {
        command.type = COMMAND_BTREE;
    } else if (token_equals(start, length, ".help")) {
        command.type = COMMAND_HELP;
    } else if (token_equals(start, length, ".exit")) {
        command.type = COMMAND_EXIT;
    } else {
        return PARSE_UNKNOWN_COMMAND;
    }
    skip_space(&p);
    if (command.type == COMMAND_INSERT || command.type == COMMAND_UPDATE ||
        command.type == COMMAND_SELECT || command.type == COMMAND_DELETE) {
        ParseStatus status = parse_number(&p, UINT32_MAX, &command.record.id);
        if (status != PARSE_OK) {
            return status;
        }
        skip_space(&p);
    }
    if (command.type == COMMAND_INSERT || command.type == COMMAND_UPDATE) {
        ParseStatus status = parse_name(&p, command.record.name);
        if (status != PARSE_OK) {
            return status;
        }
        skip_space(&p);
        uint32_t age;
        status = parse_number(&p, MINIDB_AGE_MAX, &age);
        if (status != PARSE_OK) {
            return status;
        }
        command.record.age = (uint8_t)age;
        skip_space(&p);
    }
    if (command.type == COMMAND_SELECT_ALL && *p != '\0') {
        ParseStatus status = parse_filter(&p, &command.filter);
        if (status != PARSE_OK) return status;
    }
    if (*p != '\0') {
        return PARSE_INVALID_SYNTAX;
    }
    *output = command;
    return PARSE_OK;
}

const char *parser_status_message(ParseStatus status)
{
    switch (status) {
    case PARSE_OK: return "valid command";
    case PARSE_EMPTY: return "empty input";
    case PARSE_UNKNOWN_COMMAND: return "unknown command; type .help";
    case PARSE_INVALID_SYNTAX: return "invalid syntax; type .help";
    case PARSE_OUT_OF_RANGE: return "number out of range (id: 0..4294967295; age: 0..150)";
    case PARSE_INVALID_NAME: return "name must contain 1..64 bytes of valid UTF-8 without ASCII controls";
    }
    return "unknown parse error";
}
