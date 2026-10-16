# Unit tests.
#
# Each test is a standalone executable built from one C file plus the
# check.h harness, registered with CTest by name. A test passes when its
# process exits with status zero; check.h takes care of that.
#
# Usage:
#
#     quiddity_add_test(<name> <source> [<library>...])
#
# Every test can find the story files in tests/data through the
# QUIDDITY_TEST_DATA_DIR macro, an absolute path with no trailing slash.

function(quiddity_add_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${name} PRIVATE
        QUIDDITY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    )
    if(ARGN)
        target_link_libraries(${name} PRIVATE ${ARGN})
    endif()
    quiddity_apply_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

quiddity_add_test(test_check  test_check.c)
quiddity_add_test(test_memory test_memory.c zmachine)
quiddity_add_test(test_header test_header.c zmachine)
