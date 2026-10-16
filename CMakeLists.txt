cmake_minimum_required(VERSION 3.16)
project(MiniDB VERSION 0.5.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(MINIDB_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(minidb_options INTERFACE)
target_compile_options(minidb_options INTERFACE
    -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes)
if(MINIDB_SANITIZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizers require GCC or Clang")
    endif()
    target_compile_options(minidb_options INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(minidb_options INTERFACE -fsanitize=address,undefined)
endif()

add_library(minidb_core src/parser.c src/repl.c src/record.c src/status.c
    src/pager.c src/storage.c src/executor.c src/btree.c)
target_include_directories(minidb_core PUBLIC include)
target_link_libraries(minidb_core PUBLIC minidb_options)

add_executable(minidb src/main.c)
target_link_libraries(minidb PRIVATE minidb_core)

include(CTest)
if(BUILD_TESTING)
    foreach(suite parser repl record pager storage corruption btree btree_corruption queries mixed tombstones input_stress tree_mutations)
        add_executable(test_${suite} tests/test_${suite}.c)
        target_link_libraries(test_${suite} PRIVATE minidb_core)
        add_test(NAME ${suite} COMMAND test_${suite})
    endforeach()
    add_test(NAME btree_large COMMAND test_btree --large)
    add_test(NAME mixed_stress COMMAND test_mixed --stress)
    set_tests_properties(btree_large mixed_stress tree_mutations btree_corruption
        PROPERTIES LABELS "stress")
    # GNU-compatible linker wrapping keeps fault injection out of production.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(test_failures tests/test_failures.c)
        target_link_libraries(test_failures PRIVATE minidb_core)
        foreach(function calloc malloc open pread pwrite fsync close)
            target_link_options(test_failures PRIVATE "-Wl,--wrap=${function}")
        endforeach()
        add_test(NAME failures COMMAND test_failures)
    endif()
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    add_test(NAME process_model COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_process_model.py $<TARGET_FILE:minidb>)
    set_tests_properties(process_model PROPERTIES LABELS "integration" TIMEOUT 180)
    add_test(NAME cli_queries COMMAND ${CMAKE_COMMAND}
        -DMINIDB=$<TARGET_FILE:minidb>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_queries.cmake)
    add_test(NAME cli COMMAND ${CMAKE_COMMAND}
        -DMINIDB=$<TARGET_FILE:minidb>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.cmake)
endif()

option(MINIDB_BENCHMARKS "Build standalone benchmarks (not CTest tests)" OFF)
if(MINIDB_BENCHMARKS)
    add_executable(benchmark_lookup benchmarks/lookup.c)
    target_link_libraries(benchmark_lookup PRIVATE minidb_core)
    add_executable(benchmark_engine benchmarks/engine.c)
    target_link_libraries(benchmark_engine PRIVATE minidb_core)
endif()
