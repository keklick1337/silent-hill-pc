// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The modding tools' placement editor (docs/modding/tools.md): a list of everything placed on the loaded map, a
// click on the world or on the list selects one thing, handles in the world and number fields move, turn and resize
// it in the running game, and the panel shows where the thing is written and the line as it should stand there after
// the edit, to copy into the file. With it a free cursor point (for places without ground under them) and a path of
// points with its lengths and travel time. Nothing is written to a file and nothing runs while the editor is closed:
// the panel and the overlay are drawn by the tools host only while it is open, and every change of the game goes
// through defer().

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "engine/console/instance.h"
#include "engine/console/registry.h"
#include "engine/devtools/placement_edit.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "engine/maps/map_runtime.h"
#include "engine/world/world_objects.h"
#include "features/tools/edit_internal.h"
#include "features/tools/tools.h"
#include "features/tools/tools_internal.h"

namespace sh::tools::edit {

namespace {

namespace gs = sh::game_state;

constexpr double kPi = std::numbers::pi;
constexpr std::int32_t kNoGround = 8 * 4096;  // the ground query's answer where nothing is loaded
constexpr int kKinds = static_cast<int>(Kind::Count);
constexpr const char* kOverlay = "edit.handles";
constexpr float kGripPixels = 7.0f;

constexpr Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color kAxisX{1.0f, 0.35f, 0.35f, 1.0f};
constexpr Color kAxisY{0.4f, 1.0f, 0.4f, 1.0f};
constexpr Color kAxisZ{0.45f, 0.6f, 1.0f, 1.0f};
constexpr Color kPlane{1.0f, 0.9f, 0.2f, 1.0f};
constexpr Color kSizeColor{1.0f, 0.6f, 0.15f, 1.0f};
constexpr Color kCursorColor{0.2f, 1.0f, 1.0f, 1.0f};
constexpr Color kPathColor{1.0f, 0.5f, 1.0f, 1.0f};

enum class Target : std::uint8_t { None, Thing, Cursor, Path };
enum class Tool : std::uint8_t { Select, Move, Rotate };
enum class Grip : std::uint8_t { None, Plane, AxisX, AxisZ, Height, Yaw, Pitch, Roll, Radius, SizeX, SizeZ };

constexpr const char* kGripNames[] = {"none",  "move", "x",      "z",     "height", "yaw",
                                      "pitch", "roll", "radius", "sizex", "sizez"};

struct GripAt {
    Grip grip = Grip::None;
    Vec3 at;
    Color color;
};

struct State {
    // The things of this frame.
    std::vector<Thing> things;
    int frame = -1;
    std::string map;

    bool show[kKinds] = {true, true, true, true, true, false, true};
    bool near_only = true;
    float near = 60.0f;
    char search[96] = "";
    bool click_selects = true;

    Target target = Target::None;
    std::string key;      // the selected thing
    int path_index = -1;  // the selected path point
    dt::EditValue edit;   // the selected thing's numbers as the fields show them
    Tool tool = Tool::Move;
    double step = 0.01;
    dt::EditHistory history;

    Origin origin;
    bool origin_asked = false;
    std::vector<Correction> fixes;
    dt::EditValue fixes_for;
    bool fixes_valid = false;

    Grip drag = Grip::None;
    int gesture = 0;
    Vec3 grab_offset;         // the grabbed point minus the centre
    Vec3 height_normal;       // the vertical plane a height drag moves in
    double grab_angle = 0.0;  // degrees, where a ring was grabbed
    dt::EditValue drag_from;
    int applying = 0;      // values on their way to the game: the fields are not read back meanwhile
    int click_frame = -1;  // a click the handles took: no selection from it

    bool cursor_on = false;
    bool cursor_snap = true;
    bool cursor_arm = false;  // the next click on the world puts the cursor
    double cursor[3] = {0, 0, 0};

