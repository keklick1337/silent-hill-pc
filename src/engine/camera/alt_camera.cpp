#include "engine/camera/alt_camera.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <vector>

#include "engine/camera/camera_math.h"
#include "engine/mods/data_merge.h"
#include "engine/world/fixed_math.h"
#include "formats/common/json.h"

namespace sh::camera {

namespace {

// Single precision, truncated, as the game's constant macros.
std::int32_t to_q12(float units) {
    volatile float scaled = units * 4096.0f;
    return static_cast<std::int32_t>(scaled);
}

std::int32_t to_angle(float degrees) {
    volatile float turns = degrees / 360.0f;
    return static_cast<std::int16_t>(to_q12(turns));
}

// An angle folded to [-2048, 2047] after the game's 16-bit truncation.
std::int16_t signed_angle(std::int32_t a) {
    const std::int32_t n = static_cast<std::int16_t>(a) & 0xFFF;
    return static_cast<std::int16_t>(n > 2048 ? (n | 0xF000) : n);
}

std::int32_t wide(std::int64_t a, std::int32_t b) {
    return static_cast<std::int32_t>((a * b) >> 12);
}

std::int32_t sq(std::int32_t v) {
    return fx::wrap_mul(v, v);
}

std::int32_t sqrt3(std::int32_t x, std::int32_t y, std::int32_t z) {
    return world::game_sqrt(static_cast<std::int32_t>(static_cast<std::uint32_t>(sq(x)) + static_cast<std::uint32_t>(sq(y)) +
                                                      static_cast<std::uint32_t>(sq(z))));
}

// A per-30-fps-frame amount scaled to this frame's step (the 30 fps step is Q12(1/30) = 136).
std::int32_t per_frame(std::int32_t amount, std::int32_t delta_time) {
    return (amount * delta_time) / 136;
}

// --- tuning file ---------------------------------------------------------------------------

using json::Value;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

std::string at(const Value& v, std::string_view what) {
    return "line " + std::to_string(v.line()) + ", column " + std::to_string(v.column()) + ": " + std::string(what);
}

const Value& member(const Value& parent, std::string_view key) {
    const Value* v = parent.find(key);
    if (v == nullptr) {
        fail(at(parent, "missing \"" + std::string(key) + "\""));
    }
    return *v;
}

void reject_unknown(const Value& object, std::initializer_list<std::string_view> known) {
    if (!object.is_object()) {
        fail(at(object, "an object is expected"));
    }
    for (const auto& [key, value] : object.members()) {
        bool found = key.rfind("_", 0) == 0 || key == "description";
        for (std::string_view k : known) {
            found = found || key == k;
        }
        if (!found) {
            fail(at(value, "unknown key \"" + key + "\""));
        }
    }
}

float number_of(const Value& parent, std::string_view key, double lo, double hi) {
    const Value& v = member(parent, key);
    if (!v.is_number() || v.number() < lo || v.number() > hi) {
        fail(at(v, "\"" + std::string(key) + "\" must be a number in " + json::format_number(lo) + ".." +
                       json::format_number(hi)));
    }
    return static_cast<float>(v.number());
}

int int_of(const Value& parent, std::string_view key, int lo, int hi) {
    const Value& v = member(parent, key);
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(at(v, "\"" + std::string(key) + "\" must be an integer in " + std::to_string(lo) + ".." +
                       std::to_string(hi)));
    }
    return static_cast<int>(v.integer());
}

}  // namespace

