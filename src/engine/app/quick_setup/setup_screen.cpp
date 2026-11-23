// SPDX-License-Identifier: GPL-3.0-or-later
// The quick setup's window: four steps (language, renderer, graphics preset, display) drawn in the look of the
// game's menus (bullets, the gold underline, value arrows and the dark edge bands of gamedata/menu/menu_style.json)
// on a 640x480 canvas scaled to the window, text rasterised at the window's own size. Keyboard, mouse and game
// controllers all drive it.
#include "engine/app/quick_setup/hardware_probe.h"
#include "engine/app/quick_setup/quick_setup.h"
#include "engine/app/quick_setup/setup_text.h"
#include "engine/app/quick_setup/text_raster.h"

#include "engine/desktop/window_icon.h"
#include "formats/common/json.h"
#include "i18n/core/language_set.h"
#include "i18n/core/unicode.h"

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

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
};

// The widget colours of gamedata/menu/menu_style.json; these values stand in when the file cannot be read.
struct Style {
    std::array<Rgb, 2> underline{{{176, 176, 176}, {160, 128, 64}}};
    std::array<Rgb, 3> bullet_front{{{160, 128, 64}, {255, 255, 255}, {255, 255, 255}}};
    std::array<Rgb, 3> bullet_front_inactive{{{80, 64, 32}, {160, 160, 160}, {160, 160, 160}}};
    std::array<Rgb, 3> bullet_border{{{255, 255, 255}, {160, 128, 64}, {64, 64, 64}}};
    std::array<Rgb, 3> bullet_border_inactive{{{128, 128, 128}, {40, 32, 16}, {16, 16, 16}}};
    Rgb arrow{0, 240, 240};
    Rgb vignette_edge{96, 96, 96};
};

bool read_rgb(const sh::json::Value* v, Rgb& out) {
    if (v == nullptr || !v->is_array() || v->array().size() != 3) return false;
    int c[3];
    for (int i = 0; i < 3; ++i) {
        const sh::json::Value& n = v->array()[static_cast<std::size_t>(i)];
        if (!n.is_number()) return false;
        c[i] = std::clamp(static_cast<int>(n.number()), 0, 255);
    }
    out = {static_cast<std::uint8_t>(c[0]), static_cast<std::uint8_t>(c[1]), static_cast<std::uint8_t>(c[2])};
    return true;
}

template <std::size_t N>
void read_rgbs(const sh::json::Value* v, std::array<Rgb, N>& out) {
    if (v == nullptr || !v->is_array() || v->array().size() != N) return;
    std::array<Rgb, N> got;
    for (std::size_t i = 0; i < N; ++i)
        if (!read_rgb(&v->array()[i], got[i])) return;
    out = got;
}

Style load_style(const fs::path& file) {
    Style s;
    std::ifstream in(file, std::ios::binary);
    if (!in) return s;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto doc = sh::json::parse(text);
    if (!doc) return s;
    const sh::json::Value& root = doc.value();
    if (const auto* u = root.find("underline")) read_rgbs(u->find("colors"), s.underline);
    if (const auto* b = root.find("bullet")) {
        read_rgbs(b->find("front"), s.bullet_front);
        read_rgbs(b->find("front_inactive"), s.bullet_front_inactive);
        read_rgbs(b->find("border"), s.bullet_border);
        read_rgbs(b->find("border_inactive"), s.bullet_border_inactive);
    }
    if (const auto* a = root.find("arrow")) read_rgb(a->find("solid"), s.arrow);
    if (const auto* v = root.find("vignette")) read_rgb(v->find("edge_color"), s.vignette_edge);
    return s;
}

enum class Step { Language, Renderer, Preset, Display };
constexpr int kStepCount = 4;

enum class Align { Left, Center, Right };

struct Row {
    std::string label;
    std::string detail;  // a second, smaller line
    std::string note;    // right column: "recommended" or why the row cannot be chosen
    bool note_gold = false;
    bool enabled = true;
    // The label's own language (the language list names each language in itself).
    std::vector<std::string> families;
    bool rtl = false;
};

struct Texture {
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
};

