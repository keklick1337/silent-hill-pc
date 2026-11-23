// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The editor mode of the built-in modding tools: the Dear ImGui context on the SDL3 window,
// its draw lists handed to the engine's overlay (so the UI renders the same under GL and Vulkan, and never lands in
// the frame captures the overlay stays out of), the editor's enter and leave, the registries of tools.h and the
// console commands `editor` and `editorshot`.

#include "features/tools/tools.h"
#include "features/tools/tools_internal.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/app/pc_paths.h"
#include "engine/config/pc_config.h"
#include "engine/mods/extra_config.h"
#include "engine/console/instance.h"
#include "engine/console/registry.h"
#include "engine/devtools/tool_mode.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/log/sh_log.h"
#include "engine/platform/window.h"
#include "render/gpu/frame_driver.h"
#include "render/gpu/render_queries.h"
#include "render/gpu/render_state.h"
#include "render/overlay/overlay.h"
#include "render/scene/scene.h"
#include "render/scene/surface_query.h"
#include "render/world/world_scene_source.h"

extern SDL_Window* g_window;
extern int g_DebugCamEnabled;
extern "C" {
extern int g_PcConsoleInputActive;
extern int g_PcDebugMenuActive;
void Pc_FreeCam_Set(int on);
void Pc_FreeCam_Position(std::int32_t* out);
void Pc_FreeCam_Place(const std::int32_t* position, std::int32_t yaw);
void Pc_FreeCam_Angles(std::int32_t* yaw, std::int32_t* pitch);
void Pc_FreeCam_Aim(double yaw, double pitch);
void Pc_FreeCam_ExactAngles(double* yaw, double* pitch);
}

namespace sh::tools {

namespace gs = sh::game_state;

namespace {

constexpr double kGeomPerMetre = 256.0;
constexpr double kNearView = 16.0;  // geometry units in front of the eye: closer points are clipped
constexpr double kPickReach = 400.0 * kGeomPerMetre;

struct PanelEntry {
    Panel panel;
    bool open = true;
};

struct OverlayEntry {
    Overlay overlay;
};

struct HandlerEntry {
    std::string id;
    int priority = 0;
    SelectFn fn;
};

struct Host {
    bool imgui_ready = false;
    bool font_loaded = false;
    float ui_scale = 0.0f;  // the style's scale, set at the first draw
    float ui_scale_edit = 0.0f;  // the menu bar's slider while it is dragged (applied on release)
    std::vector<PanelEntry> panels;
    std::vector<OverlayEntry> overlays;
    std::vector<HandlerEntry> handlers;
    std::vector<std::function<void()>> deferred;
    Pick pick;
    bool toggle_requested = false;
    // A shot of the next presented frame: with the tool UI or without it.
    std::string shot_path;
    bool shot_with_ui = false;
    int shot_frames = 0;
    bool shot_window = false;  // the window form (shotwin)  // frames until the capture (the UI must be drawn, or left out, in a whole frame first)
    bool hide_ui_this_frame = false;
    bool scripted_pick = false;
    // A scripted turn (`editorturn`): angle units added to the camera's aim at each of the next updates.
    int turn_frames = 0;
    double turn_yaw = 0.0, turn_pitch = 0.0;
    float scripted_pick_at[2] = {0.5f, 0.5f};  // picture fractions
    // The console's own `shot` and `shotwin`, wrapped so the editor's UI stays out of them.
    std::function<Result<void>(sh::console::Context&, const sh::console::Args&)> shot_run, shotwin_run;
};

Host& host() {
    static Host h;
    return h;
}

// --- the editor's view of the frame ---

struct ViewCamera {
    double m[3][3] = {};
    double t[3] = {};
    double h = 1.0, ofs_x = 0.0, ofs_y = 0.0;
    float rect[4] = {-160.f, 160.f, -120.f, 120.f};  // the world pass's prim rectangle (left, right, top, bottom)
    overlay::Rect picture;                           // where the picture sits on the overlay target
    bool valid = false;
};

ViewCamera s_view;

void capture_view() {
    const world_scene::source::Camera c = world_scene::source::camera();
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) s_view.m[r][k] = c.m[r][k];
        s_view.t[r] = c.t[r];
    }
    s_view.h = c.h > 0.0f ? c.h : 1.0;
    s_view.ofs_x = c.ofs_x;
    s_view.ofs_y = c.ofs_y;
    for (int i = 0; i < 4; ++i) s_view.rect[i] = render::published().world_ortho_rect[static_cast<std::size_t>(i)];
    s_view.picture = overlay::overlay().frame().picture;
    s_view.valid = s_view.picture.w > 0.f && s_view.picture.h > 0.f && s_view.rect[1] != s_view.rect[0] &&
                   s_view.rect[3] != s_view.rect[2];
}

// World metres -> view space (geometry units).
void to_view(const Vec3& p, double v[3]) {
    const double g[3] = {p.x * kGeomPerMetre, p.y * kGeomPerMetre, p.z * kGeomPerMetre};
    for (int r = 0; r < 3; ++r) v[r] = s_view.m[r][0] * g[0] + s_view.m[r][1] * g[1] + s_view.m[r][2] * g[2] + s_view.t[r];
}

// View space -> overlay pixels; the point must be in front of the eye.
void view_to_screen(const double v[3], float* x, float* y) {
    const double sx = s_view.h * v[0] / v[2] + s_view.ofs_x;
    const double sy = s_view.h * v[1] / v[2] + s_view.ofs_y;
    const double fx = (sx - s_view.rect[0]) / (s_view.rect[1] - s_view.rect[0]);
    const double fy = (sy - s_view.rect[2]) / (s_view.rect[3] - s_view.rect[2]);
    *x = static_cast<float>(s_view.picture.x + fx * s_view.picture.w);
    *y = static_cast<float>(s_view.picture.y + fy * s_view.picture.h);
}