    std::vector<dt::PathPoint> path;
    double speed = 0.8;    // m/s
    double seconds = 0.0;  // the time typed last; 0: derived from the speed
};
State s;

sh::maps::MapRuntime& rt() { return sh::maps::map_runtime(); }
double ground_y() { return metres(gs::chara_position(0).y); }

std::string lower(std::string v) {
    for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v;
}

void say(const std::string& line) { SH_LOG("[EDIT] %s", line.c_str()); }

// The things of the loaded map, read once a frame for the list, the picking and the handles.
const std::vector<Thing>& things(bool fresh = false) {
    const int now = ImGui::GetCurrentContext() != nullptr ? ImGui::GetFrameCount() : -2;
    if (fresh || now != s.frame || now < 0 || s.map != rt().name()) {
        if (s.map != rt().name()) {
            // Another map: nothing of the old one is left to step back to.
            s.history.clear();
            if (s.target == Target::Thing) s.target = Target::None;
            s.key.clear();
            s.origin = Origin{};
        }
        s.things = collect(ground_y());
        s.frame = now;
        s.map = rt().name();
    }
    return s.things;
}

const Thing* find_thing(const std::string& key) {
    for (const Thing& t : things())
        if (t.key == key) return &t;
    return nullptr;
}

const Thing* selected() { return s.target == Target::Thing ? find_thing(s.key) : nullptr; }

Vec3 centre_of(const Thing& t, const dt::EditValue& v) {
    return {v.position[0], t.on_ground ? ground_y() : v.position[1], v.position[2]};
}

// The engine holds what the fields show: they are then not read back from it, so a number typed stays as typed.
bool engine_has(const Thing& t, const dt::EditValue& v) {
    if (q12(v.position[0]) != t.position[0] || q12(v.position[2]) != t.position[2]) return false;
    if ((t.caps & kHeight) != 0 && q12(v.position[1]) != t.position[1]) return false;
    if (t.kind == Kind::Spawn) {
        if (((std::lround(v.rotation[1] * 256.0 / 360.0) * 16 - t.rotation[1]) & 0xFFF) != 0) return false;
    } else if ((t.caps & (kYaw | kTilt)) != 0) {
        for (std::size_t i = 0; i < 3; ++i) {
            if (i != 1 && (t.caps & kTilt) == 0) continue;
            if (((sh::world::world_degrees_to_angle(v.rotation[i]) - t.rotation[i]) & 0xFFF) != 0) return false;
        }
    }
    if ((t.caps & kRadius) != 0) {
        const double grid = t.size_step > 0.0 ? t.size_step * 0.51 : 0.0;
        if (grid > 0.0 ? std::fabs(v.radius - metres(t.radius)) > grid : q12(v.radius) != t.radius) return false;
    }
    if ((t.caps & kSize) != 0 && (std::fabs(v.size[0] - t.size[0]) > 0.126 || std::fabs(v.size[1] - t.size[1]) > 0.126))
        return false;
    return true;
}

void ask_origin(const Thing& t) {
    s.origin_asked = true;
    s.fixes_valid = false;
    const Thing copy = t;
    const dt::EditValue file = s.history.original(t.key).value_or(value_of(t));
    defer([copy, file] {
        s.origin = load_origin(copy, file);
        s.fixes_valid = false;
    });
}

void select_thing(const Thing& t) {
    s.target = Target::Thing;
    if (s.key != t.key) {
        s.key = t.key;
        s.origin = Origin{};
        s.origin_asked = false;
    }
    s.edit = value_of(t);
    s.drag = Grip::None;
    s.fixes_valid = false;
}

// Tenths of a millimetre and hundredths of a degree: what the fields show is what the file gets.
dt::EditValue tidy(const Thing& t, dt::EditValue v) {
    for (double& p : v.position) p = std::round(p * 10000.0) / 10000.0;
    for (double& r : v.rotation) r = std::round(std::remainder(r, 360.0) * 100.0) / 100.0;
    if ((t.caps & kRadius) != 0) {
        const double grid = t.size_step > 0.0 ? t.size_step : 0.0001;
        v.radius = std::clamp(std::round(v.radius / grid) * grid, t.size_step > 0.0 ? 0.0 : 0.05, 4000.0);
    }
    if (t.size_step > 0.0) {
        for (double& e : v.size) e = std::clamp(std::round(e / t.size_step) * t.size_step, 0.0, 255.0 * t.size_step);
    }
    return v;
}

void apply_later(const Thing& t, const dt::EditValue& v) {
    const Thing copy = t;
    ++s.applying;
    defer([copy, v] {
        apply(copy, v);
        --s.applying;
    });
}

// A new value for the selected thing: shown, put on the thing in the game, and noted to step back from.
void set_value(const Thing& t, dt::EditValue v, int gesture) {
    v = tidy(t, v);
    if (v == s.edit) return;
    s.history.record(t.key, s.edit, v, gesture);
    s.edit = v;
    s.fixes_valid = false;
    apply_later(t, v);
}

// A step back or forward: the thing gets the step's value whether it is the selected one or not.
void restore(const std::string& key, const dt::EditValue& v) {
    const Thing* t = find_thing(key);
    if (t == nullptr) {
        say("the edited thing " + key + " is gone");
        return;
    }
    apply_later(*t, v);
    select_thing(*t);
    s.edit = v;
    say("restored " + key);
}

void undo() {
    if (const auto step = s.history.undo()) restore(step->key, step->before);
}

void redo() {
    if (const auto step = s.history.redo()) restore(step->key, step->after);
}

void reset_to_file(const Thing& t) {
    if (const auto original = s.history.original(t.key)) set_value(t, *original, 0);
}

// --- the cursor and the path ---

void cursor_to(const Vec3& p) {
    s.cursor_on = true;
    s.cursor[0] = std::round(p.x * 10000.0) / 10000.0;
    s.cursor[1] = std::round(p.y * 10000.0) / 10000.0;
    s.cursor[2] = std::round(p.z * 10000.0) / 10000.0;
}

// The ground under the cursor from the collision of the loaded cells (it keeps its height where there is none).
void cursor_to_ground() {
    defer([] {
        const std::int32_t y = gs::ground_height(q12(s.cursor[0]), q12(s.cursor[2]));
        if (y != kNoGround) s.cursor[1] = metres(y);
    });
}

std::string cursor_row() { return dt::raw_point_row(s.cursor[0], s.cursor[1], s.cursor[2]); }

std::string path_summary() {
    const double length = dt::path_length(s.path);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%zu points, %.3f m; %.2f s at %.3f m/s", s.path.size(), length,
                  dt::path_time(length, s.speed), s.speed);
    return buf;
}

// --- the target of the handles: the selected thing, the cursor or a path point ---

struct Active {
    bool valid = false;
    dt::EditValue value;
    unsigned caps = 0;
    Vec3 centre;
    const Thing* thing = nullptr;
};

Active active() {
    Active a;
    if (s.target == Target::Thing) {
        const Thing* t = selected();
        if (t == nullptr) return a;
        a.valid = true;
        a.thing = t;
        a.value = s.edit;
        a.caps = t->caps;
        a.centre = centre_of(*t, s.edit);
    } else if (s.target == Target::Cursor && s.cursor_on) {
        a.valid = true;
        a.value.position = {s.cursor[0], s.cursor[1], s.cursor[2]};
        a.caps = kMove | kHeight;
        a.centre = {s.cursor[0], s.cursor[1], s.cursor[2]};
    } else if (s.target == Target::Path && s.path_index >= 0 && s.path_index < static_cast<int>(s.path.size())) {
        const dt::PathPoint& p = s.path[static_cast<std::size_t>(s.path_index)];
        a.valid = true;
        a.value.position = {p.x, p.y, p.z};
        a.caps = kMove | kHeight;
        a.centre = {p.x, p.y, p.z};
    }
    return a;
}

void set_active(const Active& a, const dt::EditValue& v) {
    if (a.thing != nullptr) {
        set_value(*a.thing, v, s.gesture);
    } else if (s.target == Target::Cursor) {
        cursor_to({v.position[0], v.position[1], v.position[2]});
    } else if (s.target == Target::Path && s.path_index >= 0 && s.path_index < static_cast<int>(s.path.size())) {
        s.path[static_cast<std::size_t>(s.path_index)] = {std::round(v.position[0] * 10000.0) / 10000.0,
                                                          std::round(v.position[1] * 10000.0) / 10000.0,
                                                          std::round(v.position[2] * 10000.0) / 10000.0};
    }
}

// The length of the handles: about the same on the screen at any distance.
double handle_length(const Vec3& centre) {
    const Vec3 eye = camera_position();
    return std::clamp(0.14 * std::hypot(centre.x - eye.x, centre.y - eye.y, centre.z - eye.z), 0.35, 40.0);
}

Vec3 on_ring(const Vec3& c, Grip ring, double degrees, double radius) {
    const double a = degrees * kPi / 180.0;
    switch (ring) {
        case Grip::Pitch:
            return {c.x, c.y - radius * std::sin(a), c.z + radius * std::cos(a)};
        case Grip::Roll:
            return {c.x + radius * std::sin(a), c.y - radius * std::cos(a), c.z};
        default:
            return {c.x + radius * std::sin(a), c.y, c.z + radius * std::cos(a)};  // 0 faces +z, 90 +x
    }
}

std::vector<GripAt> grips(const Active& a) {
    std::vector<GripAt> out;
    if (!a.valid) return out;
    const Vec3& c = a.centre;
    const double L = handle_length(c);
    const Tool tool = a.thing != nullptr ? s.tool : Tool::Move;
    if (tool == Tool::Move && (a.caps & kMove) != 0) {
        out.push_back({Grip::Plane, c, kPlane});
        out.push_back({Grip::AxisX, {c.x + L, c.y, c.z}, kAxisX});
        out.push_back({Grip::AxisZ, {c.x, c.y, c.z + L}, kAxisZ});
        if ((a.caps & kHeight) != 0) out.push_back({Grip::Height, {c.x, c.y - L, c.z}, kAxisY});
    }
    if (tool == Tool::Rotate) {
        if ((a.caps & kYaw) != 0) out.push_back({Grip::Yaw, on_ring(c, Grip::Yaw, a.value.rotation[1], L), kAxisY});
        if ((a.caps & kTilt) != 0) {
            out.push_back({Grip::Pitch, on_ring(c, Grip::Pitch, a.value.rotation[0], L * 0.85), kAxisX});
            out.push_back({Grip::Roll, on_ring(c, Grip::Roll, a.value.rotation[2], L * 0.7), kAxisZ});
        }
    }
    if ((a.caps & kRadius) != 0) out.push_back({Grip::Radius, {c.x + a.value.radius, c.y, c.z}, kSizeColor});
    if ((a.caps & kSize) != 0) {
        out.push_back({Grip::SizeX, {c.x + a.value.size[0], c.y, c.z}, kSizeColor});
        out.push_back({Grip::SizeZ, {c.x, c.y, c.z + a.value.size[1]}, kSizeColor});
    }
    return out;
}

// Where a ray meets the plane through `p` with normal `n`; false when it runs along it or away from it.
bool ray_plane(const Vec3& o, const Vec3& d, const Vec3& p, const Vec3& n, Vec3* hit) {
    const double along = d.x * n.x + d.y * n.y + d.z * n.z;
    if (std::fabs(along) < 1e-6) return false;
    const double t = ((p.x - o.x) * n.x + (p.y - o.y) * n.y + (p.z - o.z) * n.z) / along;
    if (t <= 0.0) return false;
    *hit = {o.x + d.x * t, o.y + d.y * t, o.z + d.z * t};
    return true;
}

Vec3 ring_normal(Grip ring) {
    return ring == Grip::Pitch ? Vec3{1, 0, 0} : ring == Grip::Roll ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
}

double ring_angle(Grip ring, const Vec3& c, const Vec3& hit) {
    switch (ring) {
        case Grip::Pitch:
            return std::atan2(-(hit.y - c.y), hit.z - c.z) * 180.0 / kPi;
        case Grip::Roll:
            return std::atan2(hit.x - c.x, -(hit.y - c.y)) * 180.0 / kPi;
        default:
            return std::atan2(hit.x - c.x, hit.z - c.z) * 180.0 / kPi;
    }
}

bool begin_drag(const Active& a, Grip grip, const Vec3& o, const Vec3& d) {
    const Vec3& c = a.centre;
    Vec3 hit = c;
    if (grip == Grip::Height) {
        // A vertical plane facing the camera: the pointer's height in it is the handle's.
        const double len = std::hypot(d.x, d.z);
        s.height_normal = len > 1e-6 ? Vec3{d.x / len, 0.0, d.z / len} : Vec3{0, 0, 1};
        if (!ray_plane(o, d, c, s.height_normal, &hit)) return false;
    } else if (grip == Grip::Yaw || grip == Grip::Pitch || grip == Grip::Roll) {
        if (!ray_plane(o, d, c, ring_normal(grip), &hit)) return false;
        s.grab_angle = ring_angle(grip, c, hit);
    } else if (!ray_plane(o, d, c, {0, 1, 0}, &hit)) {
        return false;
    }
    s.grab_offset = {hit.x - c.x, hit.y - c.y, hit.z - c.z};
    // A size handle is grabbed at the edge: the edge follows the pointer itself.
    if (grip == Grip::Radius || grip == Grip::SizeX || grip == Grip::SizeZ) s.grab_offset = {};
    s.drag = grip;
    s.drag_from = a.value;
    ++s.gesture;
    return true;
}

void continue_drag(const Active& a, const Vec3& o, const Vec3& d) {
    const Vec3 c0{s.drag_from.position[0], a.centre.y, s.drag_from.position[2]};
    dt::EditValue v = a.value;
    Vec3 hit;
    switch (s.drag) {
        case Grip::Plane:
        case Grip::AxisX:
        case Grip::AxisZ: {
            if (s.target == Target::Cursor && s.cursor_snap && s.drag == Grip::Plane) {
                // On the drawn surface under the pointer; over a drop with nothing below, on its own level.
                if (ray_surface(o, d, &hit)) {
                    v.position = {hit.x, hit.y, hit.z};
                    break;
                }
            }
            if (!ray_plane(o, d, {0, a.centre.y, 0}, {0, 1, 0}, &hit)) return;
            if (s.drag != Grip::AxisZ) v.position[0] = hit.x - s.grab_offset.x;
            if (s.drag != Grip::AxisX) v.position[2] = hit.z - s.grab_offset.z;
            break;
        }
        case Grip::Height:
            if (!ray_plane(o, d, c0, s.height_normal, &hit)) return;
            v.position[1] = hit.y - s.grab_offset.y;
            break;
        case Grip::Yaw:
        case Grip::Pitch:
        case Grip::Roll: {
            if (!ray_plane(o, d, a.centre, ring_normal(s.drag), &hit)) return;
            const std::size_t i = s.drag == Grip::Yaw ? 1 : s.drag == Grip::Pitch ? 0 : 2;
            // Whole tenths of a degree; Ctrl turns by 15 degrees.
            const double grid = ImGui::GetIO().KeyCtrl ? 15.0 : 0.1;
            const double turned = s.drag_from.rotation[i] + ring_angle(s.drag, a.centre, hit) - s.grab_angle;
            v.rotation[i] = std::round(turned / grid) * grid;
            break;
        }
        case Grip::Radius:
            if (!ray_plane(o, d, a.centre, {0, 1, 0}, &hit)) return;
            v.radius = std::round(std::hypot(hit.x - a.centre.x, hit.z - a.centre.z) * 100.0) / 100.0;
            break;
        case Grip::SizeX:
        case Grip::SizeZ:
            if (!ray_plane(o, d, a.centre, {0, 1, 0}, &hit)) return;
            v.size[s.drag == Grip::SizeX ? 0 : 1] =
                std::fabs(s.drag == Grip::SizeX ? hit.x - a.centre.x : hit.z - a.centre.z);
            break;
        case Grip::None:
            return;
    }
    if (s.drag == Grip::Plane || s.drag == Grip::AxisX || s.drag == Grip::AxisZ || s.drag == Grip::Height) {
        // Whole millimetres under the pointer; Ctrl moves by the step of the fields. Only what the handle moves.
        const double grid = ImGui::GetIO().KeyCtrl ? std::max(s.step, 0.001) : 0.001;
        for (std::size_t i = 0; i < 3; ++i) {
            if (v.position[i] != a.value.position[i]) v.position[i] = std::round(v.position[i] / grid) * grid;
        }
    }
    set_active(a, v);
}

// The handle under the pointer: a knob within reach, else a ring or a zone's edge the ray passes close to.
Grip grip_under(const Active& a, const Vec3& o, const Vec3& d) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float reach = (kGripPixels + 5.0f) * ui_scale();
    Grip best = Grip::None;
    float best_d = reach;
    for (const GripAt& g : grips(a)) {
        float x, y;
        if (!project(g.at, &x, &y)) continue;
        const float dist = std::hypot(x - mouse.x, y - mouse.y);
        if (dist < best_d) {
            best_d = dist;
            best = g.grip;
        }
    }
    if (best != Grip::None) return best;
    const double L = handle_length(a.centre);
    const double slack = std::max(0.06, 0.012 * std::hypot(a.centre.x - o.x, a.centre.y - o.y, a.centre.z - o.z));
    Vec3 hit;
    if (a.thing != nullptr && s.tool == Tool::Rotate) {
        for (const auto& [ring, scale, cap] : {std::tuple{Grip::Yaw, 1.0, kYaw}, std::tuple{Grip::Pitch, 0.85, kTilt},
                                               std::tuple{Grip::Roll, 0.7, kTilt}}) {
            if ((a.caps & cap) == 0 || !ray_plane(o, d, a.centre, ring_normal(ring), &hit)) continue;
            const double r = std::hypot(hit.x - a.centre.x, hit.y - a.centre.y, hit.z - a.centre.z);
            if (std::fabs(r - L * scale) < slack) return ring;
        }
    }
    if ((a.caps & kRadius) != 0 && a.thing != nullptr && a.thing->kind == Kind::Zone &&
        ray_plane(o, d, a.centre, {0, 1, 0}, &hit)) {
        if (std::fabs(std::hypot(hit.x - a.centre.x, hit.z - a.centre.z) - a.value.radius) < slack) return Grip::Radius;
    }
    return Grip::None;
}

