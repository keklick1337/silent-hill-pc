// SPDX-License-Identifier: GPL-3.0-or-later
// The quick setup's window: four steps (language, renderer, graphics preset, display) drawn in the look of the
// game's menus (bullets, the gold underline, value arrows and the dark edge bands of gamedata/menu/menu_style.json)
// on a 640x480 canvas scaled to the window, text rasterised at the window's own size. Keyboard, mouse and game
// controllers all drive it.
#include "engine/app/quick_setup/hardware_probe.h"
#include "engine/app/quick_setup/quick_setup.h"
#include "engine/app/quick_setup/setup_text.h"

#include "engine/app/screen_kit/painter.h"
#include "engine/desktop/video_driver.h"
#include "engine/desktop/window_icon.h"
#include "i18n/core/language_set.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <unordered_map>

namespace sh::quick_setup {

namespace {

namespace fs = std::filesystem;

constexpr float kCanvasW = 640.0f;
constexpr float kCanvasH = 480.0f;
constexpr float kListTop = 112.0f;
constexpr float kListBottom = 368.0f;
constexpr float kLabelX = 92.0f;
constexpr float kBulletX = 74.0f;
constexpr float kRightX = 572.0f;
constexpr float kValueX = 430.0f;
constexpr float kValueHalf = 92.0f;
constexpr float kButtonY = 440.0f;
constexpr float kBackX = 170.0f;
constexpr float kNextX = 470.0f;
constexpr float kButtonHalf = 80.0f;

using sh::screen_kit::Align;
using sh::screen_kit::Rect;
using sh::screen_kit::Rgb;
using sh::screen_kit::TextFace;

enum class Step { Language, Renderer, Preset, Display };
constexpr int kStepCount = 4;

struct Row {
    std::string label;
    std::string detail;  // a second, smaller line
    std::string note;    // right column: "recommended" or why the row cannot be chosen
    bool note_gold = false;
    bool enabled = true;
    // The label's own language (the language list names each language in itself); no families: the interface's.
    TextFace face;
};

class Screen {
public:
    Screen(const Request& request, Hardware hw);
    ~Screen();
    Outcome run(Choices& out);

private:
    bool open_window();
    void set_language(std::size_t index);
    void build_rows();
    void draw();
    void present() { SDL_RenderPresent(renderer_); }
    void load_script(const fs::path& file);
    void run_command(const ScriptCommand& c, bool& done, Outcome& outcome);
    void go_to(Step step);
    void save_shot(const std::string& file);
    void handle(const SDL_Event& e, bool& done, Outcome& outcome);
    void move_focus(int dir);
    void step_value(int dir);
    void activate(bool& done, Outcome& outcome);
    void back(bool& done, Outcome& outcome);
    void choose_row(int row);
    void next(bool& done, Outcome& outcome);
    void pointer(float x, float y, bool click, bool& done, Outcome& outcome);

    // The pixel box of a canvas box (mirrored in a right-to-left language).
    Rect box(float x0, float y0, float x1, float y1) const { return paint_.box(x0, y0, x1, y1); }
    // A line in the row's own language when it has one.
    float draw_text(const std::string& s, float x, float y, float size, Rgb color, Align align, float max_w = 0,
                    const Row* own = nullptr, bool shadow = true) {
        const bool own_face = own != nullptr && !own->face.families.empty();
        return paint_.draw_text(s, x, y, size, color, align, max_w, own_face ? &own->face : nullptr, shadow);
    }

    int row_count() const { return static_cast<int>(rows_.size()); }
    int focus_back() const { return row_count(); }
    int focus_next() const { return row_count() + 1; }
    float row_pitch() const;
    float row_y(int i) const { return kListTop + row_pitch() * (static_cast<float>(i) + 0.5f); }
    int chosen_row() const;

    const Request& request_;
    Hardware hw_;
    Strings strings_;
    sh::screen_kit::Painter paint_;
    std::vector<sh::i18n::Language> languages_;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::vector<SDL_Gamepad*> pads_;
    int pixel_w_ = 0, pixel_h_ = 0;

    Step step_ = Step::Language;
    std::vector<Row> rows_;
    int focus_ = 0;
    bool rtl_ = false;

