# SPDX-License-Identifier: GPL-3.0-or-later
include(GoogleTest)
function(jig_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE jig_core GTest::gtest GTest::gtest_main)
    target_compile_definitions(${name} PRIVATE JIG_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    jig_set_warnings(${name})
    gtest_discover_tests(${name} DISCOVERY_TIMEOUT 120 PROPERTIES TIMEOUT 600)
endfunction()

jig_test(test_expr       test_expr.cpp)
jig_test(test_solver     test_solver.cpp)
jig_test(test_document   test_document.cpp)
jig_test(test_geometry   test_geometry.cpp)
jig_test(test_naming     test_naming.cpp)
jig_test(test_commands   test_commands.cpp)
jig_test(test_features   test_features.cpp)

# UI tests: drive the real main window, sketch editor and feature panels. They need
# a display (CI runs them under Xvfb on Linux) and carry the "ui" label so they can be
# selected or skipped with `ctest -L ui` / `ctest -LE ui`.
if(JIG_BUILD_GUI)
    add_executable(test_ui test_ui.cpp)
    set_target_properties(test_ui PROPERTIES AUTOMOC ON)
    target_link_libraries(test_ui PRIVATE jigsmith_ui Qt6::Test)
    target_compile_definitions(test_ui PRIVATE JIG_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    jig_set_warnings(test_ui)
    add_test(NAME ui.workflow COMMAND test_ui)
    set_tests_properties(ui.workflow PROPERTIES LABELS ui TIMEOUT 300)
endif()

# The command labels lupdate reads (app/registry_strings.h) must match the core's registry.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME translations.registry_strings_current
             COMMAND ${CMAKE_COMMAND} -DPYTHON=${Python3_EXECUTABLE} -DSRC=${PROJECT_SOURCE_DIR}
                     -DOUT=${CMAKE_CURRENT_BINARY_DIR}/registry_strings.h
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/check_registry_strings.cmake)
    add_test(NAME translations.core_strings_current
             COMMAND ${CMAKE_COMMAND} -DPYTHON=${Python3_EXECUTABLE} -DSRC=${PROJECT_SOURCE_DIR}
                     -DOUT=${CMAKE_CURRENT_BINARY_DIR}/core_strings.h -DCORE=1
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/check_registry_strings.cmake)
endif()

# the Python module, through Python itself
if(TARGET jigsmith_py)
    add_test(NAME python.api COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_python.py)
    set_tests_properties(python.api PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/python")
endif()