// The camera ray through an overlay pixel, in metres.
bool screen_ray(float x, float y, Vec3* origin, Vec3* dir) {
    if (!s_view.valid) return false;
    const double fx = (x - s_view.picture.x) / s_view.picture.w;
    const double fy = (y - s_view.picture.y) / s_view.picture.h;
    const double sx = s_view.rect[0] + fx * (s_view.rect[1] - s_view.rect[0]);
    const double sy = s_view.rect[2] + fy * (s_view.rect[3] - s_view.rect[2]);
    const double dv[3] = {(sx - s_view.ofs_x) / s_view.h, (sy - s_view.ofs_y) / s_view.h, 1.0};
    double eye[3] = {}, dw[3] = {};
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            eye[c] -= s_view.m[r][c] * s_view.t[r];
            dw[c] += s_view.m[r][c] * dv[r];
        }
    }
    const double len = std::sqrt(dw[0] * dw[0] + dw[1] * dw[1] + dw[2] * dw[2]);
    if (len <= 0.0) return false;
    *origin = {eye[0] / kGeomPerMetre, eye[1] / kGeomPerMetre, eye[2] / kGeomPerMetre};
    *dir = {dw[0] / len, dw[1] / len, dw[2] / len};
    return true;
}

ImU32 im_color(const Color& c) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
}

// --- ImGui on the engine overlay ---