struct Rect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool contains(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
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

    // Canvas to pixels.
    float sx(float x) const { return ox_ + (rtl_ ? kCanvasW - x : x) * scale_; }
    float sy(float y) const { return oy_ + y * scale_; }
    Align flip(Align a) const {
        if (!rtl_ || a == Align::Center) return a;
        return a == Align::Left ? Align::Right : Align::Left;
    }
    // The pixel box of a canvas box (mirrored in a right-to-left language).
    Rect box(float x0, float y0, float x1, float y1) const {
        const float a = sx(x0), b = sx(x1);
        return {std::min(a, b), sy(y0), std::max(a, b), sy(y1)};
    }

    const Texture& text(const std::string& s, float size, const std::vector<std::string>& families, bool rtl);
    // Draws a line centred on canvas y; returns its width in canvas units. `max_w` shrinks a line too wide for it.
    float draw_text(const std::string& s, float x, float y, float size, Rgb color, Align align, float max_w = 0,
                    const Row* own = nullptr, bool shadow = true, std::uint8_t alpha = 255);
    std::vector<std::string> wrap(const std::string& s, float size, float max_w);
    void clear_text();

    void fill_gradient(Rect r, Rgb top, std::uint8_t top_a, Rgb bottom, std::uint8_t bottom_a, bool horizontal);
    void draw_bullet(float x, float y, bool active);
    void draw_arrow(float x, float y, bool right, float size);
    void draw_underline(float x0, float x1, float y);

    int row_count() const { return static_cast<int>(rows_.size()); }
    int focus_back() const { return row_count(); }
    int focus_next() const { return row_count() + 1; }
    float row_pitch() const;
    float row_y(int i) const { return kListTop + row_pitch() * (static_cast<float>(i) + 0.5f); }
    int chosen_row() const;

    const Request& request_;
    Hardware hw_;
    Style style_;
    Strings strings_;
    TextRaster raster_;
    std::vector<sh::i18n::Language> languages_;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::vector<SDL_Gamepad*> pads_;
    std::unordered_map<std::string, Texture> textures_;
    float scale_ = 1.0f, ox_ = 0.0f, oy_ = 0.0f;
    int pixel_w_ = 0, pixel_h_ = 0;

    Step step_ = Step::Language;
    std::vector<Row> rows_;
    int focus_ = 0;
    bool rtl_ = false;
    std::vector<std::string> families_;  // the interface language's fonts

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
    : request_(request), hw_(std::move(hw)), raster_(request.base_data / "font") {
    style_ = load_style(request.base_data / "menu" / "menu_style.json");
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
    clear_text();
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
    window_ = SDL_CreateWindow("Silent Hill", w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
    // Text input would hand the keys to an input method of the system.
    SDL_StopTextInput(window_);
    return true;
}

void Screen::clear_text() {
    for (auto& [key, t] : textures_)
        if (t.texture) SDL_DestroyTexture(t.texture);
    textures_.clear();
}

void Screen::set_language(std::size_t index) {
    language_ = index;
    const sh::i18n::Language& lang = languages_[index];
    strings_.load(request_.base_data / "text", lang.code);
    rtl_ = lang.direction == sh::i18n::Direction::Rtl;
    families_ = {"Barlow Semi Condensed"};
    families_.insert(families_.end(), lang.fonts.begin(), lang.fonts.end());
    if (window_) SDL_SetWindowTitle(window_, ("Silent Hill - " + strings_.get("ui.setup.title")).c_str());
    clear_text();
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
                row.families = {"Barlow Semi Condensed"};
                row.families.insert(row.families.end(), l.fonts.begin(), l.fonts.end());
                row.rtl = l.direction == sh::i18n::Direction::Rtl;
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

const Texture& Screen::text(const std::string& s, float size, const std::vector<std::string>& families, bool rtl) {
    const float px = std::max(6.0f, std::round(size * scale_));
    std::string key = s;
    key += '\x1f';
    key += std::to_string(static_cast<int>(px));
    key += rtl ? 'r' : 'l';
    for (const auto& f : families) key += '\x1f' + f;
    auto it = textures_.find(key);
    if (it != textures_.end()) return it->second;
    Texture t;
    const TextBitmap bitmap = raster_.render(s, px, families, rtl);
    if (SDL_Surface* surface = SDL_CreateSurface(bitmap.width, bitmap.height, SDL_PIXELFORMAT_RGBA32)) {
        for (int y = 0; y < bitmap.height; ++y) {
            auto* row = static_cast<std::uint8_t*>(surface->pixels) + static_cast<std::size_t>(y) * surface->pitch;
            for (int x = 0; x < bitmap.width; ++x) {
                std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
                p[0] = p[1] = p[2] = 255;
                p[3] = bitmap.alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap.width) +
                                    static_cast<std::size_t>(x)];
            }
        }
        t.texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_DestroySurface(surface);
        if (t.texture) {
            SDL_SetTextureBlendMode(t.texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t.texture, SDL_SCALEMODE_LINEAR);
        }
        t.width = bitmap.width;
        t.height = bitmap.height;
    }
    return textures_.emplace(std::move(key), t).first->second;
}

float Screen::draw_text(const std::string& s, float x, float y, float size, Rgb color, Align align, float max_w,
                        const Row* own, bool shadow, std::uint8_t alpha) {
    if (s.empty()) return 0.0f;
    const bool own_language = own != nullptr && !own->families.empty();
    const std::vector<std::string>& families = own_language ? own->families : families_;
    const bool rtl = own_language ? own->rtl : rtl_;
    const Texture* t = &text(s, size, families, rtl);
    float w = static_cast<float>(t->width);
    if (max_w > 0.0f && w > max_w * scale_) {
        // Too wide for its place: drawn smaller rather than cut.
        const float smaller = std::max(size * 0.6f, size * max_w * scale_ / w);
        t = &text(s, smaller, families, rtl);
        w = static_cast<float>(t->width);
    }
    if (t->texture == nullptr) return 0.0f;
    const float h = static_cast<float>(t->height);
    float left = sx(x);
    switch (flip(align)) {
        case Align::Left:
            break;
        case Align::Center:
            left -= w * 0.5f;
            break;
        case Align::Right:
            left -= w;
            break;
    }
    SDL_FRect dst{std::round(left), std::round(sy(y) - h * 0.5f), w, h};
    if (shadow) {
        const float d = std::max(1.0f, std::round(scale_));
        SDL_FRect sh_dst{dst.x + d, dst.y + d, w, h};
        SDL_SetTextureColorMod(t->texture, 0, 0, 0);
        SDL_SetTextureAlphaMod(t->texture, static_cast<std::uint8_t>(alpha * 3 / 4));
        SDL_RenderTexture(renderer_, t->texture, nullptr, &sh_dst);
    }
    SDL_SetTextureColorMod(t->texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(t->texture, alpha);
    SDL_RenderTexture(renderer_, t->texture, nullptr, &dst);
    return w / scale_;
}

std::vector<std::string> Screen::wrap(const std::string& s, float size, float max_w) {
    // Words, and single characters of the scripts written without spaces.
    std::vector<std::string> tokens;
    std::string current;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const std::size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        const std::string ch = s.substr(i, n);
        const std::u32string cp = decode_utf8(ch);
        i += n;
        if (ch == " ") {
            current += ch;
            tokens.push_back(std::move(current));
            current.clear();
        } else if (!cp.empty() && sh::i18n::breaks_anywhere(cp[0])) {
            if (!current.empty()) tokens.push_back(std::move(current));
            current = ch;
            tokens.push_back(std::move(current));
            current.clear();
        } else {
            current += ch;
        }
    }
    if (!current.empty()) tokens.push_back(std::move(current));
    std::vector<std::string> lines;
    std::string line;
    for (const std::string& token : tokens) {
        const std::string candidate = line + token;
        std::string trimmed = candidate;
        while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
        // A closing mark or full stop of CJK text never starts a line: it stays with the one before.
        const std::u32string first = decode_utf8(token.substr(0, 4));
        const bool sticks = !first.empty() && sh::i18n::no_line_start(first[0]);
        if (!line.empty() && !sticks &&
            static_cast<float>(text(trimmed, size, families_, rtl_).width) > max_w * scale_) {
            while (!line.empty() && line.back() == ' ') line.pop_back();
            lines.push_back(line);
            line = token;
        } else {
            line = candidate;
        }
    }
    while (!line.empty() && line.back() == ' ') line.pop_back();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

void Screen::fill_gradient(Rect r, Rgb a, std::uint8_t a_alpha, Rgb b, std::uint8_t b_alpha, bool horizontal) {
    const SDL_FColor ca{a.r / 255.0f, a.g / 255.0f, a.b / 255.0f, a_alpha / 255.0f};
    const SDL_FColor cb{b.r / 255.0f, b.g / 255.0f, b.b / 255.0f, b_alpha / 255.0f};
    SDL_Vertex v[4];
    v[0] = {{r.x0, r.y0}, ca, {0, 0}};
    v[1] = {{r.x1, r.y0}, horizontal ? cb : ca, {0, 0}};
    v[2] = {{r.x0, r.y1}, horizontal ? ca : cb, {0, 0}};
    v[3] = {{r.x1, r.y1}, cb, {0, 0}};
    const int idx[6] = {0, 1, 2, 1, 3, 2};
    SDL_RenderGeometry(renderer_, nullptr, v, 4, idx, 6);
}

SDL_FColor fcolor(Rgb c) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f}; }

