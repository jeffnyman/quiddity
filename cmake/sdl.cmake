# The libraries the SDL front end is built on.
#
# SDL3 does the windows, input, and sound; SDL3_ttf draws text through
# FreeType; SDL3_image decodes the PNG and JPEG pictures that Blorb files
# hold. Each is built from a pinned source release, checked against its
# hash, so that every machine builds the same front end: no platform has
# SDL3 in the same place, and most have none. They are linked statically,
# which leaves a single executable to hand out. On Linux SDL still finds
# X11, Wayland, and the sound servers at run time, so one binary serves
# every desktop, provided their development headers were present when SDL
# was configured.
#
# A packager who would rather use the system's own copies sets
# QUIDDITY_SYSTEM_SDL, and the three are looked for with find_package.
#
# Each library defines its namespaced target: SDL3::SDL3,
# SDL3_ttf::SDL3_ttf, and SDL3_image::SDL3_image.

option(QUIDDITY_SYSTEM_SDL "Use the installed SDL3, SDL3_ttf, and SDL3_image instead of building them" OFF)

if(QUIDDITY_SYSTEM_SDL)
    find_package(SDL3 3.4 CONFIG REQUIRED)
    find_package(SDL3_ttf 3.2 CONFIG REQUIRED)
    find_package(SDL3_image 3.4 CONFIG REQUIRED)
    return()
endif()

include(FetchContent)

# Static libraries only, and none of the libraries' own tests, samples,
# or install rules, which would otherwise build with the project.
set(BUILD_SHARED_LIBS OFF)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    SYSTEM)

# FreeType with none of its optional companions: the fonts the front end
# draws with need no compression, colour bitmaps, or text shaping.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)

# SDL3_ttf's release archive leaves out the FreeType it can build, so
# FreeType comes from its own release, standing in for the system's when
# SDL3_ttf asks for it with find_package.
FetchContent_Declare(freetype
    URL https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.gz
    URL_HASH SHA256=e61b31ab26358b946e767ed7eb7f4bb2e507da1cfefeb7a8861ace7fd5c899a1
    SYSTEM
    OVERRIDE_FIND_PACKAGE)

set(SDLTTF_VENDORED OFF CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3_ttf
    URL https://github.com/libsdl-org/SDL_ttf/releases/download/release-3.2.2/SDL3_ttf-3.2.2.tar.gz
    URL_HASH SHA256=63547d58d0185c833213885b635a2c0548201cc8f301e6587c0be1a67e1e045d
    SYSTEM)

# PNG and JPEG only, decoded by the stb code SDL3_image carries, so that
# it needs no other library.
set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)
foreach(format ANI AVIF BMP GIF JXL LBM PCX PNM QOI SVG TGA TIF WEBP XCF XPM XV)
    set(SDLIMAGE_${format} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3_image
    URL https://github.com/libsdl-org/SDL_image/releases/download/release-3.4.8/SDL3_image-3.4.8.tar.gz
    URL_HASH SHA256=e8223b424bc7541cf84ffff5cf7e4f24ec3711c60462cb1bd6dc1d7bd7f25477
    SYSTEM)

FetchContent_MakeAvailable(SDL3 freetype)
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()
FetchContent_MakeAvailable(SDL3_ttf SDL3_image)