void upload_textures(ImDrawData* data) {
    if (data->Textures == nullptr) return;
    overlay::TextureStore& store = overlay::overlay().textures();
    for (ImTextureData* tex : *data->Textures) {
        if (tex->Status == ImTextureStatus_OK) continue;
        const overlay::Format format = tex->Format == ImTextureFormat_Alpha8 ? overlay::Format::Alpha8
                                                                             : overlay::Format::Rgba8;
        if (tex->Status == ImTextureStatus_WantCreate) {
            const overlay::TextureId id =
                store.create(tex->Width, tex->Height, format, overlay::Filter::Linear,
                             static_cast<const std::uint8_t*>(tex->GetPixels()));
            tex->SetTexID(static_cast<ImTextureID>(id));
            tex->SetStatus(ImTextureStatus_OK);
        } else if (tex->Status == ImTextureStatus_WantUpdates) {
            // The whole texture again: the atlas only grows on new glyphs, which is rare.
            store.update(static_cast<overlay::TextureId>(tex->GetTexID()), tex->Width, tex->Height,
                         static_cast<const std::uint8_t*>(tex->GetPixels()));
            tex->SetStatus(ImTextureStatus_OK);
        } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
            store.destroy(static_cast<overlay::TextureId>(tex->GetTexID()));
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

void submit_draw_data(ImDrawData* data) {
    upload_textures(data);
    overlay::DrawList& list = overlay::overlay().list();
    std::vector<overlay::Vertex> tris;
    for (const ImDrawList* cl : data->CmdLists) {
        for (const ImDrawCmd& cmd : cl->CmdBuffer) {
            if (cmd.UserCallback != nullptr || cmd.ElemCount == 0) continue;
            const ImVec4 clip = cmd.ClipRect;
            if (clip.z <= clip.x || clip.w <= clip.y) continue;
            list.set_scissor(overlay::Space::Target, {clip.x, clip.y, clip.z - clip.x, clip.w - clip.y});
            tris.clear();
            tris.reserve(cmd.ElemCount);
            for (unsigned int i = 0; i < cmd.ElemCount; ++i) {
                const ImDrawVert& v = cl->VtxBuffer[cmd.VtxOffset + cl->IdxBuffer[cmd.IdxOffset + i]];
                overlay::Vertex o;
                o.x = v.pos.x;
                o.y = v.pos.y;
                o.u = v.uv.x;
                o.v = v.uv.y;
                // ImU32 colours keep red in the low byte.
                o.rgba[0] = static_cast<std::uint8_t>(v.col & 0xFF);
                o.rgba[1] = static_cast<std::uint8_t>((v.col >> 8) & 0xFF);
                o.rgba[2] = static_cast<std::uint8_t>((v.col >> 16) & 0xFF);
                o.rgba[3] = static_cast<std::uint8_t>((v.col >> 24) & 0xFF);
                tris.push_back(o);
            }
            list.triangles(overlay::Space::Target, tris, static_cast<overlay::TextureId>(cmd.GetTexID()));
        }
    }
    list.clear_scissor();
}

void init_imgui() {
    Host& h = host();
    if (h.imgui_ready || g_window == nullptr) return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // layouts are not written next to the exe
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;  // the engine owns the OS pointer (hidden)
    io.MouseDrawCursor = true;                               // the pointer is drawn with the UI
    ImGui_ImplSDL3_InitForOther(g_window);
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
    io.BackendRendererName = "sh_overlay";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::StyleColorsDark();
    // The console's bundled font (third_party/fonts/jetbrains-mono, OFL); ImGui's own when it is missing. Read here,
    // in the update, never from the draw hook.
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/fonts/JetBrainsMonoNL-Regular.ttf", PcPaths_ExeDir());
    if (SDL_GetPathInfo(path, nullptr)) h.font_loaded = io.Fonts->AddFontFromFileTTF(path, 15.0f) != nullptr;
    if (!h.font_loaded) io.Fonts->AddFontDefault();
    h.imgui_ready = true;
}

// --- editor state ---

struct Saved {
    int fog_disabled = 0;
    bool fog_enabled = true;
    int free_camera = 0;
    std::int32_t camera_pos[3] = {};
    std::int32_t camera_yaw = 0;
    int draw_distance_pct = 100;
    int whole_map = 2;
    float brightness = 1.0f;
    float contrast = 1.0f;
    bool collision_view = false;
};
Saved s_saved;

bool s_reenter = false;     // a warp left the editor: open it again when the new map is in play
int s_reenter_frames = 0;   // frames waited for it
int s_play_frames = 0;      // frames in a row the editor could hold the game

// The editor holds the game, so it opens in play and also over a script scene or a message (their next step waits
// until it is closed, and a map whose scene never ends, like map1_s04, stays editable). Not over a menu, a movie, a
// load, the save screen or the game over: those own the screen and the session state.
bool can_hold_game() {
    if (gs::game_state() != gs::GameState::InGame) return false;
    if (sh::game::session().music_status(sh::game::music_status::kPause)) return false;
    switch (gs::sys_state()) {
        case gs::SysState::Gameplay:
        case gs::SysState::ReadMessage:
        case gs::SysState::EventCallback:
        case gs::SysState::EventSetFlag:
        case gs::SysState::EventPlaySound:
            return true;
        default:
            return false;
    }
}

}  // namespace

// --- tools_internal.h ---

ViewerSettings& viewer() {
    static ViewerSettings v;
    return v;
}

void apply_viewer() {
    ViewerSettings& v = viewer();
    scene::ViewerOverrides& o = scene::viewer_overrides();
    o.active = true;
    o.wireframe = v.wireframe;
    o.full_bright = v.full_bright;
    o.light_scale = v.light_scale;
    o.fog_off = v.fog_off;
    o.flashlight_off = v.flashlight_off;
    // The world fog and its far drop: the free camera's own fog switch, the draw distance at its widest and every
    // exterior cell drawn.
    set_fog_debug_disabled(v.fog_off ? 1 : 0);
    if (!v.fog_off) set_world_fog_enabled(s_saved.fog_enabled);
    set_draw_distance(v.fog_off ? 200 : s_saved.draw_distance_pct, v.fog_off ? 1 : s_saved.whole_map);
    render::graphics().brightness = s_saved.brightness * v.brightness;
    render::graphics().contrast = s_saved.contrast * v.contrast;
    set_collision_view(v.collision_view);
}

void enter_editor() {
    if (tool_mode_active()) return;
    if (!tools_enabled()) {
        SH_DBG_ECHO("[EDITOR] off: Options > Interface > Modding Tools (F2), or modding_tools = 1 in config.cfg");
        return;
    }
    if (!can_hold_game() || g_PcConsoleInputActive || g_PcDebugMenuActive) {
        SH_DBG_ECHO("[EDITOR] opens in play only (not over a menu, a movie, a load or the game over)");
        return;
    }
    init_imgui();
    s_saved.fog_disabled = fog_debug_disabled();
    s_saved.fog_enabled = world_fog_enabled();
    s_saved.free_camera = g_DebugCamEnabled;
    if (g_DebugCamEnabled) {
        Pc_FreeCam_Position(s_saved.camera_pos);
        std::int32_t pitch = 0;
        Pc_FreeCam_Angles(&s_saved.camera_yaw, &pitch);
    }
    draw_distance(&s_saved.draw_distance_pct, &s_saved.whole_map);
    s_saved.brightness = render::graphics().brightness;
    s_saved.contrast = render::graphics().contrast;
    s_saved.collision_view = collision_view_enabled();

    sh::devtools::ToolMode& mode = sh::devtools::tool_mode();
    mode.active = true;
    mode.mouse_look = mode.keyboard_captured = mode.wheel_captured = false;
    if (!g_DebugCamEnabled) {
        Pc_FreeCam_Set(1);
        // From the game camera's place, turned toward the player: the view the player was looking at.
        std::int32_t cam[3] = {};
        Pc_FreeCam_Position(cam);
        const sh::entities::Vec3 p = gs::chara_position(0);
        const double dx = p.x - cam[0], dz = p.z - cam[2];
        if (dx != 0.0 || dz != 0.0) {
            const auto yaw = static_cast<std::int32_t>(std::lround(std::atan2(dx, dz) * 4096.0 / (2.0 * std::numbers::pi))) & 4095;
            Pc_FreeCam_Place(cam, yaw);
        }
    }
    apply_viewer();
    if (ImGui::GetCurrentContext() != nullptr) ImGui::GetIO().ClearInputKeys();
    SH_LOG("[EDITOR] on -- F2 or `editor off` leaves; right mouse button looks, WASD/QE fly");
}

void leave_editor() {
    if (!tool_mode_active()) return;
    sh::devtools::ToolMode& mode = sh::devtools::tool_mode();
    mode.active = false;
    mode.run_frames = 0;
    mode.mouse_look = mode.keyboard_captured = mode.wheel_captured = false;
    scene::viewer_overrides() = scene::ViewerOverrides{};
    set_draw_distance(s_saved.draw_distance_pct, s_saved.whole_map);
    render::graphics().brightness = s_saved.brightness;
    render::graphics().contrast = s_saved.contrast;
    set_collision_view(s_saved.collision_view);
    if (s_saved.free_camera) {
        Pc_FreeCam_Place(s_saved.camera_pos, s_saved.camera_yaw);
    } else if (g_DebugCamEnabled) {
        Pc_FreeCam_Set(0);
    }
    set_fog_debug_disabled(s_saved.fog_disabled);
    set_world_fog_enabled(s_saved.fog_enabled);
    SH_LOG("[EDITOR] off");
}

bool tool_mode_active() { return sh::devtools::tool_mode().active; }

void warp_to_map(const std::string& map) {
    defer([map] {
        const bool was = tool_mode_active();
        leave_editor();
        s_reenter = was;
        sh::devtools::tool_mode().hold_events = was;
        s_reenter_frames = 0;
        s_play_frames = 0;
        run_command_now("map " + map);
    });
}

void request_shot(const std::string& path, bool with_ui) {
    Host& h = host();
    h.shot_path = path;
    h.shot_with_ui = with_ui;
    h.shot_frames = 2;
}

// --- tools.h ---

std::int32_t q12(double metres) {
    volatile float scaled = static_cast<float>(metres) * 4096.0f;
    return static_cast<std::int32_t>(scaled);
}

double metres(std::int32_t q12) { return static_cast<double>(q12) / 4096.0; }

bool register_panel(Panel panel) {
    Host& h = host();
    for (const PanelEntry& e : h.panels)
        if (e.panel.id == panel.id) return false;
    PanelEntry e;
    e.open = panel.open;
    e.panel = std::move(panel);
    h.panels.push_back(std::move(e));
    return true;
}

bool register_overlay(Overlay overlay) {
    Host& h = host();
    for (const OverlayEntry& e : h.overlays)
        if (e.overlay.id == overlay.id) return false;
    h.overlays.push_back({std::move(overlay)});
    return true;
}

bool set_overlay_enabled(const std::string& id, bool on) {
    for (OverlayEntry& e : host().overlays) {
        if (e.overlay.id == id) {
            e.overlay.enabled = on;
            return true;
        }
    }
    return false;
}

bool overlay_enabled(const std::string& id) {
    for (const OverlayEntry& e : host().overlays)
        if (e.overlay.id == id) return e.overlay.enabled;
    return false;
}

bool register_selection_handler(std::string id, int priority, SelectFn fn) {
    Host& h = host();
    for (const HandlerEntry& e : h.handlers)
        if (e.id == id) return false;
    h.handlers.push_back({std::move(id), priority, std::move(fn)});
    std::stable_sort(h.handlers.begin(), h.handlers.end(),
                     [](const HandlerEntry& a, const HandlerEntry& b) { return a.priority < b.priority; });
    return true;
}

bool active() { return tool_mode_active(); }

void defer(std::function<void()> fn) { host().deferred.push_back(std::move(fn)); }

void run_command(const std::string& line) {
    defer([line] { run_command_now(line); });
}

void copy_to_clipboard(const std::string& text) {
    SDL_SetClipboardText(text.c_str());
    SH_LOG("[EDITOR] copied: %s", text.c_str());
}

const Pick& last_pick() { return host().pick; }

Vec3 camera_position() {
    std::int32_t p[3] = {};
    if (g_DebugCamEnabled) {
        Pc_FreeCam_Position(p);
    } else {
        gs::camera_position(p);
    }
    return {metres(p[0]), metres(p[1]), metres(p[2])};
}

void camera_angles(double* yaw_deg, double* pitch_deg) {
    std::int32_t yaw = 0, pitch = 0;
    Pc_FreeCam_Angles(&yaw, &pitch);
    if (yaw_deg) *yaw_deg = yaw * 360.0 / 4096.0;
    if (pitch_deg) *pitch_deg = pitch * 360.0 / 4096.0;
}

void place_camera(const Vec3& position, double yaw_deg, double pitch_deg) {
    const std::int32_t p[3] = {q12(position.x), q12(position.y), q12(position.z)};
    const auto yaw = static_cast<std::int32_t>(std::lround(yaw_deg * 4096.0 / 360.0)) & 4095;
    if (!g_DebugCamEnabled) return;
    Pc_FreeCam_Place(p, yaw);
    Pc_FreeCam_Aim(yaw_deg * 4096.0 / 360.0, pitch_deg * 4096.0 / 360.0);
}

// --- Draw3D ---

void Draw3D::line(const Vec3& a, const Vec3& b, const Color& color, float width) {
    if (!s_view.valid) return;
    double va[3], vb[3];
    to_view(a, va);
    to_view(b, vb);
    if (va[2] < kNearView && vb[2] < kNearView) return;
    // Clipped at the near plane: the far part of a line that passes the eye stays.
    auto clip = [](double* p, const double* q) {
        const double t = (kNearView - p[2]) / (q[2] - p[2]);
        for (int i = 0; i < 3; ++i) p[i] += (q[i] - p[i]) * t;
    };
    if (va[2] < kNearView) clip(va, vb);
    if (vb[2] < kNearView) clip(vb, va);
    float x0, y0, x1, y1;
    view_to_screen(va, &x0, &y0);
    view_to_screen(vb, &x1, &y1);
    ImGui::GetBackgroundDrawList()->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), im_color(color),
                                            width * std::max(1.0f, host().ui_scale));
}

