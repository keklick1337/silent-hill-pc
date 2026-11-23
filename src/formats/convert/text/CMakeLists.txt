add_library(sh_convert_text STATIC
    mini_json.cpp
    text_codec.cpp
    text_converter.cpp
    text_db.cpp
    text_layout.cpp
    text_markup.cpp)

target_compile_features(sh_convert_text PUBLIC cxx_std_20)
target_include_directories(sh_convert_text PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_convert_text PUBLIC sh_common)
target_compile_options(sh_convert_text PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
