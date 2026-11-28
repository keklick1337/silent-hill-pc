# sh_camera: the camera of the engine: stage A is the zone selection
# (which camera zone owns the frame, the nearest enemy, the through-door rail and the view-button
# bookkeeping), stage B the camera target and position, stage C the look-at, rotation and view
# output.
# camera_api.cpp is the camera API between frames (CameraRig methods), camera_driver.cpp the
# driver rules (map-start camera, subject, head point, developer camera, listener direction).
# view_system.cpp holds the view frame and the world-screen matrices; camera_entry.cpp and
# view_entry.cpp are the C entry points the game calls (vcExecCamera, the camera API, the
# driver, the vw*/Vw_* view calls), reading the game through camera_host.h (the game's host is
# src/engine/camera/game/camera_game_host.cpp);
# sh_world supplies the bit-exact fixed-point trigonometry. alt_camera.cpp holds the PC control-style cameras (third person, first person),
# driven by the game's alt-camera bridge; sh_mods finds a mod's alt_cameras.json,
# sh_gameflow reads config/camera.json (base or mod).
add_library(sh_camera STATIC
    camera_rig.cpp
    camera_motion.cpp
    camera_target.cpp
    camera_matrix.cpp
    camera_view.cpp
    view_space.cpp
    camera_config.cpp
    camera_map_rules.cpp
    self_view_gaze.cpp
    camera_api.cpp
    camera_driver.cpp
    game_camera.cpp
    camera_host.cpp
    view_system.cpp
    precise_view.cpp
    view_entry.cpp
    camera_entry.cpp
    alt_camera.cpp)

target_compile_features(sh_camera PRIVATE cxx_std_20)
target_include_directories(sh_camera PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sh_camera PRIVATE sh_world sh_entities sh_common sh_mods sh_gameflow sh_maps sh_audio sh_log)
target_compile_options(sh_camera PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>)
