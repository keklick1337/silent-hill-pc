find_package(Threads REQUIRED)

add_library(sh_import_ui STATIC strings.cpp input_check.cpp controller.cpp console.cpp picker.cpp)

target_compile_features(sh_import_ui PUBLIC cxx_std_20)
target_include_directories(sh_import_ui PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_import_ui PUBLIC sh_import sh_common Threads::Threads)
target_compile_options(sh_import_ui PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
