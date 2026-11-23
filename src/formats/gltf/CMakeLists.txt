add_library(sh_gltf STATIC
    gltf_writer.cpp
    gltf_inspect.cpp
    gltf_skin_reader.cpp
    glb_packed.cpp
    cgltf_impl.cpp)

target_compile_features(sh_gltf PUBLIC cxx_std_20)
target_include_directories(sh_gltf PUBLIC ${PROJECT_SOURCE_DIR}/src)
# cgltf is an implementation detail of gltf_inspect; its symbols are renamed (cgltf_prefixed.h).
target_include_directories(sh_gltf PRIVATE ${PROJECT_SOURCE_DIR}/third_party/cgltf/include)
target_link_libraries(sh_gltf PUBLIC sh_common)
target_compile_options(sh_gltf PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
# The vendored single-header library is not ours to fix.
set_source_files_properties(cgltf_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
