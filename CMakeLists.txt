# CMakeLists.txt -- build the mini search engine (mse). See plan.md section 13.
#
#   cmake -S . -B build          configure (out-of-source; build/ is gitignored)
#   cmake --build build          build build/mse
#   ctest --test-dir build       run the unit tests
#   cmake --build build --target clean
#
# Useful options:
#   -G Ninja                      pick a generator (default: platform default)
#   -DCMAKE_BUILD_TYPE=Debug      Debug / Release / RelWithDebInfo / MinSizeRel
#   -DCMAKE_CXX_STANDARD=20       try a newer language level (default: 17)
#   -DMSE_WERROR=ON               warnings are errors (acceptance gate: zero warnings)
#   -DBUILD_TESTING=OFF           build mse only, skip tests/
#
# The tool resolves its inputs relative to the working directory -- by default
# data/manifest.tsv (see src/main.cpp) -- so run it from this directory:
#   ./build/mse stats
# or pass an explicit path:  ./build/mse stats --manifest /path/to/manifest.tsv
# With a multi-configuration generator (Visual Studio) the binary lands in
# build/<Config>/mse.exe instead of build/mse.

cmake_minimum_required(VERSION 3.16)

project(mse
    VERSION 0.1.0
    DESCRIPTION "Mini search engine over the complete works of Shakespeare"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# language and build-type defaults
# ---------------------------------------------------------------------------

# C++17 is the language level the project is written against (plan.md 13).
# An explicit -DCMAKE_CXX_STANDARD=... on the command line still wins.
if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # -std=c++17, not -std=gnu++17

# The project has always been built with -O2; make that the default for
# single-configuration generators instead of an empty (unoptimized) build type.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# clangd and other editors read build/compile_commands.json.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# warnings -- plan.md 13: -Wall -Wextra, zero warnings
# ---------------------------------------------------------------------------

# Collected in an INTERFACE library so the tool, the core library and every
# test are compiled with exactly the same flags.
add_library(mse_warnings INTERFACE)

if(MSVC)
    target_compile_options(mse_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(mse_warnings INTERFACE -Wall -Wextra)
endif()

option(MSE_WERROR "Turn compiler warnings into errors" OFF)
if(MSE_WERROR)
    if(MSVC)
        target_compile_options(mse_warnings INTERFACE /WX)
    else()
        target_compile_options(mse_warnings INTERFACE -Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# mse_core -- everything except the CLI entry point
# ---------------------------------------------------------------------------

# One .cpp per module in src/, with src/main.cpp kept out: it is the command
# line entry point and is linked into the `mse` executable only. When a new
# module arrives as include/xxx.hpp + src/xxx.cpp, add its .cpp to this list.
add_library(mse_core STATIC
    src/corpus.cpp
    src/tokenizer.cpp
    src/count.cpp
    src/stopwords.cpp)

# PUBLIC: everything linking mse_core may also `#include "corpus.hpp"`.
target_include_directories(mse_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# src/count.cpp uses <filesystem>, which lived in a separate archive on GCC 8.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(mse_core PUBLIC stdc++fs)
endif()

target_link_libraries(mse_core PRIVATE mse_warnings)

# ---------------------------------------------------------------------------
# mse -- the command line tool
# ---------------------------------------------------------------------------

add_executable(mse src/main.cpp)
target_link_libraries(mse PRIVATE mse_core mse_warnings)

# ---------------------------------------------------------------------------
# tests -- run with `ctest --test-dir build`
# ---------------------------------------------------------------------------

include(CTest) # defines the BUILD_TESTING option and calls enable_testing()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
