add_library(sh_common STATIC byte_source.cpp json.cpp)
target_compile_features(sh_common PUBLIC cxx_std_20)
target_include_directories(sh_common PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(sh_common PRIVATE -Wall -Wextra -Wpedantic)