Result<AltCameraTuning> parse_alt_camera_tuning(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<AltCameraTuning>::fail(doc.error());
    }
    const Value& root = doc.value();
    reject_unknown(root, {"format", "version", "orbit", "look", "first_person", "melee_swing", "walls", "spring_arm"});
    const Value& format = member(root, "format");
    if (!format.is_string() || format.string() != "sh-alt-cameras") {
        fail(at(format, "\"format\" must be \"sh-alt-cameras\""));
    }
    int_of(root, "version", 1, 1);
    AltCameraTuning t;

    const Value& orbit = member(root, "orbit");
    reject_unknown(orbit, {"distance", "aim_distance", "height", "look_at_height", "head_height", "look_at_distance",
                           "zoom_percent_limit"});
    t.orbit_distance = number_of(orbit, "distance", 0.1, 64.0);
    t.aim_distance = number_of(orbit, "aim_distance", 0.0, 64.0);
    t.orbit_height = number_of(orbit, "height", -64.0, 64.0);
    t.look_at_height = number_of(orbit, "look_at_height", -64.0, 64.0);
    t.head_height = number_of(orbit, "head_height", -64.0, 64.0);
    t.look_at_distance = number_of(orbit, "look_at_distance", 0.1, 256.0);
    t.zoom_percent_limit = int_of(orbit, "zoom_percent_limit", 0, 1000);

    const Value& look = member(root, "look");
    reject_unknown(look, {"mouse_yaw", "mouse_pitch", "stick_dead_zone", "stick_yaw", "stick_pitch", "pitch_min_deg",
                          "pitch_max_deg", "stick_exponent", "smoothing_time"});
    t.mouse_yaw = int_of(look, "mouse_yaw", 0, 4096);
    t.mouse_pitch = int_of(look, "mouse_pitch", 0, 4096);
    t.stick_dead_zone = int_of(look, "stick_dead_zone", 0, 127);
    t.stick_yaw = int_of(look, "stick_yaw", 0, 4096);
    t.stick_pitch = int_of(look, "stick_pitch", 0, 4096);
    t.pitch_min_deg = number_of(look, "pitch_min_deg", -89.0, 0.0);
    t.pitch_max_deg = number_of(look, "pitch_max_deg", 0.0, 89.0);
    t.stick_exponent = number_of(look, "stick_exponent", 1.0, 4.0);
    t.look_smoothing_time = number_of(look, "smoothing_time", 0.0, 1.0);

    const Value& fp = member(root, "first_person");
    reject_unknown(fp, {"pitch_limit_deg", "yaw_limit_deg", "view_pitch_limit_deg", "head_follow_shift",
                        "head_look_delay_ms", "head_look_full_ms", "sway_limit"});
    t.fps_pitch_limit_deg = number_of(fp, "pitch_limit_deg", 0.0, 89.0);
    t.fps_yaw_limit_deg = number_of(fp, "yaw_limit_deg", 0.0, 180.0);
    t.fps_view_pitch_limit_deg = number_of(fp, "view_pitch_limit_deg", 0.0, 89.0);
    t.head_follow_shift = int_of(fp, "head_follow_shift", 0, 16);
    t.head_look_delay_ms = static_cast<std::uint32_t>(int_of(fp, "head_look_delay_ms", 0, 600000));
    t.head_look_full_ms = static_cast<std::uint32_t>(int_of(fp, "head_look_full_ms", 0, 600000));
    t.head_sway_limit = number_of(fp, "sway_limit", 0.0, 4.0);
    if (t.head_look_full_ms <= t.head_look_delay_ms) {
        fail(at(fp, "\"head_look_full_ms\" must be after \"head_look_delay_ms\""));
    }

    const Value& swing = member(root, "melee_swing");
    reject_unknown(swing, {"near", "wall", "head_show", "head_hide", "snap"});
    t.swing_near = number_of(swing, "near", 0.0, 64.0);
    t.swing_wall = number_of(swing, "wall", 0.0, 64.0);
    t.swing_head_show = number_of(swing, "head_show", 0.0, 64.0);
    t.swing_head_hide = number_of(swing, "head_hide", 0.0, 64.0);
    t.swing_snap = number_of(swing, "snap", 0.0, 64.0);

    const Value& walls = member(root, "walls");
    reject_unknown(walls, {"margin", "min"});
    t.wall_margin = number_of(walls, "margin", 0.0, 64.0);
    t.wall_min = number_of(walls, "min", 0.0, 64.0);

    const Value& arm = member(root, "spring_arm");
    reject_unknown(arm, {"follow_time", "follow_vertical_scale", "follow_aim_scale", "max_lag", "teleport_distance",
                         "zoom_time", "probe_radius", "ease_out_time", "close_lift", "close_shoulder",
                         "recenter_time",
                         "recenter_pitch_deg"});
    t.follow_time = number_of(arm, "follow_time", 0.0, 2.0);
    t.follow_vertical_scale = number_of(arm, "follow_vertical_scale", 0.0, 8.0);
    t.follow_aim_scale = number_of(arm, "follow_aim_scale", 0.0, 1.0);
    t.max_lag = number_of(arm, "max_lag", 0.0, 16.0);
    t.teleport_distance = number_of(arm, "teleport_distance", 0.1, 256.0);
    t.zoom_time = number_of(arm, "zoom_time", 0.0, 2.0);
    t.probe_radius = number_of(arm, "probe_radius", 0.0, 2.0);
    t.ease_out_time = number_of(arm, "ease_out_time", 0.0, 4.0);
    t.close_lift = number_of(arm, "close_lift", 0.0, 4.0);
    t.close_shoulder = number_of(arm, "close_shoulder", 0.0, 1.0);
    t.recenter_time = number_of(arm, "recenter_time", 0.01, 8.0);
    t.recenter_pitch_deg = number_of(arm, "recenter_pitch_deg", -89.0, 89.0);
    return Result<AltCameraTuning>::ok(t);
} catch (const std::exception& e) {
    return Result<AltCameraTuning>::fail(e.what());
}