void Draw3D::box(const Vec3& lo, const Vec3& hi, const Color& color, float width) {
    const Vec3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
                       {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    for (int i = 0; i < 4; ++i) {
        line(c[i], c[(i + 1) % 4], color, width);
        line(c[4 + i], c[4 + (i + 1) % 4], color, width);
        line(c[i], c[4 + i], color, width);
    }
}

void Draw3D::circle_xz(const Vec3& centre, double radius, const Color& color, int segments, float width) {
    segments = std::max(segments, 3);
    for (int i = 0; i < segments; ++i) {
        const double a0 = 2.0 * std::numbers::pi * i / segments, a1 = 2.0 * std::numbers::pi * (i + 1) / segments;
        line({centre.x + radius * std::cos(a0), centre.y, centre.z + radius * std::sin(a0)},
             {centre.x + radius * std::cos(a1), centre.y, centre.z + radius * std::sin(a1)}, color, width);
    }
}

void Draw3D::cross(const Vec3& p, double size, const Color& color, float width) {
    line({p.x - size, p.y, p.z}, {p.x + size, p.y, p.z}, color, width);
    line({p.x, p.y - size, p.z}, {p.x, p.y + size, p.z}, color, width);
    line({p.x, p.y, p.z - size}, {p.x, p.y, p.z + size}, color, width);
}

void Draw3D::label(const Vec3& p, const std::string& text, const Color& color) {
    float x, y;
    if (!project(p, &x, &y)) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    dl->AddRectFilled(ImVec2(x - 2, y - 2), ImVec2(x + size.x + 2, y + size.y + 2), IM_COL32(0, 0, 0, 150), 2.0f);
    dl->AddText(ImVec2(x, y), im_color(color), text.c_str());
}

bool Draw3D::project(const Vec3& p, float* x, float* y) const {
    if (!s_view.valid) return false;
    double v[3];
    to_view(p, v);
    if (v[2] < kNearView) return false;
    view_to_screen(v, x, y);
    return true;
}

// --- per frame ---

namespace {

void run_pick(int button, ImVec2 mouse) {
    Host& h = host();
    Pick p;
    p.button = button;
    if (!screen_ray(mouse.x, mouse.y, &p.ray_origin, &p.ray_dir)) return;
    const scene::SurfaceVec from = {p.ray_origin.x * kGeomPerMetre, p.ray_origin.y * kGeomPerMetre,
                                    p.ray_origin.z * kGeomPerMetre};
    const scene::SurfaceVec to = {from[0] + p.ray_dir.x * kPickReach, from[1] + p.ray_dir.y * kPickReach,
                                  from[2] + p.ray_dir.z * kPickReach};
    if (const auto hit = scene::solid_surface_on_segment(from, to, 0.0)) {
        p.hit = true;
        p.point = {hit->point[0] / kGeomPerMetre, hit->point[1] / kGeomPerMetre, hit->point[2] / kGeomPerMetre};
        p.normal = {hit->normal[0], hit->normal[1], hit->normal[2]};
    }
    if (button == 0) h.pick = p;
    for (const HandlerEntry& e : h.handlers)
        if (e.fn && e.fn(p)) break;
}

// The style at a scale: from ImGui's defaults each time, so a change never compounds.
void apply_ui_scale(float scale) {
    Host& h = host();
    h.ui_scale = std::clamp(scale, 0.5f, 3.0f);
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.ScaleAllSizes(h.ui_scale);
    style.FontScaleDpi = h.ui_scale;
    ImGui::GetStyle() = style;
    h.ui_scale_edit = h.ui_scale;
}

void save_ui_scale() {
    char value[32];
    std::snprintf(value, sizeof(value), "%.2f", host().ui_scale);
    const char* key = "editor_ui_scale";
    const char* values[] = {value};
    PcConfig_SaveKeyValues(&key, values, 1);
    sh::mods::extra_config().store(key, value);
}

// The maps in the table's order, from `current` by `step`, skipping empty slots.
int neighbour_map(int current, int step) {
    const int count = gs::map_count();
    for (int i = 1; i <= count; ++i) {
        const int at = ((current + step * i) % count + count) % count;
        if (gs::map_name(at) != nullptr) return at;
    }
    return current;
}

// The map list by area (the name up to its first '_'), one submenu each, the current map ticked.
void draw_maps_menu() {
    if (!ImGui::BeginMenu("Maps")) return;
    const int here = gs::current_map();
    std::string group;
    bool open = false;
    for (int i = 0; i < gs::map_count(); ++i) {
        const char* n = gs::map_name(i);
        if (n == nullptr) continue;
        const std::string name = n;
        const std::string g = name.substr(0, name.find('_'));
        if (g != group) {
            if (open) ImGui::EndMenu();
            group = g;
            const int here_in = here >= 0 && gs::map_name(here) != nullptr &&
                                std::string(gs::map_name(here)).rfind(g + "_", 0) == 0;
            open = ImGui::BeginMenu(here_in ? (g + "  *").c_str() : g.c_str());
        }
        if (!open) continue;
        const char* d = gs::map_description(i);
        if (ImGui::MenuItem(n, d, i == here)) warp_to_map(name);
    }
    if (open) ImGui::EndMenu();
    ImGui::Separator();
    ImGui::TextDisabled("PageUp / PageDown: previous / next map");
    ImGui::EndMenu();
}

void draw_menu_bar() {
    Host& h = host();
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("Editor")) {
        if (ImGui::MenuItem("Leave editor", "F2")) defer([] { leave_editor(); });
        if (ImGui::MenuItem("Screenshot with UI")) request_shot("editor_shot.png", true);
        if (ImGui::MenuItem("Screenshot without UI")) request_shot("editor_shot.png", false);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Panels")) {
        std::vector<std::string> menus;
        for (const PanelEntry& e : h.panels)
            if (std::find(menus.begin(), menus.end(), e.panel.menu) == menus.end()) menus.push_back(e.panel.menu);
        for (const std::string& m : menus) {
            ImGui::SeparatorText(m.empty() ? "Other" : m.c_str());
            for (PanelEntry& e : h.panels)
                if (e.panel.menu == m) ImGui::MenuItem(e.panel.title.c_str(), nullptr, &e.open);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Overlays")) {
        for (OverlayEntry& e : h.overlays) ImGui::MenuItem(e.overlay.label.c_str(), nullptr, &e.overlay.enabled);
        if (h.overlays.empty()) ImGui::TextDisabled("none registered");
        ImGui::EndMenu();
    }
    draw_maps_menu();

    // The current map with its neighbours one click away.
    const int here = gs::current_map();
    if (ImGui::ArrowButton("##prev_map", ImGuiDir_Left)) warp_to_map(gs::map_name(neighbour_map(here, -1)));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("previous map (PageUp)");
    const char* here_name = here >= 0 ? gs::map_name(here) : nullptr;
    ImGui::TextUnformatted(here_name != nullptr ? here_name : "?");
    if (here_name != nullptr && ImGui::IsItemHovered()) {
        const char* d = gs::map_description(here);
        if (d != nullptr) ImGui::SetTooltip("%s", d);
    }
    if (ImGui::ArrowButton("##next_map", ImGuiDir_Right)) warp_to_map(gs::map_name(neighbour_map(here, 1)));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("next map (PageDown)");
    if (!ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyShift) {
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) warp_to_map(gs::map_name(neighbour_map(here, -1)));
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) warp_to_map(gs::map_name(neighbour_map(here, 1)));
    }

    // The rooms of the map (or its map points), stepped the same way: the player is put there.
    const std::vector<RoomStop> stops = room_stops();
    if (!stops.empty()) {
        const int at = current_room_stop(stops);
        const int n = static_cast<int>(stops.size());
        auto step = [&stops, at, n](int by) { go_to_room_stop(stops[static_cast<std::size_t>(((at + by) % n + n) % n)]); };
        ImGui::Separator();
        if (ImGui::ArrowButton("##prev_room", ImGuiDir_Left)) step(-1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("previous room (Shift+PageUp)");
        char label[48];
        const RoomStop& here_stop = stops[static_cast<std::size_t>(std::max(at, 0))];
        if (here_stop.room != 0)
            std::snprintf(label, sizeof(label), "room %d (%d/%d)", here_stop.room, at + 1, n);
        else
            std::snprintf(label, sizeof(label), "point %d (%d/%d)", here_stop.point, at + 1, n);
        if (ImGui::BeginMenu(label)) {
            for (int i = 0; i < n; ++i) {
                const RoomStop& s = stops[static_cast<std::size_t>(i)];
                char item[96];
                if (s.room != 0)
                    std::snprintf(item, sizeof(item), "room %d   point %d   %.1f, %.1f m", s.room, s.point, s.x, s.z);
                else
                    std::snprintf(item, sizeof(item), "point %d   %.1f, %.1f m", s.point, s.x, s.z);
                if (ImGui::MenuItem(item, nullptr, i == at)) go_to_room_stop(s);
            }
            ImGui::EndMenu();
        }
        if (ImGui::ArrowButton("##next_room", ImGuiDir_Right)) step(1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("next room (Shift+PageDown)");
        if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyShift) {
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) step(-1);
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) step(1);
        }
    }

    // The UI scale: dragged freely, applied when let go (the bar itself is resized by it).
    ImGui::Separator();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    ImGui::SliderFloat("##ui_scale", &h.ui_scale_edit, 0.5f, 3.0f, "UI %.2fx");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("interface scale (saved to config.cfg, editor_ui_scale)");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        const float scale = h.ui_scale_edit;
        defer([scale] {
            apply_ui_scale(scale);
            save_ui_scale();
        });
    }
    ImGui::Separator();
    const Vec3 cam = camera_position();
    ImGui::TextDisabled("  camera %.2f %.2f %.2f m   |   RMB look, WASD/QE fly, Shift fast, LMB pick", cam.x, cam.y,
                        cam.z);
    ImGui::EndMainMenuBar();
}