    std::size_t language_ = 0;
    Renderer renderer_choice_ = Renderer::Gl;
    std::size_t preset_ = 0;
    std::vector<Resolution> resolutions_;
    std::size_t resolution_ = 0;
    Resolution desktop_{};
    int window_mode_ = 1;  // index into kModes
    float stick_y_ = 0.0f, stick_x_ = 0.0f;
    std::vector<ScriptCommand> script_;
    std::size_t script_at_ = 0;
    bool scripted_ = false;
};

constexpr std::array<WindowMode, 3> kModes = {WindowMode::Windowed, WindowMode::Borderless, WindowMode::Fullscreen};
constexpr std::array<const char*, 3> kModeKeys = {"ui.setup.window_mode.windowed", "ui.setup.window_mode.borderless",
                                                  "ui.setup.window_mode.fullscreen"};

Screen::Screen(const Request& request, Hardware hw)
    : request_(request), hw_(std::move(hw)), paint_(request.base_data) {
    std::vector<std::string> warnings;
    const std::vector<fs::path> dirs{request.base_data / "text"};
    languages_ = sh::i18n::discover_languages(dirs, warnings);
    // English first, then the order of Options > Language.
    std::stable_sort(languages_.begin(), languages_.end(), [](const auto& a, const auto& b) {
        if ((a.code == "en") != (b.code == "en")) return a.code == "en";
        return a.menu_order != b.menu_order ? a.menu_order < b.menu_order : a.code < b.code;
    });
    if (languages_.empty()) {
        sh::i18n::Language en;
        en.code = "en";
        en.name = "English";
        languages_.push_back(en);
    }
    std::vector<std::string> codes;
    for (const auto& l : languages_) codes.push_back(l.code);
    const char* env = nullptr;
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        env = std::getenv(name);
        if (env && *env) break;
    }
    std::string locale = env ? env : "";
    if (locale.empty()) {
        // Windows and macOS keep the language outside the environment.
        int count = 0;
        if (SDL_Locale** locales = SDL_GetPreferredLocales(&count)) {
            if (count > 0 && locales[0] && locales[0]->language) locale = locales[0]->language;
            SDL_free(locales);
        }
    }
    const std::string start = initial_language(request.language, locale, codes);
    for (std::size_t i = 0; i < languages_.size(); ++i)
        if (languages_[i].code == start) language_ = i;

