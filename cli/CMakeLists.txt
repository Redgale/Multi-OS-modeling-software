# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(jigsmith-cli main.cpp)
target_link_libraries(jigsmith-cli PRIVATE jig_core)
jig_set_warnings(jigsmith-cli)
