# The zmachine library: the Z-Machine virtual machine core.
#
# This is the "substance" of the interpreter. It depends on nothing but the
# C standard library, performs no I/O of its own beyond reading the story
# file, and never writes to the screen. Front ends link against it and
# supply the presentation. Public headers live under include/zm and are
# included as "zm/<name>.h".

add_library(zmachine STATIC
    fatal.c
    header.c
    instruction.c
    machine.c
    memory.c
    object.c
    status.c
    text.c
)

target_include_directories(zmachine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

quiddity_apply_warnings(zmachine)
