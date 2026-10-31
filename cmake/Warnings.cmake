# SPDX-License-Identifier: GPL-3.0-or-later
function(jig_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /bigobj
            /wd4251 /wd4275 /wd4996)
        target_compile_definitions(${target} PRIVATE NOMINMAX _USE_MATH_DEFINES)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
    if(JIG_SANITIZE AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=${JIG_SANITIZE} -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=${JIG_SANITIZE})
    endif()
endfunction()
