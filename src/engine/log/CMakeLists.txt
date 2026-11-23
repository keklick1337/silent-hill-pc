# Categorized journal: C++20, with the C facade sh_log.h.
add_library(sh_log STATIC log.cpp)
target_compile_features(sh_log PUBLIC cxx_std_20)
target_include_directories(sh_log PUBLIC ${PROJECT_SOURCE_DIR}/src)
# The log file name comes from the game manifest (log_prefix) when present.
target_link_libraries(sh_log PRIVATE sh_manifest)
if(NOT MSVC)
    target_compile_options(sh_log PRIVATE -Wall -Wextra -Wpedantic)
endif()
# Position-independent like every module linked into the exporting executable.
set_target_properties(sh_log PROPERTIES POSITION_INDEPENDENT_CODE ON)

