set(input "${TEST_DIR}/queries-input.txt")
set(database "${TEST_DIR}/queries-persistence.db")
file(REMOVE "${database}")
file(WRITE "${input}" "INSERT 3 \"Mariana\" 25\nINSERT 1 \"Alexis\" 21\nINSERT 2 \"Carlos\" 20\nUPDATE 1 \"Alexis Perez\" 22\nDELETE 2\nSELECT_ALL WHERE age >= 21\nSELECT 2\nDELETE 2\nUPDATE 2 \"Deleted\" 1\nUPDATE 9 \"Missing\" 1\nSELECT_ALL WHERE age > 150\n.exit\n")
execute_process(COMMAND "${MINIDB}" "${database}" INPUT_FILE "${input}"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status)
set(expected "OK: 1 row inserted\nOK: 1 row inserted\nOK: 1 row inserted\nOK: 1 row updated\nOK: 1 row deleted\n1 | Alexis Perez | 22\n3 | Mariana | 25\nOK: 0 rows\n")
set(expected_errors "ERROR: record is logically deleted\nERROR: record is logically deleted\nERROR: record is logically deleted\nERROR: record not found\n")
if(NOT status EQUAL 0 OR NOT output STREQUAL expected OR NOT errors STREQUAL expected_errors)
    message(FATAL_ERROR "Query session failed (${status}): ${output} ${errors}")
endif()
file(WRITE "${input}" "SELECT 1\nSELECT 2\nSELECT_ALL WHERE age != 22\n.stats\nINSERT 2 \"Carlos returned\" 23\nSELECT_ALL WHERE age <= 23\n")
execute_process(COMMAND "${MINIDB}" "${database}" INPUT_FILE "${input}"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT output MATCHES "1 \\| Alexis Perez \\| 22"
    OR NOT output MATCHES "3 \\| Mariana \\| 25"
    OR NOT output MATCHES "records=3 active=2 deleted=1 index_entries=3"
    OR NOT output MATCHES "2 \\| Carlos returned \\| 23"
    OR NOT errors STREQUAL "ERROR: record is logically deleted\n")
    message(FATAL_ERROR "Query reopen failed (${status}): ${output} ${errors}")
endif()
file(WRITE "${input}" "SELECT 2\n.stats\nSELECT_ALL WHERE age == 1\nUPDATE 1 \"Invalid\" 151\nSELECT 1\n.exit\n")
execute_process(COMMAND "${MINIDB}" "${database}" INPUT_FILE "${input}"
    OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT output MATCHES "2 \\| Carlos returned \\| 23"
    OR NOT output MATCHES "records=3 active=3 deleted=0 index_entries=3"
    OR NOT output MATCHES "1 \\| Alexis Perez \\| 22"
    OR NOT errors MATCHES "invalid syntax" OR NOT errors MATCHES "number out of range")
    message(FATAL_ERROR "Reactivation/invalid-input persistence failed (${status}): ${output} ${errors}")
endif()
file(REMOVE "${database}" "${input}")