Result<AltCameraTuning> load_alt_camera_tuning(const std::filesystem::path& gamedata_dir,
                                               const std::filesystem::path& mods_dir, std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("config") / "alt_cameras.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<AltCameraTuning>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<AltCameraTuning> parsed = parse_alt_camera_tuning(text);
    if (!parsed) {
        return Result<AltCameraTuning>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

// --- the camera ------------------------------------------------------------------------------

AltCamera::AltCamera(const AltCameraTuning& tuning) : t_(tuning) {}

void AltCamera::Spring::step(double to, double smooth_time, double dt) {
    if (smooth_time <= 0.0) {
        snap(to);
        return;
    }
    if (dt <= 0.0) {
        target = to;
        return;
    }
    // The exact solution of a critically damped spring whose target runs from the last frame's to
    // this one's at a steady speed: the same path at any frame rate. Relative to the moving target the
    // spring settles at -2u/w; the rest is the free decay.
    const double w = 2.0 / smooth_time;
    const double u = (to - target) / dt;
    const double settle = -2.0 * u / w;
    const double z = (x - target) - settle;
    const double zv = v - u;
    const double e = std::exp(-w * dt);
    const double k = (zv + w * z) * dt;
    x = to + settle + (z + k) * e;
    v = u + (zv - w * k) * e;
    target = to;
}

std::int32_t AltCamera::q(float units) const { return to_q12(units); }
std::int32_t AltCamera::deg(float degrees) const { return to_angle(degrees); }

std::int32_t AltCamera::projection_distance(float fov_deg, std::int32_t fallback) {
    // Rounded to nearest so the default 71.1 degrees lands exactly on the game's own 224.
    const float t = std::tan(fov_deg * (3.14159265f / 360.0f));
    std::int32_t h = (t > 0.001f) ? static_cast<std::int32_t>((160.0f / t) + 0.5f) : fallback;
    if (h < 16) h = 16;
    if (h > 512) h = 512;
    return h;
}

void AltCamera::look(const AltCameraInput& in, AltCameraOutput& out, std::int32_t& yaw, std::int32_t& pitch) {
    const AltCameraOptions& o = in.options;
    int mdx = in.frozen ? 0 : in.mouse_dx;
    int mdy = in.frozen ? 0 : in.mouse_dy;
    yaw += static_cast<std::int32_t>(mdx * t_.mouse_yaw * o.mouse_sensitivity);
    const std::int32_t mouse_pitch = static_cast<std::int32_t>(mdy * t_.mouse_pitch * o.mouse_sensitivity);
    pitch += o.invert_mouse_y ? mouse_pitch : -mouse_pitch;

    int rx = in.frozen ? 0 : in.right_x - 128;
    int ry = in.frozen ? 0 : in.right_y - 128;
    if (rx > -t_.stick_dead_zone && rx < t_.stick_dead_zone) rx = 0;
    if (ry > -t_.stick_dead_zone && ry < t_.stick_dead_zone) ry = 0;
    if (rx != 0 || ry != 0) {
        const float s = o.pad_sensitivity;
        const std::int32_t stick_yaw = per_frame(static_cast<std::int32_t>(((rx * t_.stick_yaw) >> 7) * s), in.delta_time);
        const std::int32_t stick_pitch =
            per_frame(static_cast<std::int32_t>(((ry * t_.stick_pitch) >> 7) * s), in.delta_time);
        yaw += stick_yaw;
        pitch += o.invert_pad_y ? stick_pitch : -stick_pitch;
    }

    // The device that last turned the view (aim assist strength): the mouse, else any stick.
    // The move stick counts past the dead zone, the look stick from its edge.
    const int lx = in.left_x - 128;
    const int ly = in.left_y - 128;
    const int zone = t_.stick_dead_zone;
    if (mdx != 0 || mdy != 0) {
        out.aim_device = 0;
    } else if (rx != 0 || ry != 0 || lx > zone || lx < -zone || ly > zone || ly < -zone) {
        out.aim_device = 1;
    }

    yaw = (yaw + 4096) & 4095;
    if (in.style == AltStyle::FirstPerson) {
        const std::int32_t limit = deg(t_.fps_pitch_limit_deg);
        if (pitch < -limit) pitch = -limit;
        if (pitch > limit) pitch = limit;
        // The look turns up to the shoulders; moving or aiming turns the body with it.
        const std::int32_t side = deg(t_.fps_yaw_limit_deg);
        const std::int32_t off = signed_angle(yaw - in.harry_yaw);
        if (off > side) {
            yaw = (in.harry_yaw + side + 4096) & 4095;
        } else if (off < -side) {
            yaw = (in.harry_yaw - side + 4096) & 4095;
        }
    } else {
        if (pitch < deg(t_.pitch_min_deg)) pitch = deg(t_.pitch_min_deg);
        if (pitch > deg(t_.pitch_max_deg)) pitch = deg(t_.pitch_max_deg);
    }
}


namespace {

// The free-aim ray runs from the eye through the screen centre: along eye -> look target, which is
// not the orbit direction (the target is anchored at the chest).
void aim_ray(const AltVec3& eye, const AltVec3& at, const AltVec3& fwd, AltCameraOutput& out) {
    out.aim_origin = eye;
    const std::int32_t ax = (at.x - eye.x) >> 6;
    const std::int32_t ay = (at.y - eye.y) >> 6;
    const std::int32_t az = (at.z - eye.z) >> 6;
    const std::int32_t mag = sqrt3(ax, ay, az);
    if (mag > 0) {
        out.aim_forward = AltVec3{static_cast<std::int32_t>((static_cast<std::int64_t>(ax) << 12) / mag),
                                  static_cast<std::int32_t>((static_cast<std::int64_t>(ay) << 12) / mag),
                                  static_cast<std::int32_t>((static_cast<std::int64_t>(az) << 12) / mag)};
    } else {
        out.aim_forward = fwd;
    }
}

std::int32_t round_i(double v) { return static_cast<std::int32_t>(std::lround(v)); }

double clamp01(float v) { return v < 0.0f ? 0.0 : (v > 1.0f ? 1.0 : static_cast<double>(v)); }

// The share of a gap an exponential ease with smooth time `time` closes in dt (1 = all of it).
double ease_share(double time, double dt) {
    if (time <= 0.0) return 1.0;
    if (dt <= 0.0) return 0.0;
    return 1.0 - std::exp(-2.0 * dt / time);
}

struct DVec {
    double x = 0.0, y = 0.0, z = 0.0;
};

double length(const DVec& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

AltVec3 to_alt(const DVec& v) { return AltVec3{round_i(v.x), round_i(v.y), round_i(v.z)}; }

DVec to_d(const AltVec3& v) { return DVec{double(v.x), double(v.y), double(v.z)}; }

}  // namespace

void AltCamera::look_orbit(const AltCameraInput& in, AltCameraOutput& out, std::int32_t& yaw, std::int32_t& pitch,
                           double dt) {
    const AltCameraOptions& o = in.options;
    const int mdx = in.frozen ? 0 : in.mouse_dx;
    const int mdy = in.frozen ? 0 : in.mouse_dy;
    double d_yaw = double(mdx) * t_.mouse_yaw * o.mouse_sensitivity;
    const double mouse_pitch = double(mdy) * t_.mouse_pitch * o.mouse_sensitivity;
    double d_pitch = o.invert_mouse_y ? mouse_pitch : -mouse_pitch;

    int rx = in.frozen ? 0 : in.right_x - 128;
    int ry = in.frozen ? 0 : in.right_y - 128;
    const int zone = t_.stick_dead_zone;
    if (rx > -zone && rx < zone) rx = 0;
    if (ry > -zone && ry < zone) ry = 0;
    if (rx != 0 || ry != 0) {
        // Past the dead zone the deflection restarts at 0 and goes through the response curve, so a
        // small push aims finely and a full push turns at the tuned rate.
        const auto curve = [&](int v) {
            const double span = 127.0 - zone;
            double m = span > 0.0 ? (std::abs(v) - zone) / span : 1.0;
            m = m < 0.0 ? 0.0 : (m > 1.0 ? 1.0 : m);
            m = std::pow(m, double(t_.stick_exponent));
            return v < 0 ? -m : m;
        };
        const double frames = dt * 30.0;  // the stick rates are per 30 fps frame
        const double s = o.pad_sensitivity;
        d_yaw += curve(rx) * t_.stick_yaw * s * frames;
        const double stick_pitch = curve(ry) * t_.stick_pitch * s * frames;
        d_pitch += o.invert_pad_y ? stick_pitch : -stick_pitch;
    }

    // The device that last turned the view (aim assist strength): the mouse, else any stick.
    const int lx = in.left_x - 128;
    const int ly = in.left_y - 128;
    if (mdx != 0 || mdy != 0) {
        out.aim_device = 0;
    } else if (rx != 0 || ry != 0 || lx > zone || lx < -zone || ly > zone || ly < -zone) {
        out.aim_device = 1;
    }

    const bool turned = mdx != 0 || mdy != 0 || rx != 0 || ry != 0;
    look_idle_ = turned ? 0.0 : look_idle_ + dt;

    if (in.frozen) {
        pending_yaw_ = pending_pitch_ = carry_yaw_ = carry_pitch_ = 0.0;
    } else {
        // Smoothing spreads a turn over the next frames without losing any of it.
        pending_yaw_ += d_yaw;
        pending_pitch_ += d_pitch;
        const double share = dt > 0.0 ? ease_share(t_.look_smoothing_time * clamp01(o.look_smoothing), dt) : 1.0;
        const double apply_yaw = share >= 1.0 ? pending_yaw_ : pending_yaw_ * share;
        const double apply_pitch = share >= 1.0 ? pending_pitch_ : pending_pitch_ * share;
        pending_yaw_ -= apply_yaw;
        pending_pitch_ -= apply_pitch;
        // The tail of an ease is applied whole, so a turn ends instead of creeping.
        if (std::abs(pending_yaw_) < 0.25) {
            carry_yaw_ += pending_yaw_;
            pending_yaw_ = 0.0;
        }
        if (std::abs(pending_pitch_) < 0.25) {
            carry_pitch_ += pending_pitch_;
            pending_pitch_ = 0.0;
        }
        carry_yaw_ += apply_yaw;
        carry_pitch_ += apply_pitch;
        const auto whole_yaw = static_cast<std::int32_t>(std::lround(carry_yaw_));
        const auto whole_pitch = static_cast<std::int32_t>(std::lround(carry_pitch_));
        carry_yaw_ -= whole_yaw;
        carry_pitch_ -= whole_pitch;
        yaw += whole_yaw;
        pitch += whole_pitch;
    }
    yaw = (yaw + 4096) & 4095;
    const std::int32_t lo = deg(t_.pitch_min_deg);
    const std::int32_t hi = deg(t_.pitch_max_deg);
    if (pitch <= lo || pitch >= hi) {
        // At a limit the rest of a smoothed turn into it is dropped, or it would hold the view there.
        if ((pitch <= lo && pending_pitch_ < 0.0) || (pitch >= hi && pending_pitch_ > 0.0)) {
            pending_pitch_ = carry_pitch_ = 0.0;
        }
        pitch = pitch < lo ? lo : (pitch > hi ? hi : pitch);
    }

    // Recenter: Harry steers by the camera yaw, so the yaw is behind him already; the pitch levels out.
    if (o.recenter && in.moving && !in.aiming && !in.frozen && look_idle_ >= o.recenter_delay && dt > 0.0) {
        const std::int32_t target = deg(t_.recenter_pitch_deg);
        recenter_carry_ += (target - pitch) * ease_share(t_.recenter_time, dt);
        const auto step = static_cast<std::int32_t>(recenter_carry_);
        recenter_carry_ -= step;
        pitch += step;
    } else {
        recenter_carry_ = 0.0;
    }
}

void AltCamera::orbit(const AltCameraInput& in, AltCameraOutput& out, std::int32_t yaw, std::int32_t pitch, double dt,
                      AltCameraHost& host) {
    const AltCameraOptions& o = in.options;
    const bool snap = !arm_valid_ || in.reset;

    // Orbit radius: rest and aim on one slider scale (0% the plain orbit, 100% the aim distance, 200%
    // the full dolly).
    const std::int32_t orbit_len = q(t_.orbit_distance);
    const std::int32_t aim_len = q(t_.aim_distance);
    const std::int32_t closest = 2 * aim_len - orbit_len;  // the dolly at full zoom, also the nearest the orbit may come
    const auto round_percent = [&](float p) {
        std::int32_t v = static_cast<std::int32_t>(p + (p < 0.0f ? -0.5f : 0.5f));
        if (v < -t_.zoom_percent_limit) v = -t_.zoom_percent_limit;
        if (v > t_.zoom_percent_limit) v = t_.zoom_percent_limit;
        return v;
    };
    const std::int32_t travel = orbit_len - closest;
    std::int32_t rest = orbit_len - ((travel * round_percent(o.rest_zoom_percent)) / 200);
    std::int32_t aimed = rest - ((travel * round_percent(o.aim_zoom_percent)) / 200);
    if (rest < closest) rest = closest;
    if (aimed < closest) aimed = closest;
    const double target_distance = in.aiming ? aimed : rest;
    // Shoulder offset (over the shoulder, and third person while aiming): along the right vector,
    // flipped by the shoulder side, and up or down.
    const double target_sx = double((in.aiming ? o.aim_x : o.rest_x) * in.shoulder_side);
    const double target_sy = double(in.aiming ? o.aim_y : o.rest_y);

    // The pivot follows Harry's root through the spring; a jump (a room or map change) snaps it.
    const DVec harry = to_d(in.harry);
    const DVec lag{follow_[0].x - harry.x, follow_[1].x - harry.y, follow_[2].x - harry.z};
    if (snap || length(lag) > q(t_.teleport_distance)) {
        follow_[0].snap(harry.x);
        follow_[1].snap(harry.y);
        follow_[2].snap(harry.z);
    } else {
        const double time = t_.follow_time * clamp01(o.follow_smoothing) * (in.aiming ? t_.follow_aim_scale : 1.0);
        follow_[0].step(harry.x, time, dt);
        follow_[2].step(harry.z, time, dt);
        follow_[1].step(harry.y, time * t_.follow_vertical_scale, dt);
        const double max_lag = q(t_.max_lag);
        const double hx = follow_[0].x - harry.x;
        const double hz = follow_[2].x - harry.z;
        const double h = std::sqrt(hx * hx + hz * hz);
        if (h > max_lag) {
            const double k = max_lag / h;
            follow_[0].x = harry.x + hx * k;
            follow_[2].x = harry.z + hz * k;
        }
        const double vy = follow_[1].x - harry.y;
        if (vy > max_lag) follow_[1].x = harry.y + max_lag;
        if (vy < -max_lag) follow_[1].x = harry.y - max_lag;
    }
    if (snap) {
        distance_.snap(target_distance);
        shoulder_x_.snap(target_sx);
        shoulder_y_.snap(target_sy);
    } else {
        distance_.step(target_distance, t_.zoom_time, dt);
        shoulder_x_.step(target_sx, t_.zoom_time, dt);
        shoulder_y_.step(target_sy, t_.zoom_time, dt);
    }
    const AltVec3 root{round_i(follow_[0].x), round_i(follow_[1].x), round_i(follow_[2].x)};

    const std::int32_t rear = in.rear_look ? 2048 : 0;
    const std::int32_t view_yaw = yaw + rear;
    const std::int32_t sy = fx::sin(view_yaw);
    const std::int32_t cy = fx::cos(view_yaw);
    const std::int32_t sp = fx::sin(pitch);
    const std::int32_t cp = fx::cos(pitch);
    const AltVec3 fwd{wide(sy, cp), -sp, wide(cy, cp)};
    const std::int32_t ahead = q(t_.look_at_distance);
    const std::int32_t distance = round_i(distance_.x);

    // Behind Harry along the view, lifted; the look target far ahead (steady), anchored at his chest.
    AltVec3 eye{root.x - wide(distance, fwd.x), root.y - wide(distance, fwd.y) + q(t_.orbit_height),
                root.z - wide(distance, fwd.z)};
    const std::int32_t anchor_y = root.y + q(t_.look_at_height);
    AltVec3 at{eye.x + wide(ahead, fwd.x), anchor_y + wide(ahead, fwd.y), eye.z + wide(ahead, fwd.z)};

    // The arm starts at Harry's real head (always in the open, unlike the lagged pivot).
    const std::int32_t head_y = in.harry.y + q(t_.head_height);
    const DVec head{harry.x, double(head_y), harry.z};
    const DVec right{cy / 4096.0, 0.0, -sy / 4096.0};
    const double probe = q(t_.probe_radius);

    // The shoulder offset first: a wall beside Harry narrows it instead of collapsing the whole arm.
    double sx = shoulder_x_.x;
    double syo = shoulder_y_.x;
    if (o.wall_collision && (std::abs(sx) >= 1.0 || std::abs(syo) >= 1.0)) {
        const DVec side{right.x * sx, syo, right.z * sx};
        const double len = length(side);
        const double k = (len + probe) / len;
        const DVec to{head.x + side.x * k, head.y + side.y * k, head.z + side.z * k};
        std::int32_t hit = 0;
        if (host.ray_hit(to_alt(head), to_alt(to), &hit) && hit < len + probe) {
            double share = (hit - probe) / len;
            share = share < 0.0 ? 0.0 : share;
            sx *= share;
            syo *= share;
        }
    }
    const DVec offset{right.x * sx, syo, right.z * sx};
    eye.x += round_i(offset.x);
    eye.y += round_i(offset.y);
    eye.z += round_i(offset.z);
    at.x += round_i(offset.x);
    at.y += round_i(offset.y);
    at.z += round_i(offset.z);

    // Walls: a cone of rays from the shoulder point to the eye and to points around it (the near
    // plane's footprint); the eye stays short of the nearest hit. In at once, back out eased, so a
    // corner the rays graze cannot make it pump.
    const DVec pivot{head.x + offset.x, head.y + offset.y, head.z + offset.z};
    const DVec arm{eye.x - pivot.x, eye.y - pivot.y, eye.z - pivot.z};
    const double full = length(arm);
    double hard = 1.0;
    if (full > 0.0) {
        const DVec around[] = {{0.0, 0.0, 0.0},
                               {right.x * probe, 0.0, right.z * probe},
                               {-right.x * probe, 0.0, -right.z * probe},
                               {0.0, -probe, 0.0},
                               {0.0, probe, 0.0}};
        const int rays = probe > 0.0 ? 5 : 1;
        const double margin = q(t_.wall_margin);
        for (int i = 0; i < rays; ++i) {
            const DVec to{eye.x + around[i].x, eye.y + around[i].y, eye.z + around[i].z};
            const double len = length(DVec{to.x - pivot.x, to.y - pivot.y, to.z - pivot.z});
            std::int32_t hit = 0;
            if (len > 0.0 && host.ray_hit(to_alt(pivot), to_alt(to), &hit) && hit < len) {
                const double share = (hit - margin) / len;
                if (share < hard) hard = share;
            }
        }
        const double least = q(t_.wall_min) / full;
        if (hard < least) hard = least;
        if (hard > 1.0) hard = 1.0;
    }
    if (snap || hard < arm_frac_) {
        arm_frac_ = hard;
    } else {
        arm_frac_ += (hard - arm_frac_) * ease_share(t_.ease_out_time, dt);
    }
    arm_valid_ = true;

    const auto along = [&](double share) {
        return AltVec3{round_i(pivot.x + arm.x * share), round_i(pivot.y + arm.y * share),
                       round_i(pivot.z + arm.z * share)};
    };
    // The aim ray always starts in front of the wall, whatever the render eye does.
    const AltVec3 aim_eye = hard < 1.0 ? along(hard) : eye;
    if (o.wall_collision && arm_frac_ < 1.0) {
        eye = along(arm_frac_);
        // The shoulder offset closes in with the arm, down to close_shoulder of itself, so Harry stays
        // in the frame when a wall presses the eye up to him.
        const double drop = (1.0 - t_.close_shoulder) * (1.0 - arm_frac_);
        eye.x -= round_i(offset.x * drop);
        eye.z -= round_i(offset.z * drop);
        at.x -= round_i(offset.x * drop);
        at.z -= round_i(offset.z * drop);
        // The look target rises toward the head as the eye comes in.
        std::int32_t pulled = 4096 - round_i(arm_frac_ * 4096.0);
        pulled = pulled < 0 ? 0 : (pulled > 4096 ? 4096 : pulled);
        at.y += wide(head_y - anchor_y, pulled);
        // Pressed close, the eye also rises so the view clears Harry's head instead of filling with it
        // (as far as the ceiling allows).
        std::int32_t lift = round_i(q(t_.close_lift) * (1.0 - arm_frac_));
        if (lift > 0) {
            const AltVec3 up{eye.x, eye.y - lift - round_i(probe), eye.z};
            std::int32_t hit = 0;
            if (host.ray_hit(eye, up, &hit)) {
                const std::int32_t room = hit - round_i(probe);
                lift = room < 0 ? 0 : (room < lift ? room : lift);
            }
            eye.y -= lift;
        }
    }

    aim_ray(eye, at, fwd, out);
    // Eye allowed through walls: the shot must not start behind one, so slide its origin along the
    // view line by as far as the pull-in would have moved the eye.
    if (!o.wall_collision) {
        const std::int32_t dx = aim_eye.x - eye.x;
        const std::int32_t dy = aim_eye.y - eye.y;
        const std::int32_t dz = aim_eye.z - eye.z;
        const std::int64_t ahead_of = (static_cast<std::int64_t>(dx) * out.aim_forward.x +
                                       static_cast<std::int64_t>(dy) * out.aim_forward.y +
                                       static_cast<std::int64_t>(dz) * out.aim_forward.z) >>
                                      12;
        if (ahead_of > 0) {
            out.aim_origin.x = eye.x + static_cast<std::int32_t>((ahead_of * out.aim_forward.x) >> 12);
            out.aim_origin.y = eye.y + static_cast<std::int32_t>((ahead_of * out.aim_forward.y) >> 12);
            out.aim_origin.z = eye.z + static_cast<std::int32_t>((ahead_of * out.aim_forward.z) >> 12);
        }
    }
    out.eye = eye;
    out.look_at = at;
}

AltCameraOutput AltCamera::update(const AltCameraInput& in, AltCameraHost& host) {
    AltCameraOutput out;
    std::int32_t yaw = in.yaw;
    std::int32_t pitch = in.pitch;
    double dt = in.delta_time / 4096.0;
    dt = dt < 0.0 ? 0.0 : (dt > 0.25 ? 0.25 : dt);

    if (in.style != AltStyle::FirstPerson) {
        if (in.reset) {
            yaw = in.harry_yaw;
            pitch = 0;
            pending_yaw_ = pending_pitch_ = carry_yaw_ = carry_pitch_ = 0.0;
        }
        head_ref_valid_ = false;
        head_rot_valid_ = false;
        swing_pull_ = 0;
        head_visible_ = false;
        look_orbit(in, out, yaw, pitch, dt);
        orbit(in, out, yaw, pitch, dt, host);
        out.yaw = yaw;
        out.pitch = pitch;
        return out;
    }

    // First person. The orbit starts fresh when its style comes back.
    arm_valid_ = false;
    const AltCameraOptions& o = in.options;
    if (in.reset) {
        // The eye sits on the tuned spot at once, looking where Harry faces: a reference kept from
        // before a scene would drift back for seconds.
        yaw = in.harry_yaw;
        pitch = 0;
        head_ref_valid_ = false;
        head_rot_valid_ = false;
        swing_pull_ = 0;
        head_visible_ = false;
    }

    look(in, out, yaw, pitch);

    std::int32_t view_yaw = yaw;
    std::int32_t view_pitch = pitch;

    // Head look: the head bone's turn relative to the body, less a slow reference that nulls its rest
    // orientation, eased in a while after Harry stops.
    if (o.head_tracking) {
        const std::int32_t hmag = world::game_sqrt(static_cast<std::int32_t>(
            static_cast<std::uint32_t>(sq(in.head.fx)) + static_cast<std::uint32_t>(sq(in.head.fz))));
        const std::int16_t head_yaw_world = static_cast<std::int16_t>(fx::atan2(in.head.fx, in.head.fz));
        const std::int16_t head_pitch = static_cast<std::int16_t>(fx::atan2(-in.head.fy, hmag));
        const std::int16_t head_yaw = signed_angle(head_yaw_world - in.harry_yaw);
        if (!head_rot_valid_) {
            if (in.in_gameplay) {
                head_yaw_ref_ = head_yaw;
                head_pitch_ref_ = head_pitch;
                head_rot_valid_ = true;
            }
        } else {
            const auto now = static_cast<std::uint32_t>(in.now_ms);
            if (in.moving) last_move_ms_ = now;
            const std::uint32_t still = now - last_move_ms_;
            std::int32_t gain;
            if (still <= t_.head_look_delay_ms) {
                gain = 0;
            } else if (still >= t_.head_look_full_ms) {
                gain = 4096;
            } else {
                gain = static_cast<std::int32_t>((static_cast<std::int64_t>(still - t_.head_look_delay_ms) << 12) /
                                                 (t_.head_look_full_ms - t_.head_look_delay_ms));
            }
            head_yaw_ref_ = static_cast<std::int16_t>(head_yaw_ref_ + (signed_angle(head_yaw - head_yaw_ref_) >> 8));
            head_pitch_ref_ =
                static_cast<std::int16_t>(head_pitch_ref_ + (signed_angle(head_pitch - head_pitch_ref_) >> 8));
            const std::int32_t d_yaw = signed_angle(head_yaw - head_yaw_ref_);
            const std::int32_t d_pitch = signed_angle(head_pitch - head_pitch_ref_);
            view_yaw += wide(d_yaw, gain);
            view_pitch += wide(d_pitch, gain);
            const std::int32_t limit = deg(t_.fps_view_pitch_limit_deg);
            if (view_pitch > limit) view_pitch = limit;
            if (view_pitch < -limit) view_pitch = -limit;
        }
    } else {
        head_rot_valid_ = false;
    }

    const std::int32_t sy = fx::sin(view_yaw);
    const std::int32_t cy = fx::cos(view_yaw);
    const std::int32_t sp = fx::sin(view_pitch);
    const std::int32_t cp = fx::cos(view_pitch);
    const AltVec3 fwd{wide(sy, cp), -sp, wide(cy, cp)};
    const std::int32_t ahead = q(t_.look_at_distance);

    // The eye rides the body yaw (the frame the offset is tuned in) plus the head's sway: the head
    // bone in the body frame less a slowly tracking reference, so the eye rests on the tuned spot.
    AltVec3 eye;
    {
        const std::int32_t s = fx::sin(in.harry_yaw);
        const std::int32_t c = fx::cos(in.harry_yaw);
        AltVec3 local = o.eye_offset;
        const std::int32_t hdx = (in.head.tx << 4) - in.harry.x;
        const std::int32_t hdy = (in.head.ty << 4) - in.harry.y;
        const std::int32_t hdz = (in.head.tz << 4) - in.harry.z;
        AltVec3 head;
        head.x = static_cast<std::int32_t>((static_cast<std::int64_t>(hdx) * c - static_cast<std::int64_t>(hdz) * s) >> 12);
        head.z = static_cast<std::int32_t>((static_cast<std::int64_t>(hdx) * s + static_cast<std::int64_t>(hdz) * c) >> 12);
        head.y = hdy;
        if (!head_ref_valid_) {
            if (in.in_gameplay) {
                head_ref_ = head;
                head_ref_valid_ = true;
            }
        } else {
            head_ref_.x += (head.x - head_ref_.x) >> t_.head_follow_shift;
            head_ref_.y += (head.y - head_ref_.y) >> t_.head_follow_shift;
            head_ref_.z += (head.z - head_ref_.z) >> t_.head_follow_shift;
            // A pose change (getting up, a scripted clip) moves the head farther than any sway: the
            // reference is dragged along, so the eye never leaves the tuned spot by more than the limit.
            const std::int32_t limit = q(t_.head_sway_limit);
            const auto drag = [limit](std::int32_t now, std::int32_t& ref) {
                if (now - ref > limit) ref = now - limit;
                if (now - ref < -limit) ref = now + limit;
            };
            drag(head.x, head_ref_.x);
            drag(head.y, head_ref_.y);
            drag(head.z, head_ref_.z);
        }
        if (head_ref_valid_) {
            local.x += head.x - head_ref_.x;
            local.y += head.y - head_ref_.y;
            local.z += head.z - head_ref_.z;
        }
        out.head_reference = head_ref_;
        out.head_local = head;

        eye.x = in.harry.x + wide(local.z, s) + wide(local.x, c);
        eye.z = in.harry.z + wide(local.z, c) - wide(local.x, s);
        eye.y = in.harry.y + local.y;
    }

    // Melee swing: back the eye along the view away from the nearest forearm or hand, up to the
    // player's cap, quick out and gentler home; a ray keeps it out of the level behind the eye.
    {
        const std::int32_t near = q(t_.swing_near);
        const std::int32_t wall = q(t_.swing_wall);
        const auto cap = static_cast<std::int32_t>(o.melee_swing * 4096.0f);
        std::int32_t target = 0;
        if (in.melee_attack) {
            std::int32_t nearest = 0x7FFFFFFF;
            for (const AltBone& b : in.arms) {
                const std::int32_t d = sqrt3((b.tx << 4) - eye.x, (b.ty << 4) - eye.y, (b.tz << 4) - eye.z);
                if (d < nearest) nearest = d;
            }
            if (nearest < near) {
                // 1.5x: the bone origins sit behind the mesh surface that fills the view.
                target = (near - nearest) + ((near - nearest) >> 1);
                if (target > cap) target = cap;
            }
        }
        std::int32_t d = target - swing_pull_;
        swing_pull_ += per_frame(d > 0 ? (d >> 1) : (d >> 2), in.delta_time);
        d = target - swing_pull_;
        if (d < 0) d = -d;
        if (d < q(t_.swing_snap)) swing_pull_ = target;

        if (swing_pull_ > q(t_.swing_head_show)) {
            head_visible_ = true;
        } else if (swing_pull_ < q(t_.swing_head_hide)) {
            head_visible_ = false;
        }

        if (swing_pull_ > 0) {
            std::int32_t pull = swing_pull_;
            const AltVec3 back{eye.x - wide(pull + wall, fwd.x), eye.y - wide(pull + wall, fwd.y),
                               eye.z - wide(pull + wall, fwd.z)};
            std::int32_t hit = 0;
            if (host.ray_hit(eye, back, &hit)) {
                std::int32_t safe = hit - wall;
                if (safe < 0) safe = 0;
                if (safe < pull) pull = safe;
            }
            eye.x -= wide(pull, fwd.x);
            eye.y -= wide(pull, fwd.y);
            eye.z -= wide(pull, fwd.z);
        }
    }
    out.first_person = true;
    out.view_forward = fwd;
    out.head_visible = head_visible_;
    const AltVec3 at{eye.x + wide(ahead, fwd.x), eye.y + wide(ahead, fwd.y), eye.z + wide(ahead, fwd.z)};

    aim_ray(eye, at, fwd, out);
    out.eye = eye;
    out.look_at = at;
    out.yaw = yaw;
    out.pitch = pitch;
    return out;
}

}  // namespace sh::camera
