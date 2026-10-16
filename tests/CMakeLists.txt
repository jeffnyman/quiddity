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
# Later increments will pass the virtual machine libraries as the trailing
# arguments.

function(quiddity_add_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(ARGN)
        target_link_libraries(${name} PRIVATE ${ARGN})
    endif()
    quiddity_apply_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

quiddity_add_test(test_check test_check.c)
