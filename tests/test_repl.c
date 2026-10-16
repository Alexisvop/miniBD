#define _POSIX_C_SOURCE 200809L
#include "repl.h"

#include "test_support.h"

static void run_session(const void *data, size_t size, bool interactive,
                        char *output_text, char *error_text)
{
    char path[64];
    temporary_path(path);
    Storage *storage;
    CHECK(storage_open(path, &storage) == DB_OK);
    const uint32_t seed_ids[] = {2, 7, 9};
    for (size_t i = 0; i < sizeof(seed_ids) / sizeof(seed_ids[0]); ++i) {
        Record record = {seed_ids[i], "Seed", 20};
        CHECK(storage_insert(storage, &record) == DB_OK);
    }
    FILE *input = tmpfile();
    FILE *output = tmpfile();
    FILE *errors = tmpfile();
    CHECK(input != NULL && output != NULL && errors != NULL);
    CHECK(fwrite(data, 1, size, input) == size);
    CHECK(fseek(input, 0, SEEK_SET) == 0);
    CHECK(repl_run(input, output, errors, interactive, storage) == 0);
    CHECK(fseek(output, 0, SEEK_SET) == 0);
    CHECK(fseek(errors, 0, SEEK_SET) == 0);
    size_t length = fread(output_text, 1, 8191, output);
    CHECK(!ferror(output) && feof(output));
    output_text[length] = '\0';
    length = fread(error_text, 1, 8191, errors);
    CHECK(!ferror(errors) && feof(errors));
    error_text[length] = '\0';
    CHECK(fclose(input) == 0);
    CHECK(fclose(output) == 0);
    CHECK(fclose(errors) == 0);
    CHECK(storage_close(storage) == DB_OK);
    CHECK(unlink(path) == 0);
}

int main(void)
{
    char output[8192];
    char errors[8192];
    const char session[] = "\n.help\r\nINSERT 1 \"Alexis\" 21\nSELECT 1\n"
                           "SELECT_ALL\nBAD\n.exit extra\n.exit\nSELECT 99\n";
    run_session(session, sizeof(session) - 1, false, output, errors);
    CHECK(strstr(output, "Commands (case-sensitive)") != NULL);
    CHECK(strstr(output, "OK: 1 row inserted") != NULL);
    CHECK(strstr(output, "1 | Alexis | 21") != NULL);
    CHECK(strstr(output, "2 | Seed | 20") != NULL);
    CHECK(strstr(errors, "record not found") == NULL);
    CHECK(strstr(output, "minidb>") == NULL);
    CHECK(strstr(errors, "unknown command") != NULL);
    CHECK(strstr(errors, "invalid syntax") != NULL);

    run_session("SELECT 7", 8, false, output, errors);
    CHECK(strstr(output, "7 | Seed | 20") != NULL);
    CHECK(errors[0] == '\0');
    run_session("", 0, true, output, errors);
    CHECK(strstr(output, "MiniDB v0.5") != NULL);
    CHECK(strstr(output, "minidb> ") != NULL);
    CHECK(errors[0] == '\0');

    char long_input[MINIDB_INPUT_MAX_BYTES + 128U];
    memset(long_input, ' ', MINIDB_INPUT_MAX_BYTES);
    memcpy(long_input, "SELECT_ALL", 10);
    long_input[MINIDB_INPUT_MAX_BYTES] = '\n';
    run_session(long_input, MINIDB_INPUT_MAX_BYTES + 1U, false, output, errors);
    CHECK(strstr(output, "2 | Seed | 20") != NULL);
    CHECK(errors[0] == '\0');

    memset(long_input, 'x', MINIDB_INPUT_MAX_BYTES + 1U);
    const char suffix[] = ".exit\nSELECT 9\n";
    memcpy(long_input + MINIDB_INPUT_MAX_BYTES + 1U, suffix, sizeof(suffix) - 1U);
    run_session(long_input, MINIDB_INPUT_MAX_BYTES + sizeof(suffix), false, output, errors);
    CHECK(strstr(output, "9 | Seed | 20") != NULL);
    CHECK(strstr(errors, "exceeds 4096 bytes") != NULL);
    CHECK(strstr(strchr(errors, '\n') + 1, "ERROR:") == NULL);

    const char nul_input[] = "SELECT 1\0.exit\nSELECT 2\n";
    run_session(nul_input, sizeof(nul_input) - 1, false, output, errors);
    CHECK(strstr(output, "1 |") == NULL);
    CHECK(strstr(output, "2 | Seed | 20") != NULL);
    CHECK(strstr(errors, "NUL byte") != NULL);

    memset(long_input, 'x', MINIDB_INPUT_MAX_BYTES + 1U);
    run_session(long_input, MINIDB_INPUT_MAX_BYTES + 1U, false, output, errors);
    CHECK(output[0] == '\0');
    CHECK(strstr(errors, "exceeds 4096 bytes") != NULL);

    char path[64];
    temporary_path(path);
    Storage *storage;
    CHECK(storage_open(path, &storage) == DB_OK);
    /* POSIX /dev/null opened in the wrong mode forces stream I/O errors. */
    FILE *write_only = fopen("/dev/null", "w");
    FILE *read_only = fopen("/dev/null", "r");
    FILE *sink = tmpfile();
    CHECK(write_only != NULL && read_only != NULL && sink != NULL);
    CHECK(repl_run(write_only, sink, sink, false, storage) == 1);
    CHECK(ferror(write_only));
    CHECK(repl_run(read_only, read_only, sink, true, storage) == 1);
    CHECK(ferror(read_only));
    CHECK(fclose(write_only) == 0);
    CHECK(fclose(read_only) == 0);
    CHECK(fclose(sink) == 0);
    CHECK(storage_close(storage) == DB_OK);
    CHECK(unlink(path) == 0);
    puts("REPL: help, exit, recovery, EOF, CRLF, NUL, line limits and I/O errors passed.");
    return 0;
}
