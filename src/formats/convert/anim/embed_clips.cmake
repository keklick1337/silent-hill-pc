# cmake -DIN=anim_clips.json -DOUT=anim_clips_data.cpp -P embed_clips.cmake
# A byte array rather than a string literal: compilers limit the length of one literal.
file(READ ${IN} SH_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," SH_BYTES "${SH_HEX}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" SH_BYTES "${SH_BYTES}")
file(WRITE ${OUT} "// Generated from anim_clips.json by embed_clips.cmake.\n"
    "namespace sh::convert::anim {\n"
    "namespace {\nconstexpr unsigned char kBytes[] = {\n${SH_BYTES}0x00};\n}\n"
    "extern const char* const kBuiltinClipsJson;\n"
    "const char* const kBuiltinClipsJson = reinterpret_cast<const char*>(kBytes);\n"
    "}  // namespace sh::convert::anim\n")
