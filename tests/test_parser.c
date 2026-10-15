#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void check_status(const char *input, ParseStatus expected)
{
    Command command = {.type = COMMAND_SELECT, .record = {42U, "unchanged", 20U}};
    ParseStatus actual = parser_parse(input, &command);
    if (actual != expected) {
        fprintf(stderr, "Unexpected status for input: %s\n", input);
    }
    CHECK(actual == expected);
    if (actual != PARSE_OK) {
        CHECK(command.type == COMMAND_SELECT);
        CHECK(command.record.id == 42U);
        CHECK(strcmp(command.record.name, "unchanged") == 0);
        CHECK(command.record.age == 20U);
    }
}

int main(void)
{
    Command command;
    CHECK(parser_parse("INSERT 1 \"Alexis Perez\" 21", &command) == PARSE_OK);
    CHECK(command.type == COMMAND_INSERT);
    CHECK(command.record.id == 1U);
    CHECK(strcmp(command.record.name, "Alexis Perez") == 0);
    CHECK(command.record.age == 21U);
    CHECK(parser_parse(" \tINSERT\t4294967295 \"Jos\xc3\xa9 \xf0\x9f\x98\x80\" 150\r ", &command) == PARSE_OK);
    CHECK(command.record.id == UINT32_MAX);
    CHECK(command.record.age == 150U);
    CHECK(strcmp(command.record.name, "Jos\xc3\xa9 \xf0\x9f\x98\x80") == 0);
    CHECK(parser_parse("SELECT 0", &command) == PARSE_OK);
    CHECK(command.type == COMMAND_SELECT && command.record.id == 0U);
    CHECK(parser_parse("SELECT 4294967295", &command) == PARSE_OK);
    CHECK(command.record.id == UINT32_MAX);
    CHECK(parser_parse("SELECT_ALL", &command) == PARSE_OK);
    CHECK(command.type == COMMAND_SELECT_ALL);
    CHECK(parser_parse(".help\t", &command) == PARSE_OK);
    CHECK(command.type == COMMAND_HELP);
    CHECK(parser_parse(" .exit ", &command) == PARSE_OK);
    CHECK(command.type == COMMAND_EXIT);

    const struct { const char *input; ParseStatus expected; } cases[] = {
        {"", PARSE_EMPTY}, {" \t\r\n", PARSE_EMPTY},
        {"insert 1 \"A\" 1", PARSE_UNKNOWN_COMMAND},
        {"SELECT_ALL_EXTRA", PARSE_UNKNOWN_COMMAND},
        {"SELECT1", PARSE_UNKNOWN_COMMAND}, {".exitnow", PARSE_UNKNOWN_COMMAND},
        {"UPDATE 1 \"A\" 1", PARSE_UNKNOWN_COMMAND},
        {"DELETE 1", PARSE_UNKNOWN_COMMAND}, {".stats", PARSE_UNKNOWN_COMMAND},
        {"SELECT", PARSE_INVALID_SYNTAX}, {"INSERT", PARSE_INVALID_SYNTAX},
        {"SELECT -1", PARSE_INVALID_SYNTAX}, {"SELECT +1", PARSE_INVALID_SYNTAX},
        {"SELECT 1.5", PARSE_INVALID_SYNTAX}, {"SELECT 0x10", PARSE_INVALID_SYNTAX},
        {"SELECT 1x", PARSE_INVALID_SYNTAX}, {"SELECT 1 2", PARSE_INVALID_SYNTAX},
        {"SELECT 4294967296", PARSE_OUT_OF_RANGE},
        {"SELECT 999999999999999999999999999999999", PARSE_OUT_OF_RANGE},
        {"INSERT 1 Alexis 21", PARSE_INVALID_SYNTAX},
        {"INSERT 1 \"Alexis 21", PARSE_INVALID_SYNTAX},
        {"INSERT 1\"A\" 21", PARSE_INVALID_SYNTAX},
        {"INSERT 1 \"A\"21", PARSE_INVALID_SYNTAX},
        {"INSERT 1 \"A\"", PARSE_INVALID_SYNTAX},
        {"INSERT 1 \"A\" -1", PARSE_INVALID_SYNTAX},
        {"INSERT 1 \"A\" 151", PARSE_OUT_OF_RANGE},
        {"INSERT 1 \"A\" 4294967296", PARSE_OUT_OF_RANGE},
        {"INSERT 1 \"A\" 1 extra", PARSE_INVALID_SYNTAX},
        {"INSERT 1 \"\" 1", PARSE_INVALID_NAME},
        {"INSERT 1 \"A\tB\" 1", PARSE_INVALID_NAME},
        {"INSERT 1 \"\x7f\" 1", PARSE_INVALID_NAME},
        {"INSERT 1 \"\xc0\xaf\" 1", PARSE_INVALID_NAME},
        {"INSERT 1 \"\xe0\x80\xaf\" 1", PARSE_INVALID_NAME},
        {"INSERT 1 \"\xed\xa0\x80\" 1", PARSE_INVALID_NAME},
        {"INSERT 1 \"\xf4\x90\x80\x80\" 1", PARSE_INVALID_NAME},
        {"INSERT 1 \"\xff\" 1", PARSE_INVALID_NAME},
        {"INSERT 1 \"\x80\" 1", PARSE_INVALID_NAME},
        {"INSERT 1 \"\xe2\x82\" 1", PARSE_INVALID_NAME},
        {"INSERT 1 \"\xc2 A\" 1", PARSE_INVALID_NAME},
        {".exit extra", PARSE_INVALID_SYNTAX}, {".help extra", PARSE_INVALID_SYNTAX},
        {"SELECT_ALL 1", PARSE_INVALID_SYNTAX}, {"SELECT 1;", PARSE_INVALID_SYNTAX},
        {"INSERT 0 \"A\" 0", PARSE_OK}, {"SELECT 00001", PARSE_OK}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        check_status(cases[i].input, cases[i].expected);
    }

    char name[66];
    char input[128];
    memset(name, 'a', 64);
    name[64] = '\0';
    CHECK(snprintf(input, sizeof(input), "INSERT 1 \"%s\" 1", name) > 0);
    check_status(input, PARSE_OK);
    name[64] = 'a'; name[65] = '\0';
    CHECK(snprintf(input, sizeof(input), "INSERT 1 \"%s\" 1", name) > 0);
    check_status(input, PARSE_INVALID_NAME);
    for (size_t i = 0; i < 64; i += 2) {
        name[i] = (char)0xc3; name[i + 1] = (char)0xa9;
    }
    name[64] = '\0';
    CHECK(snprintf(input, sizeof(input), "INSERT 1 \"%s\" 1", name) > 0);
    check_status(input, PARSE_OK);
    name[64] = 'a'; name[65] = '\0';
    CHECK(snprintf(input, sizeof(input), "INSERT 1 \"%s\" 1", name) > 0);
    check_status(input, PARSE_INVALID_NAME);
    puts("Parser: valid commands, syntax, numeric limits and UTF-8 checks passed.");
    return 0;
}
