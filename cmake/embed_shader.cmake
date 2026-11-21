file(READ "${INPUT}" bytes HEX)

if(NULL_TERMINATE)
	string(APPEND bytes "00")
endif()
string(LENGTH "${bytes}" hex_length)
math(EXPR byte_length "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
string(REGEX REPLACE
	"(0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],)"
	"\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}"
	"static const union {\n"
	"    unsigned char bytes[${byte_length}];\n"
	"    unsigned long alignment;\n"
	"} ${SYMBOL} = {{\n"
	"${bytes}\n"
	"}};\n")