void draw_ui() {
    Host& h = host();
    capture_view();
    ImGuiIO& io = ImGui::GetIO();

    Draw3D d3;
    for (OverlayEntry& e : h.overlays)
        if (e.overlay.enabled && e.overlay.draw) e.overlay.draw(d3);
    if (h.pick.hit) {
        d3.cross(h.pick.point, 0.25, {1.f, 0.85f, 0.1f, 1.f}, 2.0f);
    }

    draw_menu_bar();
    // First placement: side by side under the menu bar, a new row when the target is full.
    const float s = h.ui_scale;
    const float width = 360 * s, gap = 8 * s, top = ImGui::GetFrameHeight() + gap;
    const int per_row = std::max(1, static_cast<int>((io.DisplaySize.x - gap) / (width + gap)));
    int slot = 0;
    for (PanelEntry& e : h.panels) {
        const int col = slot % per_row, row = slot / per_row;
        ++slot;
        if (!e.open || !e.panel.draw) continue;
        ImGui::SetNextWindowPos(ImVec2(gap + col * (width + gap), top + row * 60 * s), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_FirstUseEver);
        const ImGuiWindowFlags flags = e.panel.auto_size ? ImGuiWindowFlags_AlwaysAutoResize : 0;
        if (ImGui::Begin(e.panel.title.c_str(), &e.open, flags)) e.panel.draw();
        ImGui::End();
    }

    // A click on the world, outside every window; or one asked for by `editorpick`.
    if (!io.WantCaptureMouse) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) run_pick(0, io.MousePos);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) run_pick(2, io.MousePos);
    }
    if (h.scripted_pick) {
        h.scripted_pick = false;
        run_pick(0, ImVec2(s_view.picture.x + h.scripted_pick_at[0] * s_view.picture.w,
                           s_view.picture.y + h.scripted_pick_at[1] * s_view.picture.h));
    }
}

