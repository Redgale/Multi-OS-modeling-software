# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(jigsmith-bench bench_main.cpp)
target_link_libraries(jigsmith-bench PRIVATE jig_core)
target_compile_definitions(jigsmith-bench PRIVATE JIG_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
jig_set_warnings(jigsmith-bench)