void Screen::draw_bullet(float x, float y, bool active) {
    // The menus' bullet: a square split along a diagonal, the border a size larger behind the face.
    const auto& border = active ? style_.bullet_border : style_.bullet_border_inactive;
    const auto& front = active ? style_.bullet_front : style_.bullet_front_inactive;
    const auto quad = [&](float half, const std::array<Rgb, 3>& c) {
        const float cx = sx(x), cy = sy(y), h = half * scale_;
        SDL_Vertex v[4];
        v[0] = {{cx - h, cy - h}, fcolor(c[0]), {0, 0}};
        v[1] = {{cx + h, cy - h}, fcolor(c[1]), {0, 0}};
        v[2] = {{cx - h, cy + h}, fcolor(c[1]), {0, 0}};
        v[3] = {{cx + h, cy + h}, fcolor(c[2]), {0, 0}};
        const int idx[6] = {0, 1, 2, 1, 3, 2};
        SDL_RenderGeometry(renderer_, nullptr, v, 4, idx, 6);
    };
    quad(4.0f, border);
    quad(2.8f, front);
}

void Screen::draw_arrow(float x, float y, bool right, float size) {
    if (rtl_) right = !right;
    const float cx = sx(x), cy = sy(y), h = size * scale_;
    const float dir = right ? 1.0f : -1.0f;
    const SDL_FColor c = fcolor(style_.arrow);
    SDL_Vertex v[3];
    v[0] = {{cx + dir * h, cy}, c, {0, 0}};
    v[1] = {{cx - dir * h * 0.6f, cy - h}, c, {0, 0}};
    v[2] = {{cx - dir * h * 0.6f, cy + h}, c, {0, 0}};
    SDL_RenderGeometry(renderer_, nullptr, v, 3, nullptr, 0);
}

