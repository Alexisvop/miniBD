set(input "${TEST_DIR}/cli-input.txt")
set(database "${TEST_DIR}/cli-persistence.db")
file(REMOVE "${database}")
file(WRITE "${input}" "INSERT 2 \"Carlos\" 22\nINSERT 1 \"Alexis\" 21\nSELECT 1\nSELECT_ALL\n.exit\n")
execute_process(COMMAND "${MINIDB}" "${database}" INPUT_FILE "${input}"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT output MATCHES "OK: 1 row inserted"
    OR NOT output MATCHES "1 \\| Alexis \\| 21" OR NOT output MATCHES "2 \\| Carlos \\| 22"
    OR NOT errors STREQUAL "" OR NOT EXISTS "${database}")
    message(FATAL_ERROR "CLI session failed (${status}): ${output} ${errors}")
endif()
file(WRITE "${input}" "SELECT 1\nSELECT_ALL\nINSERT 1 \"Duplicate\" 99\nSELECT 999\nINSERT 3 \"EOF saved\" 30")
execute_process(COMMAND "${MINIDB}" "${database}" INPUT_FILE "${input}"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT output MATCHES "1 \\| Alexis \\| 21"
    OR NOT output MATCHES "2 \\| Carlos \\| 22" OR NOT errors MATCHES "duplicate ID"
    OR NOT errors MATCHES "record not found" OR NOT output MATCHES "OK: 1 row inserted")
    message(FATAL_ERROR "Reopened CLI session failed (${status}): ${output} ${errors}")
endif()
file(WRITE "${input}" "SELECT_ALL\n.exit\n")
execute_process(COMMAND "${MINIDB}" "${database}" INPUT_FILE "${input}"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT output STREQUAL "1 | Alexis | 21\n2 | Carlos | 22\n3 | EOF saved | 30\n"
    OR NOT errors STREQUAL "")
    message(FATAL_ERROR "EOF persistence failed (${status}): ${output} ${errors}")
endif()
execute_process(COMMAND "${MINIDB}" --help
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT output MATCHES "Usage:")
    message(FATAL_ERROR "CLI help failed (${status}): ${output} ${errors}")
endif()
foreach(arguments "one.db;two.db" "--unknown" "")
    execute_process(COMMAND "${MINIDB}" ${arguments}
        OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status)
    if(NOT status EQUAL 1 OR NOT errors MATCHES "Usage:")
        message(FATAL_ERROR "CLI argument validation failed (${status}): ${errors}")
    endif()
endforeach()
file(WRITE "${input}" ".stats\n.btree\n.exit\n")
execute_process(COMMAND "${MINIDB}" "${database}" INPUT_FILE "${input}"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT output MATCHES "records=3"
    OR NOT output MATCHES "leaf root keys=3"
    OR NOT output MATCHES "Session splits: leaf=0 internal=0 root=0" OR NOT errors STREQUAL "")
    message(FATAL_ERROR "Inspection commands failed (${status}): ${output} ${errors}")
endif()
file(WRITE "${database}" "not a database")
execute_process(COMMAND "${MINIDB}" "${database}" INPUT_FILE "${input}"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status)
file(READ "${database}" contents)
if(NOT status EQUAL 1 OR NOT errors MATCHES "invalid or truncated"
    OR NOT contents STREQUAL "not a database")
    message(FATAL_ERROR "Invalid file was not safely rejected (${status}): ${errors}")
endif()
file(REMOVE "${database}" "${input}")
