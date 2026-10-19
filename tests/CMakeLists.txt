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
quiddity_add_test(test_text   test_text.c   zmachine)
quiddity_add_test(test_dictionary test_dictionary.c zmachine)
quiddity_add_test(test_instruction test_instruction.c zmachine)
quiddity_add_test(test_object test_object.c zmachine)
quiddity_add_test(test_machine test_machine.c zmachine)
quiddity_add_test(test_quetzal test_quetzal.c zmachine)
quiddity_add_test(test_blorb   test_blorb.c   zmachine)
quiddity_add_test(test_accept  test_accept.c  quiddity_accept)
quiddity_add_test(test_utf8    test_utf8.c    quiddity_accept)

# Acceptance scripts (see src/quiddity/script.h). Each script under
# acceptance/ is a test of its own that plays it and compares the play with
# the recording beside it. The games most of them name live outside the
# repository, in the directory QUIDDITY_GAMES_DIR points at, and a script
# whose game is not there is reported as skipped rather than failed, which
# is what the runner's exit status of 77 tells CTest.
#
# The glob is re-evaluated when the build runs, so a new script needs no
# CMake change, only a rebuild. Scripts may be grouped in subdirectories,
# and a test is named after its path under acceptance/ with the slashes
# turned to dots, so checkers/czech-z3.accept becomes accept_checkers.czech-z3
# and ctest -R checkers runs the group.

set(QUIDDITY_GAMES_DIR "${CMAKE_SOURCE_DIR}/../entharion" CACHE PATH
    "Where the story files that acceptance scripts name are kept")

file(GLOB_RECURSE acceptance_scripts CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/acceptance/*.accept")
foreach(script IN LISTS acceptance_scripts)
    file(RELATIVE_PATH stem "${CMAKE_SOURCE_DIR}/acceptance" "${script}")
    string(REGEX REPLACE "\\.accept$" "" stem "${stem}")
    string(REPLACE "/" "." stem "${stem}")
    add_test(NAME accept_${stem}
             COMMAND quiddity --accept "${script}" --games "${QUIDDITY_GAMES_DIR}")
    set_tests_properties(accept_${stem} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
