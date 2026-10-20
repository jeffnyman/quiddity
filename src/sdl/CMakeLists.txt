# The quiddity-sdl executable: the windowed front end.
#
# It links the same machine as the terminal front end, and draws through
# SDL3 (see cmake/sdl.cmake). The screen it keeps, a grid of character
# cells, is a small library of its own with no SDL in it, so that the
# unit tests can check it without a window, and it is built whether or
# not the front end is, sanitizers included.

add_library(quiddity_grid STATIC
    grid.c
)

target_include_directories(quiddity_grid PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(quiddity_grid PUBLIC zmachine)
quiddity_apply_warnings(quiddity_grid)

if(NOT QUIDDITY_SDL)
    return()
endif()

# The font, built into the program as C source (cmake/embed.cmake),
# made again whenever the font file changes.
set(font_file "${PROJECT_SOURCE_DIR}/assets/fonts/NotoSansMono-Regular.ttf")
set(font_source "${CMAKE_CURRENT_BINARY_DIR}/font_regular.c")
add_custom_command(
    OUTPUT "${font_source}"
    COMMAND "${CMAKE_COMMAND}" -DINPUT=${font_file} -DOUTPUT=${font_source} -DNAME=font_regular
            -P "${PROJECT_SOURCE_DIR}/cmake/embed.cmake"
    DEPENDS "${font_file}" "${PROJECT_SOURCE_DIR}/cmake/embed.cmake"
    COMMENT "Embedding NotoSansMono-Regular.ttf"
    VERBATIM
)

add_executable(quiddity-sdl
    display.c
    main.c
    "${font_source}"
)

target_compile_definitions(quiddity-sdl PRIVATE
    QUIDDITY_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(quiddity-sdl PRIVATE
    quiddity_grid
    quiddity_files
    zmachine
    SDL3_ttf::SDL3_ttf
    SDL3_image::SDL3_image
    SDL3::SDL3
)

quiddity_apply_warnings(quiddity-sdl)
