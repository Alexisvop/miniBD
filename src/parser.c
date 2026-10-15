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

/* Reject overlong encodings, surrogates, truncated sequences and > U+10FFFF. */
static bool valid_utf8(const unsigned char *text, size_t length)
{
    size_t i = 0;
    while (i < length) {
        unsigned char first = text[i++];
        if (first < 0x80U) {
            if (first < 0x20U || first == 0x7fU) {
                return false;
            }
            continue;
        }
        size_t extra;
        uint32_t codepoint;
        uint32_t minimum;
        if (first >= 0xc2U && first <= 0xdfU) {
            extra = 1; codepoint = first & 0x1fU; minimum = 0x80U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            extra = 2; codepoint = first & 0x0fU; minimum = 0x800U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            extra = 3; codepoint = first & 0x07U; minimum = 0x10000U;
        } else {
            return false;
        }
        if (length - i < extra) {
            return false;
        }
        for (size_t j = 0; j < extra; ++j) {
            unsigned char next = text[i++];
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            return false;
        }
    }
    return true;
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
    if (length == 0 || length > MINIDB_NAME_MAX_BYTES ||
        !valid_utf8((const unsigned char *)start, length)) {
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
    } else if (token_equals(start, length, "SELECT")) {
        command.type = COMMAND_SELECT;
    } else if (token_equals(start, length, "SELECT_ALL")) {
        command.type = COMMAND_SELECT_ALL;
    } else if (token_equals(start, length, ".help")) {
        command.type = COMMAND_HELP;
    } else if (token_equals(start, length, ".exit")) {
        command.type = COMMAND_EXIT;
    } else {
        return PARSE_UNKNOWN_COMMAND;
    }
    skip_space(&p);
    if (command.type == COMMAND_INSERT || command.type == COMMAND_SELECT) {
        ParseStatus status = parse_number(&p, UINT32_MAX, &command.record.id);
        if (status != PARSE_OK) {
            return status;
        }
        skip_space(&p);
    }
    if (command.type == COMMAND_INSERT) {
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
