#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include "test_support.h"

static uint32_t random_state = 0x915e4badU;
static uint32_t next_random(void)
{
    random_state = random_state * 1664525U + 1013904223U;
    return random_state;
}

int main(void)
{
    const char *seeds[] = {"INSERT 4294967295 \"Jos\xc3\xa9\" 150", "UPDATE 0 \"A\" 0",
        "SELECT_ALL WHERE age >= 21", "DELETE 5", "SELECT 0", ".stats"};
    for (uint32_t trial = 0; trial < 50000U; ++trial) {
        char input[256];
        strcpy(input, seeds[trial % 6U]);
        size_t length = strlen(input);
        for (uint32_t edit = 0; edit <= trial % 5U; ++edit)
            input[next_random() % (length + 1U)] = (char)(next_random() >> 24U);
        input[length + 1U] = '\0';
        Command command, saved;
        memset(&command, 0x5a, sizeof(command));
        memcpy(&saved, &command, sizeof(saved));
        ParseStatus status = parser_parse(input, &command);
        if (status != PARSE_OK) CHECK(memcmp(&command, &saved, sizeof(command)) == 0);
        else if (command.type == COMMAND_INSERT || command.type == COMMAND_UPDATE) {
            uint8_t bytes[MINIDB_RECORD_SIZE];
            Record decoded;
            CHECK(record_serialize(&command.record, bytes) && record_deserialize(bytes, &decoded));
            CHECK(decoded.id == command.record.id && decoded.age == command.record.age);
            CHECK(strcmp(decoded.name, command.record.name) == 0);
        }
        Record original = {next_random(), "UTF-8 \xf0\x9f\x98\x80", (uint8_t)(trial % 151U)};
        uint8_t bytes[MINIDB_RECORD_SIZE], roundtrip[MINIDB_RECORD_SIZE];
        CHECK(record_serialize(&original, bytes));
        bytes[next_random() % MINIDB_RECORD_SIZE] = (uint8_t)(next_random() >> 24U);
        Record decoded, untouched;
        memset(&decoded, 0x5a, sizeof(decoded));
        memcpy(&untouched, &decoded, sizeof(untouched));
        if (record_deserialize(bytes, &decoded)) {
            CHECK(record_serialize(&decoded, roundtrip));
            CHECK(memcmp(bytes, roundtrip, sizeof(bytes)) == 0);
        } else CHECK(memcmp(&decoded, &untouched, sizeof(decoded)) == 0);
    }
    puts("50000 seeded parser mutations and 50000 codec mutations passed.");
    return 0;
}
