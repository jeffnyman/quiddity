# The quiddity-sdl executable: the windowed front end.
#
# It links the same machine as the terminal front end, and draws through
# SDL3 (see cmake/sdl.cmake). For now it opens its window and nothing
# more; the machine comes into it a piece at a time.

add_executable(quiddity-sdl
    main.c
)

target_compile_definitions(quiddity-sdl PRIVATE
    QUIDDITY_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(quiddity-sdl PRIVATE
    zmachine
    SDL3_ttf::SDL3_ttf
    SDL3_image::SDL3_image
    SDL3::SDL3
)

quiddity_apply_warnings(quiddity-sdl)