void Screen::draw_underline(float x0, float x1, float y) {
    // The selected row's band and the two-colour underline of the original options.
    fill_gradient(box(x0 - 6.0f, y - 10.0f, x1, y + 1.0f), {0, 0, 0}, 0, style_.vignette_edge, 110, false);
    Rect line = box(x0 - 6.0f, y, x1, y + 1.4f);
    if (rtl_) {
        fill_gradient(line, style_.underline[1], 255, style_.underline[0], 255, true);
    } else {
        fill_gradient(line, style_.underline[0], 255, style_.underline[1], 255, true);
    }
}

void Screen::draw() {
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(renderer_, &w, &h);
    if (w != pixel_w_ || h != pixel_h_) {
        pixel_w_ = w;
        pixel_h_ = h;
        scale_ = std::min(static_cast<float>(w) / kCanvasW, static_cast<float>(h) / kCanvasH);
        ox_ = (static_cast<float>(w) - kCanvasW * scale_) * 0.5f;
        oy_ = (static_cast<float>(h) - kCanvasH * scale_) * 0.5f;
        clear_text();
    }
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    // The dark bands at the top and the bottom of the menus.
    const float band = static_cast<float>(h) * 0.14f;
    fill_gradient({0, 0, static_cast<float>(w), band}, style_.vignette_edge, 140, {0, 0, 0}, 0, false);
    fill_gradient({0, static_cast<float>(h) - band, static_cast<float>(w), static_cast<float>(h)}, {0, 0, 0}, 0,
                  style_.vignette_edge, 140, false);

    const Rgb white{232, 232, 232}, gold{200, 168, 96}, grey{150, 150, 150}, dark{90, 90, 90};
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
        if (step_ != Step::Display) draw_bullet(kBulletX, label_y, is_chosen && row.enabled);
        const float label_w =
            draw_text(row.label, kLabelX, label_y, size, color, Align::Left, step_ == Step::Display ? 220 : 300, &row);
        if (i == focus_) draw_underline(kLabelX, kLabelX + std::max(label_w, 120.0f) + 6.0f, label_y + size * 0.62f);
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
            draw_arrow(kValueX - kValueHalf, y, false, 5);
            draw_arrow(kValueX + kValueHalf, y, true, 5);
        }
    }

    // The step's help, wrapped over the width.
    static constexpr std::array<const char*, kStepCount> kHelpKeys = {
        "ui.setup.language.help", "ui.setup.renderer.help", "ui.setup.preset.help", "ui.setup.display.help"};
    const std::vector<std::string> help = wrap(strings_.get(kHelpKeys[static_cast<std::size_t>(step)]), 12, 540);
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
            draw_underline(x - bw * 0.5f - 4.0f, x + bw * 0.5f + 4.0f, kButtonY + 11.0f);
            draw_arrow(x - bw * 0.5f - 16.0f, kButtonY, true, 4);
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

bool interactive_video() {
    const char* driver = SDL_GetCurrentVideoDriver();
    return driver && SDL_strcmp(driver, "offscreen") != 0 && SDL_strcmp(driver, "dummy") != 0;
}

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
