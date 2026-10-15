cmake_minimum_required(VERSION 3.16)
project(MiniDB VERSION 0.2.0 LANGUAGES C)

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
    src/pager.c src/storage.c src/executor.c)
target_include_directories(minidb_core PUBLIC include)
target_link_libraries(minidb_core PUBLIC minidb_options)

add_executable(minidb src/main.c)
target_link_libraries(minidb PRIVATE minidb_core)

include(CTest)
if(BUILD_TESTING)
    foreach(suite parser repl record pager storage corruption)
        add_executable(test_${suite} tests/test_${suite}.c)
        target_link_libraries(test_${suite} PRIVATE minidb_core)
        add_test(NAME ${suite} COMMAND test_${suite})
    endforeach()
    # GNU-compatible linker wrapping keeps fault injection out of production.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(test_failures tests/test_failures.c)
        target_link_libraries(test_failures PRIVATE minidb_core)
        foreach(function calloc malloc open pread pwrite fsync close)
            target_link_options(test_failures PRIVATE "-Wl,--wrap=${function}")
        endforeach()
        add_test(NAME failures COMMAND test_failures)
    endif()
    add_test(NAME cli COMMAND ${CMAKE_COMMAND}
        -DMINIDB=$<TARGET_FILE:minidb>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.cmake)
endif()