void draw_hook() {
    Host& h = host();
    sh::devtools::ToolMode& mode = sh::devtools::tool_mode();
    if (!mode.active || !h.imgui_ready || g_window == nullptr) return;
    const overlay::Frame& frame = overlay::overlay().frame();
    if (!frame.valid()) return;

    ImGuiIO& io = ImGui::GetIO();
    if (h.ui_scale <= 0.0f) {
        // The saved scale, else sized for the target: 1 at 720 lines, never below 1.
        const char* saved = config_value("editor_ui_scale");
        const float fit = std::max(1.0f, static_cast<float>(frame.target_h) / 720.0f);
        const float scale = saved != nullptr ? std::strtof(saved, nullptr) : 0.0f;
        apply_ui_scale(scale > 0.0f ? scale : fit);
    }
    ImGui_ImplSDL3_NewFrame();
    // The overlay target, not the window: at render_scale above 1 or on a dense display it is the bigger one.
    io.DisplaySize = ImVec2(static_cast<float>(frame.target_w), static_cast<float>(frame.target_h));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    if (!mode.mouse_look) {
        float mx = 0, my = 0, ox = 0, oy = 0;
        SDL_GetMouseState(&mx, &my);
        sh::gpu::window_to_overlay(static_cast<int>(mx), static_cast<int>(my), &ox, &oy);
        io.AddMousePosEvent(ox, oy);
    } else {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    ImGui::NewFrame();
    draw_ui();
    ImGui::Render();
    mode.keyboard_captured = io.WantCaptureKeyboard || io.WantTextInput;
    mode.wheel_captured = io.WantCaptureMouse;
    if (h.hide_ui_this_frame) return;  // a shot without the UI: built, not drawn
    submit_draw_data(ImGui::GetDrawData());
}

void frame_end_hook();
void wrap_shot_commands();

void update_hook() {
    Host& h = host();
    sh::devtools::ToolMode& mode = sh::devtools::tool_mode();
    static bool s_started = false;
    if (!s_started) {
        // After the config is read and the renderer exists (static construction is before both).
        s_started = true;
        sh::gpu::add_frame_end_listener(&frame_end_hook);
        load_viewer_settings();
    }
    // The console registers its commands at its own start, after the first frames.
    if (!h.shot_run) wrap_shot_commands();

    if (h.toggle_requested) {
        h.toggle_requested = false;
        if (mode.active) {
            leave_editor();
        } else {
            enter_editor();
        }
    }

    // A map load, a menu or a death takes the game where the editor cannot hold it: the editor steps aside.
    if (mode.active && !can_hold_game()) leave_editor();

    if (!mode.active && s_reenter) {
        s_play_frames = can_hold_game() ? s_play_frames + 1 : 0;
        // A few frames of play first: the map's camera and cells settle after the load.
        if (s_play_frames > 20) {
            s_reenter = false;
            enter_editor();
            // The warp puts the player on the map's first point, which on some maps only a scene uses and which has
            // no floor (the cafe of map0_s01, the unused map1_s04): the player goes to a room stop with one.
            if (mode.active) stand_on_floor();
        } else if (++s_reenter_frames > 60 * 30) {
            s_reenter = false;
        }
    }

    mode.hold_events = mode.active || s_reenter;
    room_stop_tick();
    if (mode.run_frames > 0) --mode.run_frames;
    if (mode.active) apply_viewer();

    std::vector<std::function<void()>> work;
    work.swap(h.deferred);
    for (auto& fn : work) fn();

    if (h.turn_frames > 0) {
        --h.turn_frames;
        if (g_DebugCamEnabled) {
            double yaw = 0.0, pitch = 0.0;
            Pc_FreeCam_ExactAngles(&yaw, &pitch);
            Pc_FreeCam_Aim(yaw + h.turn_yaw, pitch + h.turn_pitch);
        }
    }
}

bool event_hook(const void* ev) {
    const SDL_Event& e = *static_cast<const SDL_Event*>(ev);
    Host& h = host();
    sh::devtools::ToolMode& mode = sh::devtools::tool_mode();
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F2 && !e.key.repeat &&
        !g_PcConsoleInputActive && tools_enabled()) {
        h.toggle_requested = true;
        return true;
    }
    if (!mode.active || !h.imgui_ready) return false;

    // The look button: held outside the UI, it turns the camera (relative pointer) and the UI does not see it.
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT &&
        !ImGui::GetIO().WantCaptureMouse) {
        mode.mouse_look = true;
        return true;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT && mode.mouse_look) {
        mode.mouse_look = false;
        return true;
    }
    if (mode.mouse_look && e.type == SDL_EVENT_MOUSE_MOTION) return true;

    // The console takes its keys itself while it is open.
    if (g_PcConsoleInputActive) return false;
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
        // The UI is laid out in overlay pixels (draw_hook), the event is in window points: fed as it is, a
        // pointer move in the same frame as a wheel turn or a click placed that input at the wrong spot whenever
        // the two differ (render_scale above 1, a dense display).
        SDL_Event mapped = e;
        sh::gpu::window_to_overlay(static_cast<int>(e.motion.x), static_cast<int>(e.motion.y), &mapped.motion.x,
                                   &mapped.motion.y);
        ImGui_ImplSDL3_ProcessEvent(&mapped);
        return true;
    }
    ImGui_ImplSDL3_ProcessEvent(&e);
    const ImGuiIO& io = ImGui::GetIO();
    switch (e.type) {
    case SDL_EVENT_MOUSE_WHEEL:
        return io.WantCaptureMouse;
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return true;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        // The console key always reaches the console.
        return (io.WantCaptureKeyboard || io.WantTextInput) && e.key.scancode != SDL_SCANCODE_GRAVE;
    case SDL_EVENT_TEXT_INPUT:
        return io.WantTextInput;
    default:
        return false;
    }
}