// --- the overlay: the selection, its handles, the cursor and the path ---

void draw_selection(Draw3D& d, const Thing& t, const dt::EditValue& v) {
    const Vec3 c = centre_of(t, v);
    const double yaw = v.rotation[1] * kPi / 180.0;
    const Vec3 facing{c.x + std::sin(yaw) * 0.9, c.y, c.z + std::cos(yaw) * 0.9};
    std::string label = t.label;
    char buf[64];
    switch (t.kind) {
        case Kind::Object:
        case Kind::Pickup:
            d.box({c.x - 0.25, c.y - 0.25, c.z - 0.25}, {c.x + 0.25, c.y + 0.25, c.z + 0.25}, kWhite, 3.0f);
            d.line(c, facing, kWhite, 2.0f);
            break;
        case Kind::Character:
        case Kind::Spawn:
            d.circle_xz(c, 0.5, kWhite, 20, 3.0f);
            d.line(c, {c.x, c.y - 1.7, c.z}, kWhite, 2.0f);
            d.line(c, facing, kWhite, 2.0f);
            break;
        case Kind::Zone:
            d.circle_xz(c, v.radius, kWhite, 48, 3.0f);
            d.line(c, {c.x, c.y - 1.0, c.z}, kWhite, 2.0f);
            std::snprintf(buf, sizeof(buf), "  r %.2f m", v.radius);
            label += buf;
            break;
        case Kind::Trigger:
        case Kind::Point:
            if ((t.caps & kSize) != 0) {
                d.box({c.x - v.size[0], c.y, c.z - v.size[1]}, {c.x + v.size[0], c.y - 1.0, c.z + v.size[1]}, kWhite,
                      3.0f);
                std::snprintf(buf, sizeof(buf), "  %.2f x %.2f m", v.size[0] * 2, v.size[1] * 2);
                label += buf;
            } else if (v.radius > 0.0) {
                d.circle_xz(c, v.radius, kWhite, 24, 3.0f);
            }
            d.cross(c, 0.3, kWhite, 3.0f);
            break;
        case Kind::Count:
            break;
    }
    d.label({c.x, c.y - 1.5, c.z}, label, kWhite);
}

