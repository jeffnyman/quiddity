# The quiddity executable: the terminal front end.
#
# This target owns argument handling and the terminal I/O layer. The virtual
# machines live in separate static libraries that this target links against,
# so that a future windowed front end can reuse them unchanged.
#
# The acceptance runner, which plays a script through a stream front end
# and checks the play against a recording, is a small library of its own,
# so that the unit tests can drive it without the executable's main().

add_library(quiddity_accept STATIC
    accept.c
    script.c
    utf8.c
)

target_include_directories(quiddity_accept PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(quiddity_accept PUBLIC zmachine)
quiddity_apply_warnings(quiddity_accept)

# The rules for naming saved games and auxiliary files, which every front
# end shares.
add_library(quiddity_files STATIC
    files.c
)

target_include_directories(quiddity_files PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
quiddity_apply_warnings(quiddity_files)

add_executable(quiddity
    info.c
    main.c
    terminal.c
)

target_compile_definitions(quiddity PRIVATE
    QUIDDITY_VERSION="${PROJECT_VERSION}"
)

# The runner comes first: it carries zmachine as a public dependency, and
# naming the machine after it keeps the static libraries in dependency
# order, so the linker is not handed zmachine twice. The front end uses
# the machine directly as well, which is why it is still named here.
target_link_libraries(quiddity PRIVATE quiddity_accept quiddity_files zmachine)

quiddity_apply_warnings(quiddity)