    renderer_choice_ = recommend_renderer(hw_);
    const std::string preset = recommend_preset(hw_, renderer_choice_, request_.presets);
    for (std::size_t i = 0; i < request_.presets.size(); ++i)
        if (request_.presets[i].name == preset) preset_ = i;
}

Screen::~Screen() {
    paint_.attach(nullptr);
    for (SDL_Gamepad* pad : pads_) SDL_CloseGamepad(pad);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
}

bool Screen::open_window() {
    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    std::vector<Resolution> modes;
    if (const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(display)) desktop_ = {desk->w, desk->h};
    int count = 0;
    if (SDL_DisplayMode** list = SDL_GetFullscreenDisplayModes(display, &count)) {
        for (int i = 0; i < count; ++i) modes.push_back({list[i]->w, list[i]->h});
        SDL_free(list);
    }
    resolutions_ = resolution_choices(modes, desktop_);
    resolution_ = nearest_resolution(resolutions_, desktop_.width > 0 ? desktop_ : request_.window);

    // A 4:3 window of most of the desktop's height: the screen is laid out for 4:3 and scales to any size.
    int h = 720;
    if (desktop_.height > 0) h = std::clamp(desktop_.height * 3 / 4, 480, 960);
    int w = h * 4 / 3;
    if (desktop_.width > 0 && w > desktop_.width * 9 / 10) {
        w = desktop_.width * 9 / 10;
        h = w * 3 / 4;
    }
    window_ = SDL_CreateWindow("Silent Hill", w, h,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                   static_cast<SDL_WindowFlags>(sh::desktop::hidden_window_flag()));
    if (window_ == nullptr) return false;
    SDL_SetWindowMinimumSize(window_, 320, 240);
    sh::desktop::set_window_icon(window_, request_.icons);
    // The offscreen driver's GL surface keeps the size the window was made with; the software renderer follows a
    // resize (a script's `size`).
    const char* driver = SDL_GetCurrentVideoDriver();
    const bool offscreen = driver != nullptr && SDL_strcmp(driver, "offscreen") == 0;
    renderer_ = SDL_CreateRenderer(window_, offscreen ? SDL_SOFTWARE_RENDERER : nullptr);
    if (renderer_ == nullptr) renderer_ = SDL_CreateRenderer(window_, SDL_SOFTWARE_RENDERER);
    if (renderer_ == nullptr) return false;
    SDL_SetRenderVSync(renderer_, 1);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    paint_.attach(renderer_);
    // Text input would hand the keys to an input method of the system.
    SDL_StopTextInput(window_);
    return true;
}

void Screen::set_language(std::size_t index) {
    language_ = index;
    const sh::i18n::Language& lang = languages_[index];
    strings_.load(request_.base_data / "text", lang.code);
    rtl_ = lang.direction == sh::i18n::Direction::Rtl;
    paint_.set_face(sh::screen_kit::face_for(lang.fonts, rtl_));
    paint_.set_view(paint_.view(), rtl_);
    if (window_) SDL_SetWindowTitle(window_, ("Silent Hill - " + strings_.get("ui.setup.title")).c_str());
    paint_.clear_text();
}

std::string vram_text(std::uint64_t bytes) {
    char text[32];
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    std::snprintf(text, sizeof text, gib >= 10.0 ? "%.0f" : "%.1f", gib);
    return text;
}

std::string resolution_text(Resolution r) { return std::to_string(r.width) + " × " + std::to_string(r.height); }

void Screen::build_rows() {
    rows_.clear();
    switch (step_) {
        case Step::Language:
            for (const auto& l : languages_) {
                Row row;
                row.label = l.name.empty() ? l.code : l.name;
                row.face = sh::screen_kit::face_for(l.fonts, l.direction == sh::i18n::Direction::Rtl);
                rows_.push_back(std::move(row));
            }
            break;
        case Step::Renderer:
            for (Renderer r : {Renderer::Gl, Renderer::Vulkan}) {
                Row row;
                const std::string key = std::string("ui.setup.renderer.") + std::string(renderer_name(r));
                row.label = strings_.get(key);
                row.detail = strings_.get(key + ".help");
                row.enabled = renderer_availability(hw_, r) == Availability::Available;
                if (!row.enabled) {
                    row.note = strings_.get(hw_.kind == GpuKind::Cpu ? "ui.setup.reason.vulkan_cpu"
                                                                     : "ui.setup.reason.no_vulkan");
                } else if (r == recommend_renderer(hw_)) {
                    row.note = strings_.get("ui.setup.recommended");
                    row.note_gold = true;
                }
                rows_.push_back(std::move(row));
            }
            break;
        case Step::Preset: {
            const std::string recommended = recommend_preset(hw_, renderer_choice_, request_.presets);
            for (const Preset& p : request_.presets) {
                Row row;
                const std::string value = "ui.options.value." + p.name;
                row.label = strings_.has(value) ? strings_.get(value) : p.name;
                const std::string help = "ui.setup.preset." + p.name;
                row.detail = strings_.has(help) ? strings_.get(help) : std::string();
                const Availability a = preset_availability(hw_, renderer_choice_, p);
                row.enabled = a == Availability::Available;
                if (!row.enabled) {
                    row.note = strings_.get(renderer_choice_ == Renderer::Vulkan ? "ui.setup.reason.no_ray_tracing"
                                                                                 : "ui.setup.reason.needs_vulkan");
                } else if (p.name == recommended) {
                    row.note = strings_.get("ui.setup.recommended");
                    row.note_gold = true;
                }
                rows_.push_back(std::move(row));
            }
            break;
        }
        case Step::Display: {
            Row res;
            res.label = strings_.get("ui.setup.resolution");
            Row mode;
            mode.label = strings_.get("ui.setup.window_mode");
            rows_.push_back(std::move(res));
            rows_.push_back(std::move(mode));
            break;
        }
    }
}

int Screen::chosen_row() const {
    switch (step_) {
        case Step::Language:
            return static_cast<int>(language_);
        case Step::Renderer:
            return renderer_choice_ == Renderer::Vulkan ? 1 : 0;
        case Step::Preset:
            return static_cast<int>(preset_);
        case Step::Display:
            return -1;
    }
    return -1;
}

float Screen::row_pitch() const {
    const float n = static_cast<float>(std::max(1, row_count()));
    const bool two_lines = step_ == Step::Renderer || step_ == Step::Preset;
    return std::min(two_lines ? 40.0f : 26.0f, (kListBottom - kListTop) / n);
}

void Screen::draw() {
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(renderer_, &w, &h);
    if (w != pixel_w_ || h != pixel_h_) {
        pixel_w_ = w;
        pixel_h_ = h;
        // The 640x480 canvas letterboxed in the window.
        sh::screen_kit::View view;
        view.scale = std::min(static_cast<float>(w) / kCanvasW, static_cast<float>(h) / kCanvasH);
        view.origin_x = (static_cast<float>(w) - kCanvasW * view.scale) * 0.5f;
        view.origin_y = (static_cast<float>(h) - kCanvasH * view.scale) * 0.5f;
        view.width = kCanvasW;
        view.height = kCanvasH;
        paint_.set_view(view, rtl_);
    }
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    paint_.edge_bands(w, h);

    namespace colors = sh::screen_kit::colors;
    const Rgb white = colors::kWhite, gold = colors::kGold, grey = colors::kGrey, dark = colors::kDark;
    draw_text(strings_.get("ui.setup.title"), kCanvasW / 2, 30, 26, white, Align::Center, 560);
    static constexpr std::array<const char*, kStepCount> kStepKeys = {"ui.setup.language", "ui.setup.renderer",
                                                                      "ui.setup.preset", "ui.setup.display"};
    const int step = static_cast<int>(step_);
    const std::string heading =
        strings_.format("ui.setup.step", {std::to_string(step + 1), std::to_string(kStepCount),
                                          strings_.get(kStepKeys[static_cast<std::size_t>(step)])});
    draw_text(heading, kCanvasW / 2, 64, 16, gold, Align::Center, 560);

    // What was detected, where it decides.
    std::string info;
    if (step_ == Step::Renderer || step_ == Step::Preset) {
        std::string gpu;
        if (hw_.vulkan) {
            gpu = hw_.gpu_name;
            if (hw_.vram_bytes > 0) gpu += ", " + strings_.format("ui.setup.vram", {vram_text(hw_.vram_bytes)});
            if (hw_.ray_tracing) gpu += ", " + strings_.get("ui.setup.ray_tracing");
        } else if (hw_.gl) {
            gpu = hw_.gl_renderer;
        }
        if (hw_.gl_software || hw_.kind == GpuKind::Cpu)
            gpu += (gpu.empty() ? "" : ", ") + strings_.get("ui.setup.software");
        info = gpu.empty() ? strings_.get("ui.setup.no_gpu") : strings_.format("ui.setup.detected", {gpu});
    } else if (step_ == Step::Display && desktop_.width > 0) {
        info = strings_.format("ui.setup.desktop", {resolution_text(desktop_)});
    }
    if (!info.empty()) draw_text(info, kCanvasW / 2, 88, 11, grey, Align::Center, 580);

    const float pitch = row_pitch();
    const int chosen = chosen_row();
    for (int i = 0; i < row_count(); ++i) {
        const Row& row = rows_[static_cast<std::size_t>(i)];
        const bool two = !row.detail.empty();
        const float y = row_y(i);
        const float label_y = two ? y - pitch * 0.18f : y;
        const float size = std::min(17.0f, pitch * (two ? 0.42f : 0.62f));
        const bool is_chosen = i == chosen;
        const Rgb color = !row.enabled ? dark : (is_chosen || step_ == Step::Display) ? white : grey;
        if (step_ != Step::Display) paint_.bullet(kBulletX, label_y, is_chosen && row.enabled);
        const float label_w =
            draw_text(row.label, kLabelX, label_y, size, color, Align::Left, step_ == Step::Display ? 220 : 300, &row);
        if (i == focus_) paint_.underline(kLabelX, kLabelX + std::max(label_w, 120.0f) + 6.0f, label_y + size * 0.62f);
        if (two)
            draw_text(row.detail, kLabelX, y + pitch * 0.24f, std::min(11.0f, pitch * 0.3f),
                      row.enabled ? Rgb{170, 170, 170} : dark, Align::Left, kRightX - kLabelX);
        if (!row.note.empty())
            draw_text(row.note, kRightX, label_y, std::min(12.0f, size * 0.8f), row.note_gold ? gold : dark,
                      Align::Right, 170);
        if (step_ == Step::Display) {
            const std::string value =
                i == 0 ? resolution_text(resolutions_[resolution_]) +
                             (resolutions_[resolution_] == desktop_ ? "  " + strings_.get("ui.setup.desktop_mark") : "")
                       : strings_.get(kModeKeys[static_cast<std::size_t>(window_mode_)]);
            draw_text(value, kValueX, y, size, i == focus_ ? white : grey, Align::Center, kValueHalf * 2 - 24);
            paint_.arrow(kValueX - kValueHalf, y, false, 5);
            paint_.arrow(kValueX + kValueHalf, y, true, 5);
        }
    }

    // The step's help, wrapped over the width.
    static constexpr std::array<const char*, kStepCount> kHelpKeys = {
        "ui.setup.language.help", "ui.setup.renderer.help", "ui.setup.preset.help", "ui.setup.display.help"};
    const std::vector<std::string> help =
        paint_.wrap(strings_.get(kHelpKeys[static_cast<std::size_t>(step)]), 12, 540);
    float hy = 392.0f - static_cast<float>(std::max<std::size_t>(help.size(), 1) - 1) * 7.0f;
    for (const std::string& line : help) {
        draw_text(line, kCanvasW / 2, hy, 12, {190, 190, 190}, Align::Center, 560);
        hy += 15.0f;
    }

    const bool last = step_ == Step::Display;
    const std::string back = strings_.get(step_ == Step::Language ? "ui.setup.skip" : "ui.setup.back");
    const std::string next = strings_.get(last ? "ui.setup.apply" : "ui.setup.next");
    for (int b = 0; b < 2; ++b) {
        const bool focused = focus_ == (b == 0 ? focus_back() : focus_next());
        const float x = b == 0 ? kBackX : kNextX;
        const float bw =
            draw_text(b == 0 ? back : next, x, kButtonY, 17, focused ? white : grey, Align::Center, kButtonHalf * 2);
        if (focused) {
            paint_.underline(x - bw * 0.5f - 4.0f, x + bw * 0.5f + 4.0f, kButtonY + 11.0f);
            paint_.arrow(x - bw * 0.5f - 16.0f, kButtonY, true, 4);
        }
    }
    draw_text(strings_.get("ui.setup.hint"), kCanvasW / 2, 468, 10, {120, 120, 120}, Align::Center, 600, nullptr,
              false);
}

void Screen::move_focus(int dir) {
    // The enabled rows, then the button row as one stop (on Next; left and right reach Back).
    std::vector<int> stops;
    for (int i = 0; i < row_count(); ++i)
        if (rows_[static_cast<std::size_t>(i)].enabled) stops.push_back(i);
    stops.push_back(focus_next());
    const int current = focus_ == focus_back() ? focus_next() : focus_;
    const auto at = std::find(stops.begin(), stops.end(), current);
    const int n = static_cast<int>(stops.size());
    const int index = at == stops.end() ? 0 : static_cast<int>(at - stops.begin());
    focus_ = stops[static_cast<std::size_t>((index + dir + n) % n)];
    if (focus_ < row_count() && step_ != Step::Display) choose_row(focus_);
}

void Screen::choose_row(int row) {
    if (row < 0 || row >= row_count() || !rows_[static_cast<std::size_t>(row)].enabled) return;
    switch (step_) {
        case Step::Language:
            if (static_cast<std::size_t>(row) != language_) {
                set_language(static_cast<std::size_t>(row));
                build_rows();
            }
            break;
        case Step::Renderer: {
            const Renderer r = row == 1 ? Renderer::Vulkan : Renderer::Gl;
            if (r != renderer_choice_) {
                renderer_choice_ = r;
                // The preset follows the renderer: what fits the hardware on it.
                const std::string p = recommend_preset(hw_, r, request_.presets);
                for (std::size_t i = 0; i < request_.presets.size(); ++i)
                    if (request_.presets[i].name == p) preset_ = i;
            }
            break;
        }
        case Step::Preset:
            preset_ = static_cast<std::size_t>(row);
            break;
        case Step::Display:
            break;
    }
}

void Screen::step_value(int dir) {
    if (step_ == Step::Display && focus_ == 0 && !resolutions_.empty()) {
        // Largest first: right goes to smaller sizes like the list reads.
        const int n = static_cast<int>(resolutions_.size());
        resolution_ = static_cast<std::size_t>((static_cast<int>(resolution_) + dir + n) % n);
    } else if (step_ == Step::Display && focus_ == 1) {
        window_mode_ = (window_mode_ + dir + 3) % 3;
    } else if (focus_ == focus_back() || focus_ == focus_next()) {
        focus_ = focus_ == focus_back() ? focus_next() : focus_back();
    }
}

void Screen::next(bool& done, Outcome& outcome) {
    if (step_ == Step::Display) {
        done = true;
        outcome = Outcome::Applied;
        return;
    }
    step_ = static_cast<Step>(static_cast<int>(step_) + 1);
    build_rows();
    focus_ = step_ == Step::Display ? 0 : std::max(0, chosen_row());
}

void Screen::back(bool& done, Outcome& outcome) {
    if (step_ == Step::Language) {
        done = true;
        outcome = Outcome::Skipped;
        return;
    }
    step_ = static_cast<Step>(static_cast<int>(step_) - 1);
    build_rows();
    focus_ = std::max(0, chosen_row());
}

void Screen::activate(bool& done, Outcome& outcome) {
    if (focus_ == focus_back()) {
        back(done, outcome);
    } else if (step_ == Step::Display && focus_ < row_count()) {
        // Enter on a value row moves on, so the keyboard reaches Apply without the arrows.
        focus_ = focus_ + 1 < row_count() ? focus_ + 1 : focus_next();
    } else {
        next(done, outcome);
    }
}

void Screen::pointer(float x, float y, bool click, bool& done, Outcome& outcome) {
    const float pitch = row_pitch();
    for (int i = 0; i < row_count(); ++i) {
        const float ry = row_y(i);
        if (step_ == Step::Display) {
            const Rect left = box(kValueX - kValueHalf - 12, ry - 10, kValueX - kValueHalf + 12, ry + 10);
            const Rect right = box(kValueX + kValueHalf - 12, ry - 10, kValueX + kValueHalf + 12, ry + 10);
            if (left.contains(x, y) || right.contains(x, y)) {
                focus_ = i;
                if (click) step_value(left.contains(x, y) ? -1 : 1);
                return;
            }
        }
        if (box(kBulletX - 10, ry - pitch * 0.5f, kRightX, ry + pitch * 0.5f).contains(x, y)) {
            if (!rows_[static_cast<std::size_t>(i)].enabled) return;
            focus_ = i;
            if (click && step_ != Step::Display) choose_row(i);
            if (click && step_ == Step::Display) step_value(1);
            return;
        }
    }
    for (int b = 0; b < 2; ++b) {
        const float bx = b == 0 ? kBackX : kNextX;
        if (box(bx - kButtonHalf, kButtonY - 14, bx + kButtonHalf, kButtonY + 14).contains(x, y)) {
            focus_ = b == 0 ? focus_back() : focus_next();
            if (click) activate(done, outcome);
            return;
        }
    }
}

void Screen::handle(const SDL_Event& event, bool& done, Outcome& outcome) {
    SDL_Event e = event;
    switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            done = true;
            outcome = Outcome::Quit;
            break;
        case SDL_EVENT_KEY_DOWN:
            switch (e.key.key) {
                case SDLK_UP:
                case SDLK_W:
                    move_focus(-1);
                    break;
                case SDLK_DOWN:
                case SDLK_S:
                    move_focus(1);
                    break;
                case SDLK_TAB:
                    move_focus((e.key.mod & SDL_KMOD_SHIFT) ? -1 : 1);
                    break;
                case SDLK_LEFT:
                case SDLK_A:
                    step_value(rtl_ ? 1 : -1);
                    break;
                case SDLK_RIGHT:
                case SDLK_D:
                    step_value(rtl_ ? -1 : 1);
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    activate(done, outcome);
                    break;
                case SDLK_ESCAPE:
                case SDLK_BACKSPACE:
                    if (step_ != Step::Language) back(done, outcome);
                    break;
                default:
                    break;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            SDL_ConvertEventToRenderCoordinates(renderer_, &e);
            pointer(e.motion.x, e.motion.y, false, done, outcome);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            SDL_ConvertEventToRenderCoordinates(renderer_, &e);
            if (e.button.button == SDL_BUTTON_LEFT) pointer(e.button.x, e.button.y, true, done, outcome);
            if (e.button.button == SDL_BUTTON_RIGHT && step_ != Step::Language) back(done, outcome);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (step_ == Step::Display && focus_ < row_count())
                step_value(e.wheel.y > 0 ? -1 : 1);
            else
                move_focus(e.wheel.y > 0 ? -1 : 1);
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (SDL_Gamepad* pad = SDL_OpenGamepad(e.gdevice.which)) pads_.push_back(pad);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            switch (e.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_UP:
                    move_focus(-1);
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                    move_focus(1);
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                    step_value(rtl_ ? 1 : -1);
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                    step_value(rtl_ ? -1 : 1);
                    break;
                case SDL_GAMEPAD_BUTTON_SOUTH:
                    activate(done, outcome);
                    break;
                case SDL_GAMEPAD_BUTTON_EAST:
                    if (step_ != Step::Language) back(done, outcome);
                    break;
                case SDL_GAMEPAD_BUTTON_START:
                    next(done, outcome);
                    break;
                default:
                    break;
            }
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            // The stick moves one step each time it crosses half way.
            const float v = static_cast<float>(e.gaxis.value) / 32767.0f;
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
                if (std::fabs(v) > 0.5f && std::fabs(stick_y_) <= 0.5f) move_focus(v > 0 ? 1 : -1);
                stick_y_ = v;
            } else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
                if (std::fabs(v) > 0.5f && std::fabs(stick_x_) <= 0.5f) step_value((v > 0) != rtl_ ? 1 : -1);
                stick_x_ = v;
            }
            break;
        }
        default:
            break;
    }
}

