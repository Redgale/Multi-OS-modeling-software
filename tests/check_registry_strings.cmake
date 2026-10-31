# SPDX-License-Identifier: GPL-3.0-or-later
# Regenerates the translatable command strings and fails if app/registry_strings.h is stale.
execute_process(COMMAND ${PYTHON} ${SRC}/tools/extract_registry_strings.py ${SRC}/core/src/commands.cpp ${OUT}
                RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "tools/extract_registry_strings.py failed")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${OUT} ${SRC}/app/registry_strings.h RESULT_VARIABLE diff)
if(NOT diff EQUAL 0)
    message(FATAL_ERROR "app/registry_strings.h is out of date: run tools/extract_registry_strings.py "
                        "core/src/commands.cpp app/registry_strings.h, then update the translations")
endif()
