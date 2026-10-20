# Quiddity: an interactive fiction interpreter for the Z-Machine and Glulx.
#
# This is the top-level build. It establishes the language standard and the
# warning policy, then hands off to the subdirectories that own the actual
# targets. Everything is C11 and must build cleanly, with warnings treated as
# errors, under GCC, Clang, and MSVC.

cmake_minimum_required(VERSION 3.25)

# On macOS the build is pinned to the SDK. Apple's clang otherwise searches
# /usr/local/include, where Homebrew keeps its headers, before any -isystem
# directory, so an SDL3 installed there would be compiled into the SDL3 the
# build fetches, and the two versions clash. With a sysroot, that directory
# is looked for inside the SDK instead, where it does not exist. CMake has
# to know before project(), since that is where the compiler is first run,
# and a value given by hand, or an SDKROOT in the environment, is left as
# it is.
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_SYSROOT AND "x$ENV{SDKROOT}" STREQUAL "x")
    set(CMAKE_OSX_SYSROOT "macosx")
endif()

project(quiddity
    VERSION 0.1.0
    DESCRIPTION "A specification-accurate Z-Machine and Glulx interpreter"
    HOMEPAGE_URL "https://github.com/jeffnyman/quiddity"
    LANGUAGES C
)

# The core is written against ISO C11 with no compiler extensions. Keeping
# extensions off is what makes the "portable across GCC, Clang, and MSVC"
# promise checkable: anything non-standard fails on at least one of them.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Emit build/<preset>/compile_commands.json so editors and tools such as
# clangd can see the exact flags and definitions each file is compiled with.
# Only the Ninja and Makefile generators honour this; the Visual Studio
# generator ignores it, which is why VS Code is pointed at CMake Tools as
# its configuration provider in .vscode/c_cpp_properties.json instead.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Single-configuration generators (Ninja, Makefiles) need a build type to
# pick optimisation and debug flags. Default to Debug while the project is
# young; presets override this explicitly.
get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT is_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Put every executable in build/<preset>/bin rather than mirroring the source
# tree, so the thing to run is always in one known place. Multi-config
# generators such as Visual Studio add a configuration subdirectory, giving
# build/msvc/bin/Debug/quiddity.exe.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(QUIDDITY_BUILD_TESTS "Build the unit tests" ON)
option(QUIDDITY_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang only)" OFF)
option(QUIDDITY_SDL "Build the SDL front end, quiddity-sdl" OFF)

# MSVC links its C runtime into each program rather than leaving it to a
# DLL, so that what is built runs on a machine without the Visual C++
# redistributable. Every target must agree on this, the libraries the
# SDL front end is built on included, so it is set for the whole build.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(cmake/warnings.cmake)

if(QUIDDITY_SDL)
    include(cmake/sdl.cmake)
endif()

add_subdirectory(src/zmachine)
add_subdirectory(src/quiddity)

if(QUIDDITY_SDL)
    add_subdirectory(src/sdl)
endif()

if(QUIDDITY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
