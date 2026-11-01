# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(Jigsmith VERSION 0.1.0 LANGUAGES C CXX
        DESCRIPTION "Free, open-source parametric CAD for hobbyists")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(JIG_BUILD_GUI   "Build the Qt desktop application" ON)
option(JIG_BUILD_TESTS "Build unit and geometry tests" ON)
option(JIG_BUILD_BENCH "Build the benchmark suite" ON)
option(JIG_BUILD_PYTHON "Build the jigsmith Python module when pybind11 is found" ON)
set(JIG_SANITIZE "" CACHE STRING "Sanitizers for the core and tests: '', 'address,undefined' or 'thread'")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# Linux: write the library search path as RPATH rather than RUNPATH, so it also applies to
# the libraries' own dependencies. An OCCT built into a private prefix (scripts/build-occt.sh)
# has no runpath of its own, and libTKFeat must still find libTKBool beside it.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_link_options("LINKER:--disable-new-dtags")
endif()

include(cmake/Dependencies.cmake)
include(cmake/Warnings.cmake)

if(JIG_BUILD_PYTHON AND Python_FOUND AND pybind11_FOUND)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)  # the core goes into the Python module too
endif()
add_subdirectory(third_party/planegcs)
add_subdirectory(core)
add_subdirectory(cli)
if(JIG_BUILD_PYTHON AND Python_FOUND AND pybind11_FOUND)
    add_subdirectory(python)
endif()
if(JIG_BUILD_GUI)
    add_subdirectory(app)
endif()
if(JIG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(JIG_BUILD_BENCH)
    add_subdirectory(bench)
endif()
