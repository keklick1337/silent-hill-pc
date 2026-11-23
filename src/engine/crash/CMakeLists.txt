# The report of a fatal fault (crash_handler.h): what died and where, written before the process ends.
add_library(sh_crash STATIC crash_handler.cpp)
target_compile_features(sh_crash PUBLIC cxx_std_20)
target_include_directories(sh_crash PUBLIC ${PROJECT_SOURCE_DIR}/src)
# The report continues the log file, after the lines the journal had not written yet.
target_link_libraries(sh_crash PRIVATE sh_log)
if(NOT MSVC)
    target_compile_options(sh_crash PRIVATE -Wall -Wextra -Wpedantic)
endif()
set_target_properties(sh_crash PROPERTIES POSITION_INDEPENDENT_CODE ON)
