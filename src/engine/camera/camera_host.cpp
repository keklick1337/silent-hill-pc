// The camera host registry (camera_host.h) and camera.json as the camera entry points read it.

#include "engine/camera/camera_host.h"

#include <string>

#include "engine/camera/camera_config.h"
#include "engine/camera/game_camera.h"
#include "engine/camera/view_system.h"
#include "engine/log/sh_log.h"

namespace sh::camera {

namespace {
class NeutralHost final : public CameraHost {
public:
    Matrix& world_screen() override { return world_screen_; }
    ScreenGeometry screen_geometry() override { return {}; }
    HorPlusWidths hor_plus_widths() override { return {}; }
    std::int32_t delta_time() override { return 0; }
    std::int32_t vblanks() override { return 0; }
    std::string data_path() override { return "gamedata"; }
    std::string mods_path() override { return "mods"; }
    HarryView harry() override { return {}; }
    FrameRecord* harry_head() override { return &head_; }
    int harry_clip(std::string_view, int fallback) override { return fallback; }
    void npcs(std::vector<NpcView>& out) override { out.clear(); }
    void set_npc_dead_time(std::size_t, std::int32_t) override {}
    GroundSample ground_at(std::int32_t, std::int32_t) override { return {}; }
    MapPlace map_place() override { return {}; }
    void set_fixed_angle_shot(bool) override {}

private:
    Matrix world_screen_;
    FrameRecord head_{};
};

CameraHost* s_host = nullptr;
}  // namespace

void set_camera_host(CameraHost* host) {
    s_host = host;
}

CameraHost& camera_host() {
    static NeutralHost neutral;
    return s_host != nullptr ? *s_host : neutral;
}

const CameraConfig& camera_config() {
    // The draw-side box test asks for it per test: the paths are built once.
    static const CameraConfig* s_config = nullptr;
    if (s_config == nullptr) {
        std::string error;
        s_config = &shared_camera_config(camera_host().data_path(), camera_host().mods_path(), &error);
        if (!error.empty()) {
            SH_ERR("[CAMERA] config/camera.json: %s (built-in numbers)", error.c_str());
        }
        for (const std::string& warning : s_config->warnings) {
            SH_ERR("[CAMERA] %s", warning.c_str());
        }
    }
    return *s_config;
}

const CameraConfig& camera_api_config() {
    static bool s_tuned = false;
    const CameraConfig& config = camera_config();
    if (!s_tuned) {
        s_tuned = true;
        game_camera().default_watch = config.default_watch;
        game_camera().user_camera = config.user_camera;
    }
    return config;
}

}  // namespace sh::camera