void draw_handles(Draw3D& d, const Active& a, Grip hot) {
    const Vec3& c = a.centre;
    const double L = handle_length(c);
    const Tool tool = a.thing != nullptr ? s.tool : Tool::Move;
    if (tool == Tool::Rotate) {
        if ((a.caps & kYaw) != 0) d.ring(c, {1, 0, 0}, {0, 0, 1}, L, kAxisY, 48, hot == Grip::Yaw ? 3.5f : 2.0f);
        if ((a.caps & kTilt) != 0) {
            d.ring(c, {0, 1, 0}, {0, 0, 1}, L * 0.85, kAxisX, 48, hot == Grip::Pitch ? 3.5f : 2.0f);
            d.ring(c, {1, 0, 0}, {0, 1, 0}, L * 0.7, kAxisZ, 48, hot == Grip::Roll ? 3.5f : 2.0f);
        }
    }
    for (const GripAt& g : grips(a)) {
        if (g.grip != Grip::Plane && g.grip != Grip::Radius) d.line(c, g.at, g.color, 2.0f);
        d.knob(g.at, g.grip == hot ? kGripPixels + 3.0f : kGripPixels, g.grip == hot ? kWhite : g.color);
    }
}

void draw_cursor_and_path(Draw3D& d) {
    if (!s.path.empty()) {
        const std::vector<double> segments = dt::path_segments(s.path);
        double run = 0.0;
        for (std::size_t i = 0; i < s.path.size(); ++i) {
            const Vec3 p{s.path[i].x, s.path[i].y, s.path[i].z};
            const bool sel = s.target == Target::Path && s.path_index == static_cast<int>(i);
            char text[64];
            if (i == 0) {
                std::snprintf(text, sizeof(text), "1");
            } else {
                const Vec3 q{s.path[i - 1].x, s.path[i - 1].y, s.path[i - 1].z};
                d.line(q, p, kPathColor, 2.5f);
                run += segments[i - 1];
                std::snprintf(text, sizeof(text), "%zu  +%.2f = %.2f m", i + 1, segments[i - 1], run);
            }
            if (!sel) d.knob(p, kGripPixels - 1.0f, kPathColor);
            d.label({p.x, p.y - 0.3, p.z}, text, sel ? kWhite : kPathColor);
        }
    }
    if (s.cursor_on) {
        const Vec3 c{s.cursor[0], s.cursor[1], s.cursor[2]};
        d.cross(c, 0.35, kCursorColor, 2.0f);
        d.circle_xz(c, 0.2, kCursorColor, 16, 2.0f);
        if (s.target != Target::Cursor) d.knob(c, kGripPixels - 1.0f, kCursorColor);
        char text[96];
        std::snprintf(text, sizeof(text), "cursor %s, %s, %s m", dt::tidy_number(c.x).c_str(),
                      dt::tidy_number(c.y).c_str(), dt::tidy_number(c.z).c_str());
        d.label({c.x, c.y - 0.5, c.z}, text, kCursorColor);
    }
}

// A click on the knob of the cursor or of a path point makes it the target of the handles.
bool pick_cursor_or_path() {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float reach = (kGripPixels + 5.0f) * ui_scale();
    float x, y;
    for (std::size_t i = 0; i < s.path.size(); ++i) {
        if (s.target == Target::Path && s.path_index == static_cast<int>(i)) continue;
        if (project({s.path[i].x, s.path[i].y, s.path[i].z}, &x, &y) && std::hypot(x - mouse.x, y - mouse.y) < reach) {
            s.target = Target::Path;
            s.path_index = static_cast<int>(i);
            return true;
        }
    }
    if (s.cursor_on && s.target != Target::Cursor && project({s.cursor[0], s.cursor[1], s.cursor[2]}, &x, &y) &&
        std::hypot(x - mouse.x, y - mouse.y) < reach) {
        s.target = Target::Cursor;
        return true;
    }
    return false;
}

void draw_overlay(Draw3D& d) {
    if (!rt().loaded()) return;
    things();
    // The selected thing follows the game when something else moved it (the fields are not being edited then).
    if (const Thing* t = selected(); t != nullptr && s.drag == Grip::None && s.applying == 0 &&
                                     !engine_has(*t, s.edit) && !ImGui::IsAnyItemActive()) {
        s.edit = value_of(*t);
        s.fixes_valid = false;
    }
    draw_cursor_and_path(d);
    const Active a = active();
    if (a.thing != nullptr) draw_selection(d, *a.thing, a.value);

    const ImGuiIO& io = ImGui::GetIO();
    Vec3 o, dir;
    const bool pointer = pointer_ray(&o, &dir) && !io.WantCaptureMouse;
    Grip hot = s.drag;
    if (s.drag != Grip::None) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !a.valid) {
            s.drag = Grip::None;
        } else if (pointer_ray(&o, &dir)) {
            continue_drag(a, o, dir);
        }
    } else if (pointer) {
        if (a.valid) hot = grip_under(a, o, dir);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hot != Grip::None && begin_drag(a, hot, o, dir)) {
                s.click_frame = ImGui::GetFrameCount();
            } else if (pick_cursor_or_path()) {
                s.click_frame = ImGui::GetFrameCount();
            }
        }
    }
    if (a.valid) draw_handles(d, a, hot);
}

// --- selection by a click on the world ---

bool listed(const Thing& t, const Vec3& eye, double* distance) {
    const Vec3 p = place_of(t);
    const double dist = std::hypot(p.x - eye.x, p.z - eye.z);
    if (distance != nullptr) *distance = dist;
    if (!s.show[static_cast<int>(t.kind)]) return false;
    return !s.near_only || dist <= s.near || t.key == s.key;
}

