#include "engine/camera/game_camera.h"

#include "engine/camera/camera_config.h"

namespace sh::camera {

GameCamera& game_camera() {
    static GameCamera camera = [] {
        GameCamera c;
        const CameraConfig built_in{};
        c.default_watch = built_in.default_watch;
        c.user_camera = built_in.user_camera;
        return c;
    }();
    return camera;
}

}  // namespace sh::camera