Outcome Screen::run(Choices& out) {
    if (!open_window()) return Outcome::NoDisplay;
    set_language(language_);
    build_rows();
    focus_ = std::max(0, chosen_row());
    if (!request_.script.empty()) load_script(request_.script);
    bool done = false;
    Outcome outcome = Outcome::Quit;
    while (!done) {
        draw();
        present();
        SDL_Event e;
        if (scripted_) {
            // One command a frame, so every command sees the screen the one before it left.
            while (!done && SDL_PollEvent(&e)) handle(e, done, outcome);
            if (done) break;
            if (script_at_ < script_.size()) {
                run_command(script_[script_at_++], done, outcome);
            } else {
                std::fprintf(stderr, "quick setup script: ended without apply or skip; the game ends\n");
                done = true;
                outcome = Outcome::Quit;
            }
        } else if (SDL_WaitEventTimeout(&e, 250)) {
            handle(e, done, outcome);
            while (!done && SDL_PollEvent(&e)) handle(e, done, outcome);
        }
    }
    out.language = languages_[language_].code;
    out.renderer = renderer_choice_;
    out.preset = request_.presets.empty() ? std::string() : request_.presets[preset_].name;
    out.resolution = resolutions_.empty() ? request_.window : resolutions_[resolution_];
    out.window_mode = kModes[static_cast<std::size_t>(window_mode_)];
    return outcome;
}

