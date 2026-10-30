# SPDX-License-Identifier: LGPL-2.1-or-later
# Builds the vendored planegcs solver as a static library.
add_library(planegcs STATIC
    Sketcher/App/planegcs/Constraints.cpp
    Sketcher/App/planegcs/GCS.cpp
    Sketcher/App/planegcs/Geo.cpp
    Sketcher/App/planegcs/SubSystem.cpp
    Sketcher/App/planegcs/qp_eq.cpp
)
target_include_directories(planegcs
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/Sketcher/App
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shim ${CMAKE_CURRENT_SOURCE_DIR}/Sketcher)
target_link_libraries(planegcs PUBLIC Eigen3::Eigen Boost::headers)
target_compile_features(planegcs PUBLIC cxx_std_20)
set_target_properties(planegcs PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(planegcs PUBLIC _USE_MATH_DEFINES NOMINMAX)
    target_compile_options(planegcs PRIVATE /bigobj /W0)
else()
    target_compile_options(planegcs PRIVATE -w)
endif()
# Upstream planegcs uses C++23 library features (std::unreachable) in its .cpp files.
# Its headers stay C++20-clean, so only this target is built as C++23 (D-004).
set_target_properties(planegcs PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
