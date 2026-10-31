# SPDX-License-Identifier: GPL-3.0-or-later
# jig_embed_file(<input> <output.cpp> <symbol>): turns a binary file into a C++ source
# defining `extern const unsigned char <symbol>[]` and `extern const std::size_t
# <symbol>_size`. Used for the bundled font so the headless core needs no data files.
function(jig_embed_file input output symbol)
    file(READ "${input}" hex HEX)
    string(LENGTH "${hex}" n)
    math(EXPR bytes "${n} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," body "${hex}")
    string(REGEX REPLACE "((0x..,){32})" "\\1\n" body "${body}")
    file(WRITE "${output}.tmp"
        "// Generated from ${input} by cmake/EmbedFile.cmake. Do not edit.\n"
        "#include <cstddef>\n"
        "extern const unsigned char ${symbol}[];\n"
        "extern const std::size_t ${symbol}_size;\n"
        "const unsigned char ${symbol}[] = {\n${body}\n};\n"
        "const std::size_t ${symbol}_size = ${bytes};\n")
    # only touch the output when it changed, so builds stay incremental
    file(COPY_FILE "${output}.tmp" "${output}" ONLY_IF_DIFFERENT)
    file(REMOVE "${output}.tmp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
endfunction()