bool on_pick(const Pick& pick) {
    if (pick.button != 0 || !rt().loaded() || !overlay_enabled(kOverlay)) return false;
    if (s.click_frame == ImGui::GetFrameCount()) return true;  // the handles took this click
    if (s.cursor_arm) {
        if (pick.hit) {
            cursor_to(pick.point);
            s.target = Target::Cursor;
        }
        s.cursor_arm = false;
        return true;
    }
    if (!s.click_selects) return false;
    const Vec3 eye = camera_position();
    const double hit_t = pick.hit ? std::hypot(pick.point.x - pick.ray_origin.x, pick.point.y - pick.ray_origin.y,
                                               pick.point.z - pick.ray_origin.z)
                                  : 1e9;
    const Thing* best = nullptr;
    double best_off = 1e9;
    for (const Thing& t : things()) {
        if (!listed(t, eye, nullptr)) continue;
        Vec3 p = place_of(t);
        if (t.kind == Kind::Character) p.y -= 0.9;  // the middle of the body, not the feet
        const double vx = p.x - pick.ray_origin.x, vy = p.y - pick.ray_origin.y, vz = p.z - pick.ray_origin.z;
        const double along = vx * pick.ray_dir.x + vy * pick.ray_dir.y + vz * pick.ray_dir.z;
        if (along <= 0 || along > hit_t + 3.0) continue;
        const double off =
            std::hypot(vx - along * pick.ray_dir.x, vy - along * pick.ray_dir.y, vz - along * pick.ray_dir.z);
        const double reach = std::max(t.kind == Kind::Character ? 0.9 : 0.6, 0.035 * along);
        if (off < reach && off < best_off) {
            best_off = off;
            best = &t;
        }
    }
    // Else the smallest zone the picked point lies in.
    if (best == nullptr && pick.hit) {
        double best_r = 1e9;
        for (const Thing& t : things()) {
            if (t.kind != Kind::Zone || !listed(t, eye, nullptr)) continue;
            const Vec3 p = place_of(t);
            const double r = metres(t.radius);
            if (std::hypot(pick.point.x - p.x, pick.point.z - p.z) <= r && r < best_r) {
                best_r = r;
                best = &t;
            }
        }
    }
    if (best == nullptr) return false;
    select_thing(*best);
    return true;
}

// --- the panel ---

void fly_to(const Vec3& p) {
    double yaw = 0;
    camera_angles(&yaw, nullptr);
    const double r = yaw * kPi / 180.0;
    const double back = std::max(6.0, 1.2 * (selected() != nullptr ? s.edit.radius : 0.0));
    place_camera({p.x - back * std::sin(r), p.y - 0.45 * back, p.z - back * std::cos(r)}, yaw, 22.0);
}

void copy_line(const std::string& where, const std::string& before, const std::string& after) {
    say(where);
    if (!before.empty()) say("  - " + before);
    say("  + " + after);
    copy_to_clipboard(after);
}

std::string all_fixes() {
    std::string out;
    for (const Correction& c : s.fixes) {
        if (c.after.empty()) continue;
        out += (out.empty() ? "" : "\n") + c.after;
    }
    return out;
}

void print_fixes() {
    if (s.fixes.empty()) say("nothing changed for " + s.key);
    for (const Correction& c : s.fixes) {
        say(c.where);
        if (!c.before.empty()) say("  - " + c.before);
        if (!c.after.empty()) say("  + " + c.after);
        if (!c.note.empty()) say("  note: " + c.note);
    }
}

const std::vector<Correction>& fixes(const Thing& t) {
    if (!s.fixes_valid || !(s.fixes_for == s.edit)) {
        const dt::EditValue original = s.history.original(t.key).value_or(s.edit);
        s.fixes = corrections(t, s.origin, original, s.edit);
        s.fixes_for = s.edit;
        s.fixes_valid = true;
    }
    return s.fixes;
}

// A number field that is dragged or typed into; true while it changes the value. A drag is one step back.
bool number(const char* label, double* v, double speed, const char* format) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.5f);
    const bool changed =
        ImGui::DragScalar(label, ImGuiDataType_Double, v, static_cast<float>(speed), nullptr, nullptr, format);
    if (ImGui::IsItemActivated()) ++s.gesture;
    return changed;
}

void draw_list() {
    const Vec3 eye = camera_position();
    int counts[kKinds] = {};
    for (const Thing& t : things()) ++counts[static_cast<int>(t.kind)];
    for (int k = 0; k < kKinds; ++k) {
        if (k % 2) ImGui::SameLine(ImGui::GetFontSize() * 15.0f);
        char label[64];
        std::snprintf(label, sizeof(label), "%s (%d)", kind_title(static_cast<Kind>(k)), counts[k]);
        ImGui::Checkbox(label, &s.show[k]);
    }
    ImGui::Checkbox("only within", &s.near_only);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off: the whole map. On: what lies within this distance of the camera.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    ImGui::SliderFloat("m", &s.near, 5.0f, 300.0f, "%.0f");
    ImGui::SameLine();
    ImGui::Checkbox("click selects", &s.click_selects);
    ImGui::InputTextWithHint("##search", "search: name, kind, what it does", s.search, sizeof(s.search));

    struct Row {
        const Thing* thing;
        double distance;
    };
    std::vector<Row> rows;
    const std::string q = lower(s.search);
    for (const Thing& t : things()) {
        double dist = 0;
        if (!listed(t, eye, &dist)) continue;
        if (!q.empty() &&
            lower(t.label + " " + kind_name(t.kind) + " " + t.does + " " + t.key).find(q) == std::string::npos)
            continue;
        rows.push_back({&t, dist});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.distance < b.distance; });
    if (ImGui::BeginChild("things", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 9), ImGuiChildFlags_Borders)) {
        for (const Row& r : rows) {
            const Thing& t = *r.thing;
            char text[256];
            std::snprintf(text, sizeof(text), "%-8s %s%s   %.1f m##%s", kind_name(t.kind), t.label.c_str(),
                          t.shown ? "" : "  (off now)", r.distance, t.key.c_str());
            const bool sel = s.target == Target::Thing && s.key == t.key;
            if (ImGui::Selectable(text, sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                select_thing(t);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) fly_to(place_of(t));
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\ndouble click: fly to it", t.does.c_str());
        }
        if (rows.empty()) ImGui::TextDisabled("nothing here: widen the distance or the kinds");
    }
    ImGui::EndChild();
    ImGui::TextDisabled("%zu shown of %zu on the map", rows.size(), things().size());
}

