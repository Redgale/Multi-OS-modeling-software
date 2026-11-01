# SPDX-License-Identifier: GPL-3.0-or-later
# jig_core: the headless library. No Qt, no window system (architecture rule 1).
include(${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake)
jig_embed_file(${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.otf
               ${CMAKE_CURRENT_BINARY_DIR}/generated/font_inter_regular.cpp jig_font_inter_regular)
configure_file(version.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/jig/version.hpp @ONLY)

add_library(jig_core STATIC
    src/commands.cpp
    src/document.cpp
    src/dxf.cpp
    src/expr.cpp
    src/features.cpp
    src/io.cpp
    src/kernel.cpp
    src/sketch.cpp
    src/sketch_offset.cpp
    src/sketch_edit.cpp
    src/curve_constraints.cpp
    src/screws.cpp
    src/svg.cpp
    src/text.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/font_inter_regular.cpp
)
target_include_directories(jig_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(jig_core
    PUBLIC  jig_occt nlohmann_json::nlohmann_json
    PRIVATE planegcs jig_miniz Freetype::Freetype)
target_compile_features(jig_core PUBLIC cxx_std_20)

if(TARGET PkgConfig::HARFBUZZ)  # text shaping (cmake/Dependencies.cmake)
    target_link_libraries(jig_core PRIVATE PkgConfig::HARFBUZZ)
    target_compile_definitions(jig_core PRIVATE JIG_HAVE_HARFBUZZ=1)
endif()
jig_set_warnings(jig_core)