void Screen::load_script(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "quick setup script: cannot read %s\n", file.string().c_str());
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    script_ = parse_script(text);
    scripted_ = true;
}

void Screen::go_to(Step step) {
    step_ = step;
    build_rows();
    focus_ = step_ == Step::Display ? 0 : std::max(0, chosen_row());
}

void Screen::save_shot(const std::string& file) {
    draw();
    SDL_Surface* shot = SDL_RenderReadPixels(renderer_, nullptr);
    if (shot == nullptr || !SDL_SaveBMP(shot, file.c_str())) {
        std::fprintf(stderr, "quick setup script: shot %s failed: %s\n", file.c_str(), SDL_GetError());
    }
    if (shot) SDL_DestroySurface(shot);
}

// The script's verbs: lang <code>, renderer gl|vulkan, preset <name>, resolution <w>x<h>,
// mode windowed|borderless|fullscreen, step 1..4, key up|down|left|right|enter|back|tab, size <w>x<h> (the window),
// shot <file.bmp>, apply, skip, quit. A choice goes to its step first, as a player would.
void Screen::run_command(const ScriptCommand& c, bool& done, Outcome& outcome) {
    const auto fail = [&](const char* why) {
        std::fprintf(stderr, "quick setup script: %s %s: %s\n", c.verb.c_str(), c.arg.c_str(), why);
    };
    const auto pick = [&](Step step, int row) {
        go_to(step);
        if (row < 0 || row >= row_count() || !rows_[static_cast<std::size_t>(row)].enabled) {
            fail("not offered here");
            return;
        }
        focus_ = row;
        choose_row(row);
    };
    std::fprintf(stderr, "quick setup script: %s %s\n", c.verb.c_str(), c.arg.c_str());
    if (c.verb == "lang") {
        int row = -1;
        for (std::size_t i = 0; i < languages_.size(); ++i)
            if (languages_[i].code == c.arg) row = static_cast<int>(i);
        pick(Step::Language, row);
    } else if (c.verb == "renderer") {
        pick(Step::Renderer, c.arg == "vulkan" ? 1 : c.arg == "gl" ? 0 : -1);
    } else if (c.verb == "preset") {
        int row = -1;
        for (std::size_t i = 0; i < request_.presets.size(); ++i)
            if (request_.presets[i].name == c.arg) row = static_cast<int>(i);
        pick(Step::Preset, row);
    } else if (c.verb == "resolution") {
        go_to(Step::Display);
        int w = 0, h = 0;
        const bool parsed = std::sscanf(c.arg.c_str(), "%dx%d", &w, &h) == 2;
        const auto it = std::find(resolutions_.begin(), resolutions_.end(), Resolution{w, h});
        if (!parsed || it == resolutions_.end()) return fail("not among the display's sizes");
        resolution_ = static_cast<std::size_t>(it - resolutions_.begin());
    } else if (c.verb == "mode") {
        go_to(Step::Display);
        int index = -1;
        for (std::size_t i = 0; i < kModeKeys.size(); ++i)
            if (std::string(kModeKeys[i]).ends_with("." + c.arg)) index = static_cast<int>(i);
        if (index < 0) return fail("windowed, borderless or fullscreen");
        window_mode_ = index;
        focus_ = 1;
    } else if (c.verb == "step") {
        const int n = std::atoi(c.arg.c_str());
        if (n < 1 || n > kStepCount) return fail("1 to 4");
        go_to(static_cast<Step>(n - 1));
    } else if (c.verb == "key") {
        if (c.arg == "up")
            move_focus(-1);
        else if (c.arg == "down")
            move_focus(1);
        else if (c.arg == "tab")
            move_focus(1);
        else if (c.arg == "left")
            step_value(rtl_ ? 1 : -1);
        else if (c.arg == "right")
            step_value(rtl_ ? -1 : 1);
        else if (c.arg == "enter")
            activate(done, outcome);
        else if (c.arg == "back")
            back(done, outcome);
        else
            fail("unknown key");
    } else if (c.verb == "size") {
        int w = 0, h = 0;
        if (std::sscanf(c.arg.c_str(), "%dx%d", &w, &h) != 2 || w < 320 || h < 240) return fail("<w>x<h>");
        SDL_SetWindowSize(window_, w, h);
        SDL_SyncWindow(window_);
    } else if (c.verb == "shot") {
        save_shot(c.arg);
    } else if (c.verb == "apply") {
        done = true;
        outcome = Outcome::Applied;
    } else if (c.verb == "skip") {
        done = true;
        outcome = Outcome::Skipped;
    } else if (c.verb == "quit") {
        done = true;
        outcome = Outcome::Quit;
    } else {
        fail("unknown command");
    }
}

bool interactive_video() { return SDL_GetCurrentVideoDriver() != nullptr && !sh::desktop::headless_video(); }

}  // namespace

Answer run(const Request& request) {
    Answer result;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return result;
    if (request.script.empty() && !interactive_video()) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return result;
    }
    const bool pads = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    if (pads && !request.controller_db.empty()) SDL_AddGamepadMappingsFromFile(request.controller_db.string().c_str());
    result.hardware = probe_hardware();
    {
        Screen screen(request, result.hardware);
        result.outcome = screen.run(result.choices);
    }
    if (pads) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return result;
}

}  // namespace sh::quick_setup
