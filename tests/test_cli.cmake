set(input "${TEST_DIR}/cli-input.txt")
set(database "${TEST_DIR}/phase1-must-not-create.db")
if(EXISTS "${database}")
    message(FATAL_ERROR "Unexpected pre-existing test database: ${database}")
endif()
file(WRITE "${input}" "INSERT 1 \"Alexis\" 21\nSELECT 1\nSELECT_ALL\n.exit\n")
execute_process(COMMAND "${MINIDB}" "${database}" INPUT_FILE "${input}"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT output MATCHES "PARSED: INSERT 1"
    OR NOT output MATCHES "PARSED: SELECT 1" OR NOT output MATCHES "PARSED: SELECT_ALL"
    OR NOT errors MATCHES "does not open or create" OR EXISTS "${database}")
    message(FATAL_ERROR "CLI session failed (${status}): ${output} ${errors}")
endif()
execute_process(COMMAND "${MINIDB}" --help
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT output MATCHES "Usage:")
    message(FATAL_ERROR "CLI help failed (${status}): ${output} ${errors}")
endif()
execute_process(COMMAND "${MINIDB}" one.db two.db
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status)
if(NOT status EQUAL 1 OR NOT errors MATCHES "Usage:")
    message(FATAL_ERROR "CLI argument validation failed (${status}): ${errors}")
endif()
execute_process(COMMAND "${MINIDB}" --unknown
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status)
if(NOT status EQUAL 1 OR NOT errors MATCHES "Usage:")
    message(FATAL_ERROR "CLI unknown-option validation failed (${status}): ${errors}")
endif()
