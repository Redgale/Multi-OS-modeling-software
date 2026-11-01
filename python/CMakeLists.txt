# SPDX-License-Identifier: GPL-3.0-or-later
# The `jigsmith` Python module: the same commands as .jigs scripts, plus the design's
# parameters, steps, bodies and measurements, from Python. Built when pybind11 is found.
pybind11_add_module(jigsmith_py MODULE jigsmith.cpp)
set_target_properties(jigsmith_py PROPERTIES OUTPUT_NAME jigsmith LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/python)
target_link_libraries(jigsmith_py PRIVATE jig_core)
jig_set_warnings(jigsmith_py)
