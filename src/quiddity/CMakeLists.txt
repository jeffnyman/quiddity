# The quiddity executable: the terminal front end.
#
# This target owns argument handling and the terminal I/O layer. The virtual
# machines themselves will live in separate static libraries that this
# target links against, so that a future windowed front end can reuse them
# unchanged.

add_executable(quiddity
    main.c
)

target_compile_definitions(quiddity PRIVATE
    QUIDDITY_VERSION="${PROJECT_VERSION}"
)

quiddity_apply_warnings(quiddity)