// The capture of `editorshot`: after the frame it asked for was presented.
void frame_end_hook() {
    Host& h = host();
    if (h.shot_frames <= 0) return;
    if (--h.shot_frames > 0) {
        h.hide_ui_this_frame = !h.shot_with_ui;
        return;
    }
    const std::string path = h.shot_path;
    h.shot_path.clear();
    if (g_DebugCamEnabled) {
        // Which camera the captured frame had: a scripted turn's frames are told apart by it.
        double yaw = 0.0, pitch = 0.0;
        Pc_FreeCam_ExactAngles(&yaw, &pitch);
        SH_LOG("[TOOLS] shot %s: camera yaw %.4f pitch %.4f (angle units)", path.c_str(), yaw, pitch);
    }
    h.hide_ui_this_frame = false;
    // The console's own capture, unwrapped: the frame just presented is the one asked for.
    auto& run = h.shot_window ? h.shotwin_run : h.shot_run;
    h.shot_window = false;
    if (run) {
        sh::console::Output* out = sh::console::active_output();
        struct Log final : sh::console::Output {
            void print(sh::console::Level, std::string_view line) override {
                SH_LOG("%.*s", static_cast<int>(line.size()), line.data());
            }
        } log;
        sh::console::Context ctx{out != nullptr ? *out : log, sh::console::global_registry()};
        std::vector<std::string> words;
        if (!path.empty()) words.push_back(path);
        (void)run(ctx, sh::console::Args(words));
    }
}

