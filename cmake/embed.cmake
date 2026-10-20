# Turn a file into C source, so that it can be built into a program.
#
# C11 has no #embed, so the bytes are written out as an array. The SDL
# front end uses this for its font, which keeps the program a single file
# with nothing to be found beside it at run time.
#
# Run as a script at build time:
#
#     cmake -DINPUT=<file> -DOUTPUT=<file.c> -DNAME=<identifier> -P embed.cmake
#
# The source defines two objects, which a header declares as
#
#     extern const unsigned char NAME[];
#     extern const unsigned long NAME_size;

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" digits)
math(EXPR size "${digits} / 2")

# Every byte becomes "0xNN," and a line break follows each sixteen.
# CMake's regular expressions have no counted repetition, so the pattern
# for sixteen is written out.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REPEAT "0x..," 16 line)
string(REGEX REPLACE "(${line})" "\\1\n    " bytes "${bytes}")

get_filename_component(source "${INPUT}" NAME)
file(WRITE "${OUTPUT}"
"/* Generated from ${source} by cmake/embed.cmake; do not edit. */\n\n"
"const unsigned char ${NAME}[] = {\n    ${bytes}\n};\n\n"
"const unsigned long ${NAME}_size = ${size}UL;\n")
