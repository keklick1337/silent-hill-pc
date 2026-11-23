# cmake -DIN=draw_usage.json -DOUT=draw_usage_data.cpp -P embed_usage.cmake
# A byte array rather than a string literal: compilers limit the length of one literal.
file(READ ${IN} SH_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," SH_BYTES "${SH_HEX}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" SH_BYTES "${SH_BYTES}")
file(WRITE ${OUT} "// Generated from draw_usage.json by embed_usage.cmake.\n"
    "namespace sh::convert::materials {\n"
    "namespace {\nconstexpr unsigned char kBytes[] = {\n${SH_BYTES}0x00};\n}\n"
    "extern const char* const kBuiltinDrawUsageJson;\n"
    "const char* const kBuiltinDrawUsageJson = reinterpret_cast<const char*>(kBytes);\n"
    "}  // namespace sh::convert::materials\n")