void register_commands() {
    using sh::console::ArgSpec;
    using sh::console::ArgType;
    sh::console::Registry& r = sh::console::global_registry();
    sh::console::CommandSpec editor;
    editor.name = "editor";
    editor.group = "debug";
    editor.help = "open or close the editor (modding tools, F2): frozen game, free camera, viewer panels";
    editor.args = {ArgSpec{"state", ArgType::Bool, true, "on/off; omitted toggles", {}, {}}};
    editor.details = "Needs Modding Tools (F2) on in Options > Interface (modding_tools = 1) and free gameplay. docs/modding/tools.md.";
    editor.run = [](sh::console::Context&, const sh::console::Args& args) -> Result<void> {
        const bool want = args.has(0) ? args.boolean(0) : !tool_mode_active();
        // At the next update: the console's own frame ends first.
        defer([want] { want ? enter_editor() : leave_editor(); });
        return Result<void>::ok();
    };
    (void)r.add(std::move(editor));

    sh::console::CommandSpec shot;
    shot.name = "editorshot";
    shot.group = "debug";
    shot.help = "save the next frame as PNG with the editor's UI (ui) or without it (noui, default)";
    shot.args = {ArgSpec{"file", ArgType::String, false, "output path (.png)", {}, {}},
                 ArgSpec{"mode", ArgType::Enum, true, "ui or noui", {"ui", "noui"}, {}}};
    shot.run = [](sh::console::Context& ctx, const sh::console::Args& args) -> Result<void> {
        request_shot(args.str(0), args.has(1) && args.str(1) == "ui");
        ctx.out.info("editorshot: capturing " + args.str(0));
        return Result<void>::ok();
    };
    (void)r.add(std::move(shot));

    // The viewer panel's controls by name, for scripted runs (-consolecmd) and key binds.
    sh::console::CommandSpec view;
    view.name = "editorview";
    view.group = "debug";
    view.help = "set a viewer option of the editor (fog_off, full_bright, light_scale, flashlight_off, brightness, "
                "contrast, wireframe, collision_view, cells)";
    view.args = {ArgSpec{"option", ArgType::Enum, false, "the option",
                         {"fog_off", "full_bright", "light_scale", "flashlight_off", "brightness", "contrast",
                          "wireframe", "collision_view", "cells"},
                         {}},
                 ArgSpec{"value", ArgType::Float, false, "0/1 for switches, a number for levels", {}, {}}};
    view.run = [](sh::console::Context&, const sh::console::Args& args) -> Result<void> {
        ViewerSettings& v = viewer();
        const std::string& k = args.str(0);
        const double x = args.number(1);
        const bool on = x != 0.0;
        if (k == "fog_off") v.fog_off = on;
        else if (k == "full_bright") v.full_bright = on;
        else if (k == "light_scale") v.light_scale = static_cast<float>(x);
        else if (k == "flashlight_off") v.flashlight_off = on;
        else if (k == "brightness") v.brightness = static_cast<float>(x);
        else if (k == "contrast") v.contrast = static_cast<float>(x);
        else if (k == "wireframe") v.wireframe = on;
        else if (k == "collision_view") v.collision_view = on;
        else if (k == "cells") set_overlay_enabled("tools.cell_bounds", on);
        return Result<void>::ok();
    };
    (void)r.add(std::move(view));

    // A left click on the world at a point of the picture, for scripted runs (no pointer offscreen).
    sh::console::CommandSpec pick;
    pick.name = "editorpick";
    pick.group = "debug";
    pick.help = "pick the world point under a place of the picture (fractions 0..1 from the top left)";
    pick.args = {ArgSpec{"x", ArgType::Float, false, "0..1 across", {}, {}},
                 ArgSpec{"y", ArgType::Float, false, "0..1 down", {}, {}}};
    pick.run = [](sh::console::Context&, const sh::console::Args& args) -> Result<void> {
        Host& h = host();
        h.scripted_pick = true;
        h.scripted_pick_at[0] = static_cast<float>(args.number(0));
        h.scripted_pick_at[1] = static_cast<float>(args.number(1));
        return Result<void>::ok();
    };
    (void)r.add(std::move(pick));

    sh::console::CommandSpec cam;
    cam.name = "editorcam";
    cam.group = "debug";
    cam.help = "put the editor's camera at x y z (metres) facing yaw, tilted by pitch (degrees, + looks down)";
    cam.args = {ArgSpec{"x", ArgType::Float, false, "metres", {}, {}}, ArgSpec{"y", ArgType::Float, false, "metres, +down", {}, {}},
                ArgSpec{"z", ArgType::Float, false, "metres", {}, {}}, ArgSpec{"yaw", ArgType::Float, true, "degrees", {}, {}},
                ArgSpec{"pitch", ArgType::Float, true, "degrees, + looks down", {}, {}}};
    cam.run = [](sh::console::Context&, const sh::console::Args& args) -> Result<void> {
        const Vec3 p{args.number(0), args.number(1), args.number(2)};
        const double yaw = args.number(3, 0.0), pitch = args.number(4, 0.0);
        defer([p, yaw, pitch] { place_camera(p, yaw, pitch); });
        return Result<void>::ok();
    };
    (void)r.add(std::move(cam));

    sh::console::CommandSpec warp;
    warp.name = "editorwarp";
    warp.group = "debug";
    warp.help = "the editor's map warp (PageUp/PageDown): next, prev or a map name";
    warp.args = {ArgSpec{"which", ArgType::String, false, "next, prev or a map name", {}, {}}};
    warp.run = [](sh::console::Context& ctx, const sh::console::Args& args) -> Result<void> {
        const std::string& w = args.str(0);
        const int here = gs::current_map();
        const char* name = w == "next"   ? gs::map_name(neighbour_map(here, 1))
                           : w == "prev" ? gs::map_name(neighbour_map(here, -1))
                                         : (gs::map_find(w.c_str()) >= 0 ? w.c_str() : nullptr);
        if (name == nullptr) {
            ctx.out.error("editorwarp: unknown map " + w);
            return Result<void>::ok();
        }
        ctx.out.info(std::string("editorwarp: ") + name);
        warp_to_map(name);
        return Result<void>::ok();
    };
    (void)r.add(std::move(warp));

    sh::console::CommandSpec room;
    room.name = "editorroom";
    room.group = "debug";
    room.help = "step the editor through the map's rooms (or map points): next, prev or a 1-based number";
    room.args = {ArgSpec{"which", ArgType::String, false, "next, prev or a number", {}, {}}};
    room.run = [](sh::console::Context& ctx, const sh::console::Args& args) -> Result<void> {
        const std::vector<RoomStop> stops = room_stops();
        if (stops.empty()) {
            ctx.out.error("editorroom: the map has no map points");
            return Result<void>::ok();
        }
        const int n = static_cast<int>(stops.size());
        const int at = std::max(current_room_stop(stops), 0);
        const std::string& w = args.str(0);
        const int to = w == "next" ? at + 1 : w == "prev" ? at - 1 : std::atoi(w.c_str()) - 1;
        const RoomStop& s = stops[static_cast<std::size_t>((to % n + n) % n)];
        go_to_room_stop(s);
        ctx.out.info("editorroom: room " + std::to_string(s.room) + ", point " + std::to_string(s.point) + " of " +
                     std::to_string(n) + " stops");
        return Result<void>::ok();
    };
    (void)r.add(std::move(room));

    // A steady turn over the next frames, for scripted runs: every frame of it can be compared with a still camera
    // at the same angles.
    sh::console::CommandSpec turn;
    turn.name = "editorturn";
    turn.group = "debug";
    turn.help = "turn the editor's camera by yaw and pitch (degrees) at each of the next frames";
    turn.args = {ArgSpec{"yaw", ArgType::Float, false, "degrees a frame", {}, {}},
                 ArgSpec{"pitch", ArgType::Float, false, "degrees a frame, + looks down", {}, {}},
                 ArgSpec{"frames", ArgType::Float, false, "how many frames", {}, {}}};
    turn.run = [](sh::console::Context&, const sh::console::Args& args) -> Result<void> {
        Host& h = host();
        h.turn_yaw = args.number(0) * 4096.0 / 360.0;
        h.turn_pitch = args.number(1) * 4096.0 / 360.0;
        h.turn_frames = static_cast<int>(args.number(2));
        return Result<void>::ok();
    };
    (void)r.add(std::move(turn));

    sh::console::CommandSpec panel;
    panel.name = "editorpanel";
    panel.group = "debug";
    panel.help = "show or hide an editor panel by id (tools.viewer, tools.navigation, tools.coordinates, ...; all)";
    panel.args = {ArgSpec{"id", ArgType::String, false, "panel id or all", {}, {}},
                  ArgSpec{"state", ArgType::Bool, false, "on/off", {}, {}}};
    panel.run = [](sh::console::Context& ctx, const sh::console::Args& args) -> Result<void> {
        bool found = false;
        for (PanelEntry& e : host().panels) {
            if (args.str(0) == "all" || e.panel.id == args.str(0)) {
                e.open = args.boolean(1);
                found = true;
            }
        }
        if (!found) ctx.out.error("editorpanel: no panel " + args.str(0));
        return Result<void>::ok();
    };
    (void)r.add(std::move(panel));
}

// `shot` and `shotwin` read the presented frame, which carries the overlay: while the editor is open they capture
// the next frame drawn without its UI instead. Wrapped once the console has registered them.
void wrap_shot_commands() {
    sh::console::Registry& r = sh::console::global_registry();
    Host& h = host();
    for (const char* name : {"shot", "shotwin"}) {
        const sh::console::CommandSpec* found = r.find(name);
        if (found == nullptr || found->name != name) continue;
        sh::console::CommandSpec spec = *found;
        auto original = spec.run;
        (std::string(name) == "shot" ? h.shot_run : h.shotwin_run) = original;
        const bool window = std::string(name) == "shotwin";
        spec.run = [original, window](sh::console::Context& ctx, const sh::console::Args& args) -> Result<void> {
            if (!tool_mode_active()) return original(ctx, args);
            Host& host_ref = host();
            host_ref.shot_window = window;
            request_shot(args.str(0), false);
            ctx.out.info("shot: the editor's UI is left out; the next frame is captured");
            return Result<void>::ok();
        };
        (void)r.add(std::move(spec));
    }
}

struct Install {
    Install() {
        sh::devtools::ToolMode& mode = sh::devtools::tool_mode();
        mode.update = &update_hook;
        mode.draw = &draw_hook;
        sh::platform::set_event_filter(&event_hook);
        register_commands();
        register_builtin_panels();
    }
};

const Install s_install;

}  // namespace

}  // namespace sh::tools