void draw_selected() {
    const Thing* t = selected();
    if (t == nullptr) {
        ImGui::TextWrapped(s.target == Target::Thing ? "The selected thing is gone (taken, killed, or the scene ended)."
                                                     : "Click a thing in the world or in the list.");
        return;
    }
    if (!s.origin_asked || s.origin.key != t->key) {
        if (!s.origin_asked) ask_origin(*t);
    }
    ImGui::Text("%s  %s", kind_name(t->kind), t->label.c_str());
    ImGui::TextDisabled("%s%s", t->does.c_str(), t->owner.empty() ? "" : ("  [" + t->owner + "]").c_str());

    int tool = static_cast<int>(s.tool);
    ImGui::RadioButton("select", &tool, 0);
    ImGui::SameLine();
    ImGui::RadioButton("move (1)", &tool, 1);
    ImGui::SameLine();
    ImGui::RadioButton("rotate (2)", &tool, 2);
    s.tool = static_cast<Tool>(tool);
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) s.tool = Tool::Move;
        if (ImGui::IsKeyPressed(ImGuiKey_2, false)) s.tool = Tool::Rotate;
    }

    dt::EditValue v = s.edit;
    bool changed = false;
    if ((t->caps & kMove) != 0) {
        changed |= number("x", &v.position[0], s.step, "%.4f");
        ImGui::SameLine();
        if ((t->caps & kHeight) != 0) {
            changed |= number("y", &v.position[1], s.step, "%.4f");
            ImGui::SameLine();
        }
        changed |= number("z  m", &v.position[2], s.step, "%.4f");
        ImGui::TextDisabled("raw %d, %d, %d", q12(v.position[0]), q12(v.position[1]), q12(v.position[2]));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
        static const double kSteps[] = {0.001, 0.01, 0.05, 0.1, 0.5, 1.0};
        char preview[32];
        std::snprintf(preview, sizeof(preview), "%g m", s.step);
        if (ImGui::BeginCombo("step", preview)) {
            for (const double st : kSteps) {
                char item[32];
                std::snprintf(item, sizeof(item), "%g m", st);
                if (ImGui::Selectable(item, st == s.step)) s.step = st;
            }
            ImGui::EndCombo();
        }
    }
    if ((t->caps & kYaw) != 0) {
        if ((t->caps & kTilt) != 0) {
            changed |= number("rx", &v.rotation[0], 0.5, "%.2f");
            ImGui::SameLine();
        }
        changed |= number((t->caps & kTilt) != 0 ? "ry" : "facing, degrees", &v.rotation[1], 0.5, "%.2f");
        if ((t->caps & kTilt) != 0) {
            ImGui::SameLine();
            changed |= number("rz  degrees", &v.rotation[2], 0.5, "%.2f");
        }
    }
    if ((t->caps & kRadius) != 0) {
        changed |= number(t->kind == Kind::Zone ? "radius, m" : "half width, m", &v.radius,
                          t->size_step > 0 ? t->size_step : 0.01, "%.3f");
    }
    if ((t->caps & kSize) != 0) {
        changed |= number("half x", &v.size[0], 0.05, "%.2f");
        ImGui::SameLine();
        changed |= number("half z  m", &v.size[1], 0.05, "%.2f");
    }
    if (changed) set_value(*t, v, s.gesture);
    if (t->kind == Kind::Zone) {
        ImGui::TextWrapped(
            "This moves the mark the script drew of its zone. The script's own test keeps its numbers until "
            "its file has the corrected line and is read again (mods reload).");
    }

    ImGui::BeginDisabled(!s.history.can_undo());
    if (ImGui::Button("Undo")) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!s.history.can_redo());
    if (ImGui::Button("Redo")) redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    const auto original = s.history.original(t->key);
    ImGui::BeginDisabled(!original || *original == s.edit);
    if (ImGui::Button("Reset to the file")) reset_to_file(*t);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back to what it had before the first edit (what its file says).");
    ImGui::SameLine();
    if (ImGui::Button("Fly to")) fly_to(centre_of(*t, s.edit));
    if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        ImGui::GetIO().KeyShift ? redo() : undo();
    }
    if (ImGui::SmallButton("Player here")) teleport_player(s.edit.position[0], s.edit.position[2]);
    ImGui::SameLine();
    if (ImGui::SmallButton("Cursor here")) cursor_to(centre_of(*t, s.edit));
    ImGui::SameLine();
    if (ImGui::SmallButton("To cursor") && s.cursor_on) {
        dt::EditValue to = s.edit;
        to.position = {s.cursor[0], (t->caps & kHeight) != 0 ? s.cursor[1] : s.edit.position[1], s.cursor[2]};
        set_value(*t, to, 0);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Deselect")) s.target = Target::None;

    ImGui::SeparatorText("Where it is written");
    if (s.origin.key != t->key) {
        ImGui::TextDisabled("looking...");
    } else {
        for (const std::string& row : s.origin.rows) ImGui::TextWrapped("%s", row.c_str());
        if (s.origin.place_lines.size() > 1) {
            if (ImGui::SmallButton("next matching line")) {
                s.origin.place_pick = (s.origin.place_pick + 1) % static_cast<int>(s.origin.place_lines.size());
                s.fixes_valid = false;
            }
            ImGui::SameLine();
            const dt::SourceLine& l = s.origin.place_lines[static_cast<std::size_t>(s.origin.place_pick)];
            ImGui::TextDisabled("line %d of %zu matches", l.line, s.origin.place_lines.size());
        }
        if (!s.origin.lua_file.empty()) {
            if (ImGui::SmallButton("Open in the Lua editor")) {
                const std::string file = s.origin.lua_file, mod = s.origin.lua_mod;
                const std::string where = s.origin.lua_where;
                defer([file, mod, where] {
                    // "<root>/<relative>": the relative part is what the owner text ends with.
                    const std::size_t at = where.rfind(mod.empty() ? "gamedata/" : ": ");
                    const std::string rel =
                        at == std::string::npos ? std::string() : where.substr(at + (mod.empty() ? 9 : 2));
                    if (rel.empty() || file.size() <= rel.size()) return;
                    (void)open_in_lua_editor(file.substr(0, file.size() - rel.size() - 1), rel, mod);
                });
            }
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("Look again")) ask_origin(*t);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Read the files again (after you edited them).");
    }

    ImGui::SeparatorText("The corrected line");
    const std::vector<Correction>& list = fixes(*t);
    if (list.empty()) {
        ImGui::TextDisabled("Nothing changed yet: move, turn or resize it.");
        return;
    }
    int id = 0;
    for (const Correction& c : list) {
        ImGui::PushID(id++);
        ImGui::TextWrapped("%s", c.where.c_str());
        if (!c.before.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.5f, 1.0f));
            ImGui::TextWrapped("- %s", c.before.c_str());
            ImGui::PopStyleColor();
        }
        if (!c.after.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 1.0f, 0.55f, 1.0f));
            ImGui::TextWrapped("+ %s", c.after.c_str());
            ImGui::PopStyleColor();
            if (ImGui::SmallButton("Copy")) copy_line(c.where, c.before, c.after);
            if (!c.record.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy the whole record")) copy_line(c.where + " (the record)", {}, c.record);
            }
        }
        if (!c.note.empty()) ImGui::TextDisabled("%s", c.note.c_str());
        ImGui::PopID();
    }
    if (ImGui::Button("Copy all")) {
        print_fixes();
        copy_to_clipboard(all_fixes());
    }
    ImGui::SameLine();
    if (ImGui::Button("Print to the console")) print_fixes();
}

