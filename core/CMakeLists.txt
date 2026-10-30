# SPDX-License-Identifier: GPL-3.0-or-later
# jig_core: the headless library. No Qt, no window system (architecture rule 1).
configure_file(version.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/jig/version.hpp @ONLY)

add_library(jig_core STATIC
    src/commands.cpp
    src/document.cpp
    src/expr.cpp
    src/features.cpp
    src/io.cpp
    src/kernel.cpp
    src/sketch.cpp
)
target_include_directories(jig_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(jig_core
    PUBLIC  jig_occt nlohmann_json::nlohmann_json
    PRIVATE planegcs jig_miniz)
target_compile_features(jig_core PUBLIC cxx_std_20)
jig_set_warnings(jig_core)
