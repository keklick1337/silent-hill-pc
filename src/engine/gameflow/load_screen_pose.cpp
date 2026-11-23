#include "engine/gameflow/load_screen_pose.h"

#include "engine/world/fixed_trig.h"
#include "formats/common/json.h"

namespace sh::gameflow {

namespace {

bool read_int(const json::Value& doc, const char* key, std::int32_t& out, std::string& err) {
    const json::Value* v = doc.find(key);
    if (v == nullptr) return true;
    if (!v->is_integer() || v->integer() < -(1 << 24) || v->integer() > (1 << 24)) {
        err = std::string("load_screen.json: \"") + key + "\" must be an integer";
        return false;
    }
    out = static_cast<std::int32_t>(v->integer());
    return true;
}

}  // namespace

Result<LoadScreenParams> LoadScreenParams::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) return Result<LoadScreenParams>::fail("load_screen.json: " + doc.error());
    LoadScreenParams p = builtin();
    std::string err;
    const json::Value& d = doc.value();
    if (!read_int(d, "look_height", p.look_height, err) || !read_int(d, "cam_height", p.cam_height, err) ||
        !read_int(d, "cam_angle_offset", p.cam_angle_offset, err) ||
        !read_int(d, "cam_distance_mul", p.cam_distance_mul, err) || !read_int(d, "pose_time", p.pose_time, err) ||
        !read_int(d, "player_height", p.player_height, err) || !read_int(d, "light_tint", p.light_tint, err)) {
        return Result<LoadScreenParams>::fail(err);
    }
    return Result<LoadScreenParams>::ok(p);
}

LoadScreenView load_screen_view(const LoadScreenParams& p, std::int32_t px, std::int32_t pz, std::int32_t yaw) {
    LoadScreenView v;
    v.look_at = {px, p.look_height, pz};
    const std::int32_t a = yaw + p.cam_angle_offset;
    v.cam_target.x = px - sh::world::fixed_sin(a) * p.cam_distance_mul;
    v.cam_target.y = p.cam_height;
    v.cam_target.z = pz - sh::world::fixed_cos(a) * p.cam_distance_mul;
    return v;
}

}  // namespace sh::gameflow