void draw_cursor() {
    ImGui::Checkbox("show", &s.cursor_on);
    ImGui::SameLine();
    if (ImGui::SmallButton(s.cursor_arm ? "click the world..." : "Place by a click")) s.cursor_arm = !s.cursor_arm;
    ImGui::SameLine();
    if (ImGui::SmallButton("At the player")) {
        const sh::entities::Vec3 p = gs::chara_position(0);
        cursor_to({metres(p.x), metres(p.y), metres(p.z)});
        s.target = Target::Cursor;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("In front of the camera")) {
        Vec3 o = camera_position();
        double yaw = 0, pitch = 0;
        camera_angles(&yaw, &pitch);
        const double y = yaw * kPi / 180.0, p = pitch * kPi / 180.0;
        cursor_to(
            {o.x + 5.0 * std::sin(y) * std::cos(p), o.y + 5.0 * std::sin(p), o.z + 5.0 * std::cos(y) * std::cos(p)});
        s.target = Target::Cursor;
    }
    if (!s.cursor_on) {
        ImGui::TextDisabled("A point of its own for places with no ground: in the air, over a drop, over water.");
        return;
    }
    bool moved = number("x##c", &s.cursor[0], s.step, "%.4f");
    ImGui::SameLine();
    moved |= number("y##c", &s.cursor[1], s.step, "%.4f");
    ImGui::SameLine();
    moved |= number("z  m##c", &s.cursor[2], s.step, "%.4f");
    if (moved) {
        s.target = Target::Cursor;
        if (s.cursor_snap && !ImGui::IsItemActive()) cursor_to_ground();
    }
    int snap = s.cursor_snap ? 0 : 1;
    ImGui::RadioButton("snap to the ground", &snap, 0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A drag puts the cursor on the surface under the pointer.");
    ImGui::SameLine();
    ImGui::RadioButton("free height", &snap, 1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A drag keeps the cursor's height; the green handle changes it.");
    if ((snap == 0) != s.cursor_snap) {
        s.cursor_snap = snap == 0;
        if (s.cursor_snap) cursor_to_ground();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Handles on the cursor")) s.target = Target::Cursor;
    ImGui::Text("metres  %s, %s, %s", dt::tidy_number(s.cursor[0]).c_str(), dt::tidy_number(s.cursor[1]).c_str(),
                dt::tidy_number(s.cursor[2]).c_str());
    ImGui::Text("raw     %d, %d, %d", q12(s.cursor[0]), q12(s.cursor[1]), q12(s.cursor[2]));
    const std::string row = cursor_row();
    if (ImGui::SmallButton("Copy##row")) {
        say(row);
        copy_to_clipboard(row);
    }
    ImGui::SameLine();
    ImGui::TextWrapped("%s", row.c_str());
    if (ImGui::SmallButton("Player to the cursor")) teleport_player(s.cursor[0], s.cursor[2]);
    ImGui::SameLine();
    if (ImGui::SmallButton("Fly to##c")) fly_to({s.cursor[0], s.cursor[1], s.cursor[2]});
}

void print_path() {
    say("path: " + path_summary());
    const std::vector<double> seg = dt::path_segments(s.path);
    for (std::size_t i = 0; i < seg.size(); ++i) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "  %zu -> %zu: %.3f m, %.2f s", i + 1, i + 2, seg[i],
                      dt::path_time(seg[i], s.speed));
        say(buf);
    }
    say(dt::path_lua_raw(s.path));
}

void draw_path() {
    ImGui::BeginDisabled(!s.cursor_on);
    if (ImGui::Button("Add point")) {
        s.path.push_back({s.cursor[0], s.cursor[1], s.cursor[2]});
        s.target = Target::Cursor;  // the cursor goes on to the next point
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("A point at the cursor; the cursor stays for the next.");
    ImGui::SameLine();
    ImGui::BeginDisabled(s.path.empty());
    if (ImGui::Button("Clear")) {
        s.path.clear();
        if (s.target == Target::Path) s.target = Target::None;
    }
    ImGui::EndDisabled();
    if (s.path.empty()) {
        ImGui::TextDisabled("Put the cursor, add a point, move the cursor, add the next.");
        return;
    }
    const std::vector<double> seg = dt::path_segments(s.path);
    int remove = -1;
    for (std::size_t i = 0; i < s.path.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        char text[160];
        if (i == 0) {
            std::snprintf(text, sizeof(text), "%zu   %s, %s, %s", i + 1, dt::tidy_number(s.path[i].x, 3).c_str(),
                          dt::tidy_number(s.path[i].y, 3).c_str(), dt::tidy_number(s.path[i].z, 3).c_str());
        } else {
            std::snprintf(text, sizeof(text), "%zu   %s, %s, %s   +%.3f m  %.2f s", i + 1,
                          dt::tidy_number(s.path[i].x, 3).c_str(), dt::tidy_number(s.path[i].y, 3).c_str(),
                          dt::tidy_number(s.path[i].z, 3).c_str(), seg[i - 1], dt::path_time(seg[i - 1], s.speed));
        }
        if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::Selectable(text, s.target == Target::Path && s.path_index == static_cast<int>(i))) {
            s.target = Target::Path;
            s.path_index = static_cast<int>(i);
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        s.path.erase(s.path.begin() + remove);
        if (s.target == Target::Path) s.target = Target::None;
    }
    const double length = dt::path_length(s.path);
    ImGui::Text("total %.3f m", length);
    if (number("speed, m/s", &s.speed, 0.01, "%.3f")) {
        s.speed = std::max(0.0, s.speed);
        s.seconds = 0.0;
    }
    ImGui::SameLine();
    double seconds = s.seconds > 0.0 ? s.seconds : dt::path_time(length, s.speed);
    if (number("time, s", &seconds, 0.05, "%.2f") && seconds > 0.0) {
        s.seconds = seconds;
        s.speed = dt::path_speed(length, seconds);
    }
    if (ImGui::SmallButton("Copy Lua (raw)")) {
        print_path();
        copy_to_clipboard(dt::path_lua_raw(s.path));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy Lua (metres)")) {
        print_path();
        copy_to_clipboard(dt::path_lua_metres(s.path));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy JSON")) {
        print_path();
        copy_to_clipboard(dt::path_json(s.path));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Print")) print_path();
}

void draw_panel() {
    static bool sized = false;
    if (!sized) {
        // At the right edge, as tall as the picture allows: the list, the fields and the corrected line together.
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float top = ImGui::GetFrameHeight() * 1.4f;
        const ImVec2 size(std::min(ImGui::GetFontSize() * 34.0f, display.x * 0.45f), display.y - top - 8.0f);
        ImGui::SetWindowSize(size, ImGuiCond_Always);
        ImGui::SetWindowPos(ImVec2(display.x - size.x - 8.0f, top), ImGuiCond_Always);
        sized = true;
    }
    if (!rt().loaded()) {
        ImGui::TextDisabled("No map loaded.");
        return;
    }
    bool handles = overlay_enabled(kOverlay);
    if (!handles) {
        ImGui::TextWrapped("The editor's overlay is off: nothing is selected by a click and no handle is drawn.");
        if (ImGui::SmallButton("Switch it on")) set_overlay_enabled(kOverlay, true);
    }
    if (ImGui::CollapsingHeader("Things on the map", ImGuiTreeNodeFlags_DefaultOpen)) draw_list();
    if (ImGui::CollapsingHeader("Selected", ImGuiTreeNodeFlags_DefaultOpen)) draw_selected();
    if (ImGui::CollapsingHeader("Cursor point", ImGuiTreeNodeFlags_DefaultOpen)) draw_cursor();
    if (ImGui::CollapsingHeader("Path", ImGuiTreeNodeFlags_DefaultOpen)) draw_path();
}

// --- the console: every action of the panel, for scripted runs ---

void info(sh::console::Context& ctx, const std::string& line) {
    ctx.out.info(line);
    say(line);
}

std::string describe(const Thing& t) {
    const dt::EditValue v = t.key == s.key && s.target == Target::Thing ? s.edit : value_of(t);
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s %s | %s | at %s, %s, %s m | rot %s, %s, %s | r %s | size %s, %s%s",
                  kind_name(t.kind), t.key.c_str(), t.label.c_str(), dt::tidy_number(v.position[0]).c_str(),
                  dt::tidy_number(v.position[1]).c_str(), dt::tidy_number(v.position[2]).c_str(),
                  dt::tidy_number(v.rotation[0], 2).c_str(), dt::tidy_number(v.rotation[1], 2).c_str(),
                  dt::tidy_number(v.rotation[2], 2).c_str(), dt::tidy_number(v.radius).c_str(),
                  dt::tidy_number(v.size[0]).c_str(), dt::tidy_number(v.size[1]).c_str(), t.shown ? "" : " | off now");
    return buf;
}

Result<void> run_edit(sh::console::Context& ctx, const sh::console::Args& args) {
    const std::string action = args.str(0);
    const auto num = [&args](std::size_t i, double fallback = 0.0) { return args.number(i, fallback); };
    things(true);
    const Thing* t = selected();
    const auto need = [&ctx, &t]() {
        if (t == nullptr) ctx.out.error("edit: nothing is selected (edit select <key or text>)");
        return t != nullptr;
    };
    if (action == "list") {
        const std::string q = lower(args.joined(1));
        int n = 0;
        for (const Thing& thing : things()) {
            if (!q.empty() &&
                lower(thing.key + " " + thing.label + " " + kind_name(thing.kind)).find(q) == std::string::npos)
                continue;
            info(ctx, describe(thing));
            ++n;
        }
        info(ctx, std::to_string(n) + " things");
    } else if (action == "select") {
        const std::string q = lower(args.joined(1));
        const Thing* found = nullptr;
        for (const Thing& thing : things())
            if (lower(thing.key) == q) found = &thing;
        for (const Thing& thing : things()) {
            if (found != nullptr) break;
            if (lower(thing.key + " " + thing.label).find(q) != std::string::npos) found = &thing;
        }
        if (found == nullptr) {
            ctx.out.error("edit: nothing matches " + q);
            return Result<void>::ok();
        }
        select_thing(*found);
        ask_origin(*found);
        info(ctx, "selected " + describe(*found));
    } else if (action == "show") {
        if (need()) info(ctx, describe(*t));
    } else if (action == "tool") {
        s.tool = args.str(1) == "rotate" ? Tool::Rotate : args.str(1) == "select" ? Tool::Select : Tool::Move;
    } else if (action == "set" || action == "rot" || action == "radius" || action == "size") {
        if (!need()) return Result<void>::ok();
        dt::EditValue v = s.edit;
        if (action == "set") v.position = {num(1), num(2), num(3)};
        if (action == "rot") v.rotation = {num(1), num(2), num(3)};
        if (action == "radius") v.radius = num(1);
        if (action == "size") v.size = {num(1), num(2)};
        set_value(*t, v, 0);
        info(ctx, describe(*t));
    } else if (action == "undo") {
        undo();
    } else if (action == "redo") {
        redo();
    } else if (action == "reset") {
        if (need()) reset_to_file(*t);
    } else if (action == "origin") {
        if (!need()) return Result<void>::ok();
        const Thing copy = *t;
        const dt::EditValue file = s.history.original(copy.key).value_or(value_of(copy));
        s.origin = load_origin(copy, file);
        s.origin_asked = true;
        s.fixes_valid = false;
        for (const std::string& row : s.origin.rows) info(ctx, "origin: " + row);
    } else if (action == "copy") {
        if (!need()) return Result<void>::ok();
        if (s.origin.key != t->key) s.origin = load_origin(*t, s.history.original(t->key).value_or(value_of(*t)));
        s.fixes_valid = false;
        (void)fixes(*t);
        print_fixes();
        for (const Correction& c : s.fixes) {
            ctx.out.info(c.where);
            if (!c.before.empty()) ctx.out.info("  - " + c.before);
            if (!c.after.empty()) ctx.out.info("  + " + c.after);
            if (!c.note.empty()) ctx.out.info("  note: " + c.note);
        }
        if (!all_fixes().empty()) copy_to_clipboard(all_fixes());
    } else if (action == "grip") {
        // Where a handle stands in the world, for a scripted drag (editormouse dragw).
        const Active a = active();
        for (const GripAt& g : grips(a)) {
            if (args.str(1) != kGripNames[static_cast<int>(g.grip)]) continue;
            char buf[128];
            std::snprintf(buf, sizeof(buf), "grip %s at %.4f %.4f %.4f", args.str(1).c_str(), g.at.x, g.at.y, g.at.z);
            info(ctx, buf);
        }
    } else if (action == "filter") {
        const bool on = args.str(2) != "off" && args.str(2) != "0";
        for (int k = 0; k < kKinds; ++k)
            if (args.str(1) == "all" || args.str(1) == kind_name(static_cast<Kind>(k))) s.show[k] = on;
    } else if (action == "near") {
        s.near_only = args.str(1) != "off" && args.str(1) != "0";
        if (args.has(2)) s.near = static_cast<float>(num(2));
    } else if (action == "search") {
        std::snprintf(s.search, sizeof(s.search), "%s", args.joined(1).c_str());
    } else if (action == "cursor") {
        if (args.str(1) == "off") {
            s.cursor_on = false;
        } else if (args.str(1) == "snap" || args.str(1) == "free") {
            s.cursor_snap = args.str(1) == "snap";
            if (s.cursor_snap) cursor_to_ground();
        } else if (args.str(1) == "ground") {
            cursor_to_ground();
        } else if (args.has(3)) {
            cursor_to({num(1), num(2), num(3)});
            s.target = Target::Cursor;
        }
        info(ctx, "cursor " + cursor_row());
    } else if (action == "path") {
        const std::string what = args.str(1);
        if (what == "add") {
            if (args.has(4)) cursor_to({num(2), num(3), num(4)});
            s.path.push_back({s.cursor[0], s.cursor[1], s.cursor[2]});
        } else if (what == "del") {
            const int i = static_cast<int>(num(2)) - 1;
            if (i >= 0 && i < static_cast<int>(s.path.size())) s.path.erase(s.path.begin() + i);
        } else if (what == "clear") {
            s.path.clear();
        } else if (what == "speed") {
            s.speed = std::max(0.0, num(2));
            s.seconds = 0.0;
        } else if (what == "time") {
            s.seconds = num(2);
            s.speed = dt::path_speed(dt::path_length(s.path), s.seconds);
        } else if (what == "select") {
            s.target = Target::Path;
            s.path_index = static_cast<int>(num(2)) - 1;
        }
        print_path();
        ctx.out.info("path: " + path_summary());
    } else if (action == "tp") {
        if (args.has(2)) {
            teleport_player(num(1), num(2));
        } else if (s.cursor_on) {
            teleport_player(s.cursor[0], s.cursor[2]);
        }
    } else if (action == "fly") {
        if (need()) fly_to(centre_of(*t, s.edit));
    }
    return Result<void>::ok();
}

void register_command() {
    using sh::console::ArgSpec;
    using sh::console::ArgType;
    sh::console::CommandSpec spec;
    spec.name = "edit";
    spec.group = "debug";
    spec.help =
        "the placement editor (F2): list, select, move, turn and resize a thing of the map, print its "
        "corrected line; the cursor point and the path";
    spec.details =
        "list [text] | select <key or text> | show | set <x> <y> <z> | rot <rx> <ry> <rz> | radius <m> | size <hx> "
        "<hz> | "
        "undo | redo | reset | origin | copy | tool select|move|rotate | grip <move|x|z|height|yaw|pitch|roll|radius> "
        "| "
        "filter <kind|all> on|off | near on|off [m] | search <text> | cursor <x> <y> <z> | cursor snap|free|ground|off "
        "| "
        "path add [x y z] | path del <n> | path clear | path speed <m/s> | path time <s> | tp [x z] | fly. "
        "Metres (y down) and degrees. docs/modding/tools.md.";
    spec.args = {
        ArgSpec{"action",
                ArgType::Enum,
                false,
                "what to do",
                {"list", "select", "show", "set",    "rot",  "radius", "size",   "undo", "redo", "reset", "origin",
                 "copy", "tool",   "grip", "filter", "near", "search", "cursor", "path", "tp",   "fly"},
                {}},
        ArgSpec{"a", ArgType::String, true, "", {}, {}},
        ArgSpec{"b", ArgType::String, true, "", {}, {}},
        ArgSpec{"c", ArgType::String, true, "", {}, {}},
        ArgSpec{"d", ArgType::String, true, "", {}, {}},
        ArgSpec{"e", ArgType::String, true, "", {}, {}}};
    spec.run = &run_edit;
    (void)sh::console::global_registry().add(std::move(spec));
}

struct Install {
    Install() {
        register_command();
        register_panel({"tools.edit", "Placement editor", "World", &draw_panel, true, false});
        register_overlay({kOverlay, "Placement editor: selection, handles, cursor, path", &draw_overlay, true});
        // Before the map inspector and the Placement panel: a click the handles took selects nothing else.
        register_selection_handler("tools.edit", 10, &on_pick);
    }
};

const Install s_install;

}  // namespace

}  // namespace sh::tools::edit
