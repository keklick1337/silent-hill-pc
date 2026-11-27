#include "engine/app/first_run/progress_window.h"

#include "engine/app/first_run/picked_document.h"
#include "engine/app/screen_kit/line_edit.h"
#include "engine/app/screen_kit/painter.h"
#include "engine/app/screen_kit/script.h"
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
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sh::first_run {

namespace {

namespace fs = std::filesystem;
namespace iu = sh::import::ui;
namespace kit = sh::screen_kit;
using kit::Align;
using kit::Rect;
using kit::Rgb;

// The canvases the layout is written for, a wide and a tall one; a larger window scales them up, a phone's screen
// keeps them finger sized.
constexpr kit::Size kDesigns[] = {{720.0f, 460.0f}, {420.0f, 700.0f}};
constexpr float kMaxColumn = 640.0f;
constexpr float kButtonH = 44.0f;
constexpr float kFieldH = 42.0f;
constexpr float kGap = 12.0f;

constexpr Rgb kWhite = kit::colors::kWhite;
constexpr Rgb kGold = kit::colors::kGold;
constexpr Rgb kGrey = kit::colors::kGrey;
constexpr Rgb kDark = kit::colors::kDark;
constexpr Rgb kBody{196, 196, 196};
constexpr Rgb kRed{232, 112, 96};

// The importer's stages, then what the first start derives from the import (first_run.cpp).
constexpr const char* kStages[] = {"read", "raw", "convert", "verify", "commit", "prepare"};
constexpr iu::Str kStageNames[] = {iu::Str::StageRead,   iu::Str::StageRaw,    iu::Str::StageConvert,
                                   iu::Str::StageVerify, iu::Str::StageCommit, iu::Str::StagePrepare};
constexpr int kStageCount = static_cast<int>(std::size(kStages));

// What a click, a tap or the keyboard's focus can be on.
enum class Target { None, Field, Browse, Import, Quit, Cancel };

struct Hit {
    Rect box;
    Target target = Target::None;
    bool enabled = true;
};

// The file dialog answers from another thread on some systems, and possibly after the picker has gone: the state is
// shared with the callback, which owns one reference.
struct DialogState {
    std::mutex mutex;
    bool pending = false;
    bool answered = false;
    std::string path;   // empty on cancel
    std::string error;  // set when the dialog could not be shown
    std::string filter_name;
    SDL_DialogFileFilter filter{};
};

[[maybe_unused]] void SDLCALL on_dialog(void* userdata, const char* const* files, int) {
    std::unique_ptr<std::shared_ptr<DialogState>> holder(static_cast<std::shared_ptr<DialogState>*>(userdata));
    DialogState& d = **holder;
    std::lock_guard<std::mutex> lock(d.mutex);
    d.pending = false;
    d.answered = true;
    d.path.clear();
    d.error.clear();
    if (!files) {
        d.error = SDL_GetError();
        if (d.error.empty()) d.error = "?";
    } else if (files[0]) {
        d.path = files[0];
    }
}

bool interactive() { return SDL_GetCurrentVideoDriver() != nullptr && !sh::desktop::headless_video(); }

bool touch_screen() {
#ifdef __ANDROID__
    return true;
#else
    return false;
#endif
}

}  // namespace

class ProgressWindow::Screen {
public:
    explicit Screen(WindowOptions options);
    ~Screen();

    bool open();
    void close();
    bool can_pick() const { return renderer_ != nullptr && (interactive() || scripted_); }
    void pick(iu::ImagePicker& picker, iu::Lang lang, const std::string& gamedata_dir);
    void draw(const iu::ViewModel& v);
    bool stop_requested();
    void set_subject(std::string subject) { subject_ = std::move(subject); }

private:
    bool create_window();
    void destroy_window();
    void set_language(const std::string& code);
    void load_script(const fs::path& file);

    // Frame
    void begin_frame();
    void end_frame() { SDL_RenderPresent(renderer_); }
    void save_shot(const std::string& file);
    float column_x0() const;
    float column_x1() const;

    // The picker
    void draw_picker();
    float picker_body(float y, bool paint);
    void draw_field(float x0, float x1, float y);
    void handle_picker(const SDL_Event& e);
    void activate(Target t);
    void open_dialog();
    void submit();
    void run_picker_command(const kit::ScriptCommand& c);
    void move_focus(int dir);
    void step_focus(int dir);
    void set_focus(Target t);
    void place_caret(float pixel_x);
    float caret_px(std::size_t byte);

    // The progress
    void draw_progress(const iu::ViewModel& v);
    float progress_body(const iu::ViewModel& v, float y, bool paint);
    void handle_progress(const SDL_Event& e);
    bool run_progress_command(const kit::ScriptCommand& c, const iu::ViewModel& v);

    // Shared
    void pointer_event(const SDL_Event& e, bool picker);
    Target target_at(float x, float y) const;
    void scrolled_body(float body_top, float body_bottom, const std::function<float(float, bool)>& body);
    void footer(const std::vector<std::pair<Target, std::string>>& buttons, const std::vector<bool>& enabled);
    void button(Rect box, const std::string& label, Target t, bool enabled, bool primary);
    void paragraph(const std::string& text, float& y, float size, float line, Rgb color, bool paint,
                   Align align = Align::Left);
    void heading(const std::string& title, const std::string& subtitle, float& y, bool paint);
    void text_input(bool on);
    void gamepad_added(SDL_JoystickID id);

    WindowOptions options_;
    kit::Painter paint_;
    kit::TextFace path_face_;  // a path reads left to right in every language
    bool rtl_ = false;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool video_ = false;
    bool pads_ready_ = false;
    std::vector<SDL_Gamepad*> pads_;
    int pixel_w_ = 0, pixel_h_ = 0;
    float density_ = 1.0f;

    std::vector<Hit> hits_;
    std::vector<Target> stops_;  // the focus order of the screen on show
    Target focus_ = Target::None;
    Target hover_ = Target::None;
    Target pressed_ = Target::None;
    float scroll_ = 0.0f;
    float focus_top_ = -1.0f, focus_bottom_ = -1.0f;  // the field row in the body, from its top
    bool reveal_focus_ = false;
    float content_h_ = 0.0f, viewport_h_ = 0.0f;
    Rect body_px_;

    SDL_FingerID finger_ = 0;
    bool finger_down_ = false, dragging_ = false;
    float down_x_ = 0, down_y_ = 0, scroll_at_down_ = 0;
    float stick_y_ = 0.0f, stick_x_ = 0.0f;

    // The picker's state while pick() runs.
    iu::ImagePicker* picker_ = nullptr;
    iu::Lang lang_ = iu::Lang::En;
    std::string gamedata_dir_;
    kit::LineEdit edit_;
    std::string synced_;         // the text the picker and the field last agreed on
    float field_scroll_ = 0.0f;  // pixels of the path hidden on the left
    Rect field_px_;
    bool text_input_ = false;
    bool drop_hover_ = false;
    std::shared_ptr<DialogState> dialog_;
    std::uint64_t blink_from_ = 0;

    bool stop_ = false;
    std::string title_;
    std::string subject_;  // what is imported, under the progress screen's title
    std::vector<kit::ScriptCommand> script_;
    std::size_t script_at_ = 0;
    bool scripted_ = false;
};

ProgressWindow::Screen::Screen(WindowOptions options) : options_(std::move(options)), paint_(options_.base_data) {
    set_language(options_.language);
    if (!options_.script.empty()) load_script(options_.script);
}

ProgressWindow::Screen::~Screen() { close(); }

void ProgressWindow::Screen::set_language(const std::string& code) {
    std::vector<std::string> warnings;
    const std::vector<fs::path> dirs{options_.base_data / "text"};
    std::vector<std::string> fonts;
    for (const sh::i18n::Language& l : sh::i18n::discover_languages(dirs, warnings)) {
        if (l.code == code) {
            fonts = l.fonts;
            rtl_ = l.direction == sh::i18n::Direction::Rtl;
        }
    }
    paint_.set_face(kit::face_for(fonts, rtl_));
    path_face_ = kit::face_for(fonts, false);
}

void ProgressWindow::Screen::load_script(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) std::fprintf(stderr, "import screen script: cannot read %s\n", file.string().c_str());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    script_ = kit::parse_script(text);
    scripted_ = true;
}

bool ProgressWindow::Screen::open() {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return false;
    video_ = true;
    if (!create_window()) {
        close();
        return false;
    }
    // The base data's fonts are read now: an import moves the base entries of gamedata/ aside while it runs, and a
    // font read later would be gone then (a screen with no text).
    paint_.width("Aa", 14.0f);
    paint_.width("Aa", 14.0f, &path_face_);
    pads_ready_ = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    if (pads_ready_ && !options_.controller_db.empty())
        SDL_AddGamepadMappingsFromFile(options_.controller_db.string().c_str());
    return true;
}

bool ProgressWindow::Screen::create_window() {
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                            static_cast<SDL_WindowFlags>(sh::desktop::hidden_window_flag());
#ifdef __ANDROID__
    flags |= SDL_WINDOW_FULLSCREEN;  // a phone has no windows: the whole screen, without the system bars over it
#endif
    // SDL3 centres a new window by default.
    window_ = SDL_CreateWindow("Silent Hill", 780, 500, flags);
    if (window_ == nullptr) return false;
    SDL_SetWindowMinimumSize(window_, 360, 320);
    sh::desktop::set_window_icon(window_, options_.icons);
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
    // Text input hands the keys to the system's input method (and on a phone shows the keyboard): only while the
    // path field has the focus.
    SDL_StopTextInput(window_);
    text_input_ = false;
    title_.clear();
    return true;
}

void ProgressWindow::Screen::destroy_window() {
    paint_.attach(nullptr);
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

void ProgressWindow::Screen::close() {
    for (SDL_Gamepad* pad : pads_) SDL_CloseGamepad(pad);
    pads_.clear();
    destroy_window();
    if (pads_ready_) {
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        pads_ready_ = false;
    }
    if (video_) {
        // The game window's start (sh::platform::start) brings video up again.
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        video_ = false;
    }
}

void ProgressWindow::Screen::gamepad_added(SDL_JoystickID id) {
    if (SDL_Gamepad* pad = SDL_OpenGamepad(id)) pads_.push_back(pad);
}

void ProgressWindow::Screen::text_input(bool on) {
    if (on == text_input_ || window_ == nullptr) return;
    text_input_ = on;
    if (on) {
        SDL_StartTextInput(window_);
    } else {
        SDL_StopTextInput(window_);
    }
}

// ---- frame ----

void ProgressWindow::Screen::begin_frame() {
    SDL_GetRenderOutputSize(renderer_, &pixel_w_, &pixel_h_);
    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(window_, &win_w, &win_h);
    // Pixels per window unit (a high-density display), times the display's own scale.
    const float pixel_density = win_w > 0 ? static_cast<float>(pixel_w_) / static_cast<float>(win_w) : 1.0f;
    const float content = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window_));
    density_ = std::max(1.0f, pixel_density * (content > 0.0f ? content : 1.0f));
    kit::Insets insets;
    SDL_Rect safe{};
    if (SDL_GetWindowSafeArea(window_, &safe) && safe.w > 0 && safe.h > 0) {
        insets.left = static_cast<float>(safe.x) * pixel_density;
        insets.top = static_cast<float>(safe.y) * pixel_density;
        insets.right = static_cast<float>(win_w - safe.x - safe.w) * pixel_density;
        insets.bottom = static_cast<float>(win_h - safe.y - safe.h) * pixel_density;
    }
    // A finger needs nearly the display's full density (a button stays about 38 dp tall); a desktop window may
    // shrink the canvas a little more.
    const float min_scale = touch_screen() ? density_ * 0.85f : density_ * 0.75f;
    const kit::View view = kit::fit_view(pixel_w_, pixel_h_, insets, kDesigns, min_scale, density_ * 2.2f);
    if (view.scale != paint_.scale() || view.width != paint_.view().width) {
        std::fprintf(stderr, "first run: the import screen is %dx%d pixels, %.2f a unit, insets %.0f %.0f %.0f %.0f\n",
                     pixel_w_, pixel_h_, view.scale, insets.left, insets.top, insets.right, insets.bottom);
    }
    paint_.set_view(view, rtl_);

    SDL_SetRenderClipRect(renderer_, nullptr);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    paint_.edge_bands(pixel_w_, pixel_h_);
    hits_.clear();
}

float ProgressWindow::Screen::column_x0() const {
    const float w = paint_.view().width;
    const float side = w < 520.0f ? 16.0f : 28.0f;
    const float col = std::min(w - 2.0f * side, kMaxColumn);
    return (w - col) * 0.5f;
}

float ProgressWindow::Screen::column_x1() const { return paint_.view().width - column_x0(); }

void ProgressWindow::Screen::save_shot(const std::string& file) {
    SDL_Surface* shot = SDL_RenderReadPixels(renderer_, nullptr);
    if (shot == nullptr || !SDL_SaveBMP(shot, file.c_str()))
        std::fprintf(stderr, "import screen script: shot %s failed: %s\n", file.c_str(), SDL_GetError());
    if (shot) SDL_DestroySurface(shot);
}

void ProgressWindow::Screen::paragraph(const std::string& text, float& y, float size, float line, Rgb color, bool paint,
                                       Align align) {
    if (text.empty()) return;
    const float x0 = column_x0(), x1 = column_x1();
    for (const std::string& l : paint_.wrap(text, size, x1 - x0)) {
        if (paint) {
            const float x = align == Align::Center ? (x0 + x1) * 0.5f : align == Align::Right ? x1 : x0;
            paint_.draw_text(l, x, y + line * 0.5f, size, color, align);
        }
        y += line;
    }
}

void ProgressWindow::Screen::heading(const std::string& title, const std::string& subtitle, float& y, bool paint) {
    paragraph(title, y, 24.0f, 32.0f, kWhite, paint, Align::Center);
    y += 2.0f;
    paragraph(subtitle, y, 15.0f, 21.0f, kGold, paint, Align::Center);
    y += 10.0f;
    if (paint) {
        // A rule that fades out towards both ends (the same both ways, so a mirrored canvas changes nothing).
        const Rect rule = paint_.box(column_x0(), y, column_x1(), y + 1.0f);
        const float mid = (rule.x0 + rule.x1) * 0.5f;
        const Rgb edge = paint_.style().underline[1];
        paint_.fill_gradient({rule.x0, rule.y0, mid, rule.y1}, edge, 0, edge, 200, true);
        paint_.fill_gradient({mid, rule.y0, rule.x1, rule.y1}, edge, 200, edge, 0, true);
    }
    y += 16.0f;
}

void ProgressWindow::Screen::button(Rect box, const std::string& label, Target t, bool enabled, bool primary) {
    const bool focused = enabled && focus_ == t;
    const bool hot = enabled && (hover_ == t || pressed_ == t);
    const float s = paint_.scale();
    Rgb top = primary ? Rgb{74, 60, 32} : Rgb{34, 34, 38};
    Rgb bottom = primary ? Rgb{44, 35, 18} : Rgb{20, 20, 23};
    if (hot) {
        top = primary ? Rgb{96, 78, 42} : Rgb{48, 48, 54};
        bottom = primary ? Rgb{58, 46, 24} : Rgb{28, 28, 32};
    }
    if (pressed_ == t) std::swap(top, bottom);
    paint_.fill_gradient(box, top, enabled ? 235 : 120, bottom, enabled ? 235 : 120, false);
    const Rgb border = !enabled ? Rgb{50, 50, 54} : focused ? kGold : primary ? Rgb{140, 112, 56} : Rgb{78, 78, 86};
    paint_.outline(box, focused ? std::max(1.0f, s * 1.5f) : std::max(1.0f, s), border);
    const Rgb text = !enabled ? kDark : focused || hot ? Rgb{255, 255, 255} : primary ? Rgb{236, 214, 160} : kBody;
    const kit::Texture& t_line = paint_.text(label, 15.0f, paint_.face());
    float w = static_cast<float>(t_line.width);
    const kit::Texture* tex = &t_line;
    if (w > box.width() - 8.0f * s) {
        tex = &paint_.text(label, std::max(9.0f, 15.0f * (box.width() - 8.0f * s) / w), paint_.face());
        w = static_cast<float>(tex->width);
    }
    paint_.blit(*tex, (box.x0 + box.x1 - w) * 0.5f, (box.y0 + box.y1 - static_cast<float>(tex->height)) * 0.5f, text,
                255, false);
    hits_.push_back({box, t, enabled});
}

void ProgressWindow::Screen::footer(const std::vector<std::pair<Target, std::string>>& buttons,
                                    const std::vector<bool>& enabled) {
    const float x0 = column_x0(), x1 = column_x1();
    std::vector<float> widths;
    for (const auto& [t, label] : buttons) widths.push_back(paint_.width(label, 15.0f) + 40.0f);
    const auto slots = kit::lay_out_buttons(widths, x0, x1, kGap, 150.0f);
    int rows = 0;
    for (const auto& s : slots) rows = std::max(rows, s.row + 1);
    const float h = static_cast<float>(rows) * kButtonH + static_cast<float>(rows - 1) * 10.0f;
    const float bottom = paint_.view().height - 16.0f;
    const float top = bottom - h;
    body_px_.y1 = paint_.sy(top - 14.0f);
    // A faint rule over the buttons, where the body scrolls under them.
    paint_.fill(paint_.box(x0, top - 14.0f, x1, top - 13.0f), {60, 60, 66}, content_h_ > viewport_h_ ? 255 : 0);
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const float y = top + static_cast<float>(slots[i].row) * (kButtonH + 10.0f);
        button(paint_.box(slots[i].x0, y, slots[i].x1, y + kButtonH), buttons[i].second, buttons[i].first, enabled[i],
               i == 0);
    }
}

void ProgressWindow::Screen::scrolled_body(float body_top, float body_bottom,
                                           const std::function<float(float, bool)>& body) {
    viewport_h_ = std::max(1.0f, body_bottom - body_top);
    focus_top_ = focus_bottom_ = -1.0f;
    content_h_ = body(0.0f, false);
    if (reveal_focus_ && focus_top_ >= 0.0f && (focus_ == Target::Field || focus_ == Target::Browse)) {
        // The keyboard's or the controller's focus scrolled into view (the field row, with a margin).
        scroll_ = std::min(scroll_, focus_top_ - 12.0f);
        scroll_ = std::max(scroll_, focus_bottom_ + 12.0f - viewport_h_);
    }
    reveal_focus_ = false;
    scroll_ = kit::clamp_scroll(scroll_, content_h_, viewport_h_);
    // Content that fits sits a little above the middle; taller content scrolls.
    const float y0 = content_h_ <= viewport_h_ ? body_top + (viewport_h_ - content_h_) * 0.4f : body_top - scroll_;
    body_px_ = {0.0f, paint_.sy(body_top), static_cast<float>(pixel_w_), paint_.sy(body_bottom)};
    const SDL_Rect clip{0, static_cast<int>(body_px_.y0), pixel_w_, static_cast<int>(body_px_.y1 - body_px_.y0)};
    SDL_SetRenderClipRect(renderer_, &clip);
    body(y0, true);
    SDL_SetRenderClipRect(renderer_, nullptr);
    if (content_h_ > viewport_h_) {
        // A thin bar at the end side shows where the view is.
        const float x = std::min(paint_.view().width - 4.0f, column_x1() + 8.0f);
        const float thumb = std::max(24.0f, viewport_h_ * viewport_h_ / content_h_);
        const float at = body_top + (viewport_h_ - thumb) * (scroll_ / (content_h_ - viewport_h_));
        paint_.fill(paint_.box(x, body_top, x + 3.0f, body_bottom), {40, 40, 44}, 200);
        paint_.fill(paint_.box(x, at, x + 3.0f, at + thumb), {130, 130, 138}, 230);
    }
}

Target ProgressWindow::Screen::target_at(float x, float y) const {
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (!it->enabled || !it->box.contains(x, y)) continue;
        // A part of the body scrolled under the buttons cannot be touched there.
        if ((it->target == Target::Field || it->target == Target::Browse) && !body_px_.contains(x, y)) continue;
        return it->target;
    }
    return Target::None;
}

// ---- the picker ----

float ProgressWindow::Screen::picker_body(float y, bool paint) {
    const float start = y;
    const float x0 = column_x0(), x1 = column_x1();
    y += 18.0f;
    heading(iu::format(lang_, iu::Str::Title), iu::format(lang_, iu::Str::PickerTitle), y, paint);
    paragraph(iu::format(lang_, iu::Str::IntroNeedImage, {gamedata_dir_}), y, 14.0f, 20.0f, kBody, paint);
    y += 8.0f;
    const iu::Str hint = touch_screen() ? iu::Str::PickerHintTouch : iu::Str::PickerHint;
    paragraph(iu::format(lang_, hint), y, 14.0f, 20.0f, kBody, paint);
    y += 6.0f;
    paragraph(iu::format(lang_, iu::Str::IntroFormats), y, 11.5f, 16.0f, kGrey, paint);
    y += 16.0f;
    paragraph(iu::format(lang_, iu::Str::PathLabel), y, 12.5f, 18.0f, kGold, paint);
    y += 4.0f;

    // The field and Browse side by side, or Browse under the field on a narrow screen.
    const std::string browse = iu::format(lang_, iu::Str::ActionBrowse);
    const float browse_w = std::max(120.0f, paint_.width(browse, 15.0f) + 40.0f);
    const bool side_by_side = x1 - x0 - browse_w - kGap >= 220.0f;
    const std::string& note = picker_ ? picker_->note() : std::string();
    // No second dialog while one is open or the document it gave is still being read.
    bool dialog_open = !note.empty();
    if (dialog_) {
        std::lock_guard<std::mutex> lock(dialog_->mutex);
        dialog_open = dialog_open || dialog_->pending;
    }
    const float field_top = y - start;
    if (side_by_side) {
        if (paint) {
            draw_field(x0, x1 - browse_w - kGap, y);
            button(paint_.box(x1 - browse_w, y, x1, y + kFieldH), browse, Target::Browse, !dialog_open, false);
        }
        y += kFieldH;
    } else {
        if (paint) draw_field(x0, x1, y);
        y += kFieldH + 10.0f;
        if (paint) button(paint_.box(x0, y, x1, y + kButtonH), browse, Target::Browse, !dialog_open, false);
        y += kButtonH;
    }
    if (!paint) {
        focus_top_ = field_top;
        focus_bottom_ = y - start;
    }

    const std::string& error = picker_ ? picker_->error() : std::string();
    if (!error.empty()) {
        y += 14.0f;
        const float top = y;
        // The error reads from the column's start, beside a red mark.
        const float indent = 12.0f;
        for (const std::string& l : paint_.wrap(error, 13.0f, x1 - x0 - indent)) {
            if (paint) paint_.draw_text(l, x0 + indent, y + 9.0f, 13.0f, kRed, Align::Left);
            y += 18.0f;
        }
        if (paint) paint_.fill(paint_.box(x0, top + 1.0f, x0 + 3.0f, y - 1.0f), kRed, 220);
    } else if (!note.empty()) {
        y += 14.0f;
        paragraph(note, y, 13.0f, 18.0f, kGold, paint);
    }
    y += 12.0f;
    return y - start;
}

float ProgressWindow::Screen::caret_px(std::size_t byte) {
    const float px = std::max(6.0f, std::round(14.0f * paint_.scale()));
    const float pad = std::ceil(px * 0.1f);
    if (byte == 0) return pad;
    return paint_.width(edit_.text().substr(0, byte), 14.0f, &path_face_) * paint_.scale() - px * 0.1f;
}

void ProgressWindow::Screen::draw_field(float x0, float x1, float y) {
    const Rect box = paint_.box(x0, y, x1, y + kFieldH);
    field_px_ = box;
    const float s = paint_.scale();
    const bool focused = focus_ == Target::Field;
    paint_.fill(box, {8, 8, 10}, 240);
    paint_.fill_gradient({box.x0, box.y0, box.x1, box.y0 + box.height() * 0.5f}, {255, 255, 255}, 10, {0, 0, 0}, 0,
                         false);
    const Rgb border = drop_hover_               ? kGold
                       : focused                 ? kGold
                       : hover_ == Target::Field ? Rgb{120, 120, 128}
                                                 : Rgb{78, 78, 86};
    paint_.outline(box, focused || drop_hover_ ? std::max(1.0f, s * 1.5f) : std::max(1.0f, s), border);
    hits_.push_back({box, Target::Field, true});

    // The path, left to right in every language, scrolled so the caret stays in view.
    const float pad = 10.0f * s;
    const float inner = box.width() - 2.0f * pad;
    const float caret = caret_px(edit_.caret());
    if (caret - field_scroll_ > inner) field_scroll_ = caret - inner;
    if (caret - field_scroll_ < 0.0f) field_scroll_ = caret;
    const float full = edit_.text().empty() ? 0.0f : paint_.width(edit_.text(), 14.0f, &path_face_) * s;
    field_scroll_ = std::max(0.0f, std::min(field_scroll_, std::max(0.0f, full - inner)));
    SDL_Rect clip{static_cast<int>(box.x0 + pad * 0.5f), static_cast<int>(body_px_.y0),
                  static_cast<int>(box.width() - pad), static_cast<int>(body_px_.y1 - body_px_.y0)};
    SDL_Rect old{};
    SDL_GetRenderClipRect(renderer_, &old);
    SDL_Rect both{};
    if (SDL_GetRectIntersection(&clip, &old, &both)) {
        SDL_SetRenderClipRect(renderer_, &both);
        const float left = box.x0 + pad - field_scroll_;
        if (!edit_.text().empty()) {
            const kit::Texture& t = paint_.text(edit_.text(), 14.0f, path_face_);
            paint_.blit(t, left, (box.y0 + box.y1 - static_cast<float>(t.height)) * 0.5f, kWhite, 255, false);
        }
        const bool blink_on = ((SDL_GetTicks() - blink_from_) / 530) % 2 == 0;
        if (focused && blink_on) {
            const float cx = std::round(left + caret);
            paint_.fill({cx, box.y0 + 9.0f * s, cx + std::max(1.0f, std::round(1.5f * s)), box.y1 - 9.0f * s},
                        {240, 220, 170});
        }
        SDL_SetRenderClipRect(renderer_, &old);
    }
}

void ProgressWindow::Screen::draw_picker() {
    begin_frame();
    // On a phone the field only shows the file Browse chose: no path is typed there.
    if (touch_screen())
        stops_ = {Target::Browse, Target::Import, Target::Quit};
    else
        stops_ = {Target::Field, Target::Browse, Target::Import, Target::Quit};
    const float h = paint_.view().height;
    // The footer first: it sets where the body ends.
    const std::vector<std::pair<Target, std::string>> buttons{
        {Target::Import, iu::format(lang_, iu::Str::ActionImport)},
        {Target::Quit, iu::format(lang_, iu::Str::ActionQuit)}};
    body_px_ = {0, 0, static_cast<float>(pixel_w_), paint_.sy(h)};
    footer(buttons, {true, true});
    const float body_bottom = (body_px_.y1 - paint_.view().origin_y) / paint_.scale();
    scrolled_body(0.0f, body_bottom, [this](float y, bool paint) { return picker_body(y, paint); });
    if (drop_hover_ && !touch_screen()) {
        // A file is being dragged over the window: a frame round the whole screen.
        const float s = paint_.scale();
        paint_.outline(paint_.box(6.0f, 6.0f, paint_.view().width - 6.0f, h - 6.0f), 2.0f * s, kGold, 200);
    }
}

void ProgressWindow::Screen::set_focus(Target t) {
    focus_ = t;
    reveal_focus_ = true;
    blink_from_ = SDL_GetTicks();
    // A phone shows its keyboard for text input: only when the field is touched, not when the focus passes by.
    if (t != Target::Field)
        text_input(false);
    else if (!touch_screen())
        text_input(true);
}

void ProgressWindow::Screen::move_focus(int dir) {
    if (stops_.empty()) return;
    const auto at = std::find(stops_.begin(), stops_.end(), focus_);
    const int n = static_cast<int>(stops_.size());
    const int index = at == stops_.end() ? (dir > 0 ? -1 : 0) : static_cast<int>(at - stops_.begin());
    set_focus(stops_[static_cast<std::size_t>((index + dir + n) % n)]);
}

void ProgressWindow::Screen::step_focus(int dir) {
    // Left and right move between the neighbours of a row: the field and Browse, Import and Quit.
    if (rtl_) dir = -dir;
    if (focus_ == Target::Browse && dir < 0 && !touch_screen())
        set_focus(Target::Field);
    else if (focus_ == Target::Import && dir > 0)
        set_focus(Target::Quit);
    else if (focus_ == Target::Quit && dir < 0)
        set_focus(Target::Import);
    else if (focus_ == Target::None)
        move_focus(1);
}

void ProgressWindow::Screen::open_dialog() {
    if (!dialog_) return;
    {
        std::lock_guard<std::mutex> lock(dialog_->mutex);
        if (dialog_->pending) return;
        dialog_->pending = true;
    }
    text_input(false);
#ifdef __ANDROID__
    // The activity's own request (SilentHillActivity.pickDocument), answered through takeDocument in pick(): the
    // answer waits in the activity, so it also reaches a process the system restarted while the picker was open,
    // and the picker offers every file, whatever type its provider reports (the check names a wrong one).
    if (activity_call("pickDocument", std::string()) != "1") {
        std::lock_guard<std::mutex> lock(dialog_->mutex);
        dialog_->pending = false;
        dialog_->answered = true;
        dialog_->path.clear();
        dialog_->error = "?";
    }
#else
    SDL_ShowOpenFileDialog(on_dialog, new std::shared_ptr<DialogState>(dialog_), window_, &dialog_->filter, 1, nullptr,
                           false);
#endif
}

void ProgressWindow::Screen::submit() {
    picker_->set_text(edit_.text());
    synced_ = edit_.text();
    if (!picker_->submit()) {
        scroll_ = 1e9f;  // the error is at the bottom of the body
        // A phone with no file chosen: the error names Browse, and the focus goes there.
        if (touch_screen() && picker_->text().empty() && picker_->note().empty()) set_focus(Target::Browse);
    }
}

void ProgressWindow::Screen::activate(Target t) {
    switch (t) {
        case Target::Field:
            if (touch_screen()) {
                // A touch on the field opens the file dialog, as Browse does.
                set_focus(Target::Browse);
                open_dialog();
                break;
            }
            focus_ = Target::Field;
            blink_from_ = SDL_GetTicks();
            text_input(true);
            break;
        case Target::Browse:
            set_focus(Target::Browse);
            open_dialog();
            break;
        case Target::Import:
            set_focus(Target::Import);
            submit();
            break;
        case Target::Quit:
            if (picker_) picker_->quit();
            break;
        case Target::Cancel:
            stop_ = true;
            break;
        case Target::None:
            break;
    }
}

void ProgressWindow::Screen::place_caret(float pixel_x) {
    const float pad = 10.0f * paint_.scale();
    const float local = pixel_x - (field_px_.x0 + pad - field_scroll_);
    const std::string& text = edit_.text();
    std::size_t best = 0;
    float best_d = std::fabs(caret_px(0) - local);
    for (std::size_t i = 1; i <= text.size(); ++i) {
        if (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;
        const float d = std::fabs(caret_px(i) - local);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    edit_.place(best);
}

void ProgressWindow::Screen::pointer_event(const SDL_Event& event, bool picker) {
    SDL_Event e = event;
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            if (e.motion.which == SDL_TOUCH_MOUSEID) return;
            SDL_ConvertEventToRenderCoordinates(renderer_, &e);
            hover_ = target_at(e.motion.x, e.motion.y);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.which == SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT) return;
            SDL_ConvertEventToRenderCoordinates(renderer_, &e);
            pressed_ = target_at(e.button.x, e.button.y);
            if (pressed_ == Target::Field) {
                activate(Target::Field);
                place_caret(e.button.x);
                pressed_ = Target::None;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.which == SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT) return;
            SDL_ConvertEventToRenderCoordinates(renderer_, &e);
            if (pressed_ != Target::None && target_at(e.button.x, e.button.y) == pressed_) activate(pressed_);
            pressed_ = Target::None;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            scroll_ -= e.wheel.y * 40.0f;
            break;
        case SDL_EVENT_FINGER_DOWN: {
            if (finger_down_) return;
            const float x = e.tfinger.x * static_cast<float>(pixel_w_), y = e.tfinger.y * static_cast<float>(pixel_h_);
            finger_down_ = true;
            finger_ = e.tfinger.fingerID;
            dragging_ = false;
            down_x_ = x;
            down_y_ = y;
            scroll_at_down_ = scroll_;
            pressed_ = target_at(x, y);
            break;
        }
        case SDL_EVENT_FINGER_MOTION: {
            if (!finger_down_ || e.tfinger.fingerID != finger_) return;
            const float x = e.tfinger.x * static_cast<float>(pixel_w_), y = e.tfinger.y * static_cast<float>(pixel_h_);
            // Past a few units it is a drag that scrolls, no longer a tap.
            if (!dragging_ && std::hypot(x - down_x_, y - down_y_) > 10.0f * paint_.scale()) {
                dragging_ = true;
                pressed_ = Target::None;
            }
            if (dragging_) scroll_ = scroll_at_down_ - (y - down_y_) / paint_.scale();
            break;
        }
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            if (!finger_down_ || e.tfinger.fingerID != finger_) return;
            finger_down_ = false;
            const float x = e.tfinger.x * static_cast<float>(pixel_w_), y = e.tfinger.y * static_cast<float>(pixel_h_);
            const Target t = pressed_;
            pressed_ = Target::None;
            if (e.type == SDL_EVENT_FINGER_UP && !dragging_ && t != Target::None && target_at(x, y) == t) {
                activate(t);
                if (t == Target::Field && picker) place_caret(x);
            }
            dragging_ = false;
            break;
        }
        default:
            break;
    }
}

void ProgressWindow::Screen::handle_picker(const SDL_Event& e) {
    const bool field = focus_ == Target::Field;
    switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            picker_->quit();
            break;
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_POSITION:
            drop_hover_ = true;
            break;
        case SDL_EVENT_DROP_COMPLETE:
            drop_hover_ = false;
            break;
        case SDL_EVENT_DROP_FILE:
            drop_hover_ = false;
            if (e.drop.data) {
                picker_->choose(e.drop.data);
                set_focus(picker_->error().empty() ? Target::Import : touch_screen() ? Target::Browse : Target::Field);
            }
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (field && e.text.text) {
                edit_.insert(e.text.text);
                blink_from_ = SDL_GetTicks();
            }
            break;
        case SDL_EVENT_KEY_DOWN: {
            const bool ctrl = (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
            blink_from_ = SDL_GetTicks();
            switch (e.key.key) {
                case SDLK_ESCAPE:
                case SDLK_AC_BACK:
                    picker_->quit();
                    break;
                case SDLK_TAB:
                    move_focus((e.key.mod & SDL_KMOD_SHIFT) ? -1 : 1);
                    break;
                case SDLK_UP:
                    move_focus(-1);
                    break;
                case SDLK_DOWN:
                    move_focus(1);
                    break;
                case SDLK_LEFT:
                    if (field)
                        edit_.left();
                    else
                        step_focus(-1);
                    break;
                case SDLK_RIGHT:
                    if (field)
                        edit_.right();
                    else
                        step_focus(1);
                    break;
                case SDLK_HOME:
                    if (field) edit_.home();
                    break;
                case SDLK_END:
                    if (field) edit_.end();
                    break;
                case SDLK_BACKSPACE:
                    if (field) edit_.backspace();
                    break;
                case SDLK_DELETE:
                    if (field) edit_.erase_forward();
                    break;
                case SDLK_V:
                case SDLK_INSERT:
                    if (field &&
                        ((e.key.key == SDLK_V && ctrl) || (e.key.key == SDLK_INSERT && (e.key.mod & SDL_KMOD_SHIFT)))) {
                        if (char* clip = SDL_GetClipboardText()) {
                            edit_.insert(clip);
                            SDL_free(clip);
                        }
                    }
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    activate(focus_ == Target::Field || focus_ == Target::None ? Target::Import : focus_);
                    break;
                case SDLK_SPACE:
                    if (!field && focus_ != Target::None) activate(focus_);
                    break;
                case SDLK_O:
                    if (ctrl) open_dialog();
                    break;
                default:
                    break;
            }
            break;
        }
        case SDL_EVENT_GAMEPAD_ADDED:
            gamepad_added(e.gdevice.which);
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
                    step_focus(-1);
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                    step_focus(1);
                    break;
                case SDL_GAMEPAD_BUTTON_SOUTH:
                    // On the field: the system's file dialog, which a controller can work.
                    activate(focus_ == Target::Field || focus_ == Target::None ? Target::Browse : focus_);
                    break;
                case SDL_GAMEPAD_BUTTON_EAST:
                    picker_->quit();
                    break;
                case SDL_GAMEPAD_BUTTON_START:
                    activate(Target::Import);
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
                if (std::fabs(v) > 0.5f && std::fabs(stick_x_) <= 0.5f) step_focus(v > 0 ? 1 : -1);
                stick_x_ = v;
            }
            break;
        }
        default:
            pointer_event(e, true);
            break;
    }
}

// The script's verbs for the picker: text <path>, drop <path>, key tab|up|down|left|right|enter|back, focus
// field|browse|import|quit, size <w>x<h> (the window), shot <file.bmp>, import, quit. Once the import runs, the
// rest of the script goes to the progress screen (run_progress_command).
void ProgressWindow::Screen::run_picker_command(const kit::ScriptCommand& c) {
    std::fprintf(stderr, "import screen script: %s %s\n", c.verb.c_str(), c.arg.c_str());
    const auto fail = [&](const char* why) {
        std::fprintf(stderr, "import screen script: %s %s: %s\n", c.verb.c_str(), c.arg.c_str(), why);
    };
    if (c.verb == "text") {
        edit_.set(c.arg);
        set_focus(Target::Field);
    } else if (c.verb == "drop") {
        picker_->choose(c.arg);
    } else if (c.verb == "key") {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        const std::pair<const char*, SDL_Keycode> keys[] = {
            {"tab", SDLK_TAB},     {"up", SDLK_UP},        {"down", SDLK_DOWN},  {"left", SDLK_LEFT},
            {"right", SDLK_RIGHT}, {"enter", SDLK_RETURN}, {"back", SDLK_ESCAPE}};
        for (const auto& [name, key] : keys)
            if (c.arg == name) e.key.key = key;
        if (e.key.key == 0) return fail("unknown key");
        handle_picker(e);
    } else if (c.verb == "focus") {
        const std::pair<const char*, Target> targets[] = {
            {"field", Target::Field}, {"browse", Target::Browse}, {"import", Target::Import}, {"quit", Target::Quit}};
        for (const auto& [name, t] : targets)
            if (c.arg == name) set_focus(t);
    } else if (c.verb == "size") {
        int w = 0, h = 0;
        if (std::sscanf(c.arg.c_str(), "%dx%d", &w, &h) != 2 || w < 320 || h < 240) return fail("<w>x<h>");
        SDL_SetWindowSize(window_, w, h);
        SDL_SyncWindow(window_);
    } else if (c.verb == "shot") {
        draw_picker();
        save_shot(c.arg);
    } else if (c.verb == "import") {
        submit();
    } else if (c.verb == "quit") {
        picker_->quit();
    } else {
        fail("unknown command");
    }
}

void ProgressWindow::Screen::pick(iu::ImagePicker& picker, iu::Lang lang, const std::string& gamedata_dir) {
    if (renderer_ == nullptr) {
        picker.quit();
        return;
    }
    picker_ = &picker;
    lang_ = lang;
    gamedata_dir_ = gamedata_dir;
    SDL_SetWindowTitle(window_, ("Silent Hill - " + iu::format(lang, iu::Str::Title)).c_str());
    title_.clear();
    std::fprintf(stderr, "first run: the disc image picker is open\n");
    std::fflush(stderr);

    dialog_ = std::make_shared<DialogState>();
    dialog_->filter_name = iu::format(lang, iu::Str::PickerFilter);
    dialog_->filter = SDL_DialogFileFilter{dialog_->filter_name.c_str(), "cue;bin;iso;img;zip"};
    picker.set_touch(touch_screen());
    edit_.set(picker.text());
    synced_ = picker.text();
    scroll_ = picker.error().empty() ? 0.0f : 1e9f;
    hover_ = pressed_ = Target::None;
    // A phone starts on Browse (its document picker); a desktop on the field, ready for typing or pasting.
    set_focus(touch_screen() ? Target::Browse : Target::Field);

    while (picker.state() == iu::ImagePicker::State::Picking) {
        SDL_Event e;
        if (scripted_) {
            while (SDL_PollEvent(&e)) handle_picker(e);
            if (picker.state() == iu::ImagePicker::State::Picking) {
                if (script_at_ < script_.size()) {
                    run_picker_command(script_[script_at_++]);
                } else {
                    std::fprintf(stderr, "import screen script: ended in the picker; nothing is imported\n");
                    picker.quit();
                }
            }
        } else if (SDL_WaitEventTimeout(&e, 100)) {
            do {
                handle_picker(e);
            } while (SDL_PollEvent(&e));
        }
#ifdef __ANDROID__
        {
            // Asked on every turn, not only after Browse: a pick made before the system restarted the app is
            // answered to this run.
            const iu::PickAnswer answer = iu::parse_pick_answer(activity_call("takeDocument", std::string()));
            if (answer.kind != iu::PickAnswer::Kind::Waiting) {
                const bool was_opening = !picker.note().empty();
                picker.apply(answer);
                if (answer.kind != iu::PickAnswer::Kind::Opening) {
                    std::lock_guard<std::mutex> lock(dialog_->mutex);
                    dialog_->pending = false;
                    if (answer.kind == iu::PickAnswer::Kind::Path && picker.error().empty()) {
                        set_focus(Target::Import);
                    }
                    if (!picker.error().empty()) scroll_ = 1e9f;
                } else if (!was_opening) {
                    scroll_ = 1e9f;
                }
            }
        }
#endif
        {
            std::lock_guard<std::mutex> lock(dialog_->mutex);
            if (dialog_->answered) {
                dialog_->answered = false;
                if (!dialog_->error.empty()) {
                    picker.dialog_failed(dialog_->error, touch_screen());
                } else if (!dialog_->path.empty()) {
                    picker.choose(picked_document(dialog_->path));
                    if (picker.error().empty()) set_focus(Target::Import);
                }
            }
        }
        // The picker changes the text on a drop or a dialog's answer, the field on typing.
        if (picker.text() != synced_) {
            edit_.set(picker.text());
            synced_ = picker.text();
            if (!picker.error().empty()) scroll_ = 1e9f;
        } else if (edit_.text() != synced_) {
            picker.set_text(edit_.text());
            synced_ = edit_.text();
        }
        if (picker.state() != iu::ImagePicker::State::Picking) break;
        draw_picker();
        end_frame();
    }
    text_input(false);
    focus_ = hover_ = pressed_ = Target::None;
    drop_hover_ = false;
    scroll_ = 0.0f;
    stop_ = false;
    picker_ = nullptr;
}

// ---- the progress ----

float ProgressWindow::Screen::progress_body(const iu::ViewModel& v, float y, bool paint) {
    const float start = y;
    const float x0 = column_x0(), x1 = column_x1();
    y += 18.0f;
    heading(v.title, subject_, y, paint);

    // The stages as the menus list their rows: done ones dim, the current one lit and underlined.
    const auto current = std::find(std::begin(kStages), std::end(kStages), v.stage) - std::begin(kStages);
    for (int i = 0; i < kStageCount; ++i) {
        const float row_y = y + 13.0f;
        if (paint) {
            const bool now = i == current;
            const Rgb color = now ? kWhite : i < current ? kGrey : Rgb{70, 70, 74};
            const std::string label = iu::format(lang_, kStageNames[i]);
            paint_.bullet(x0 + 10.0f, row_y, now || i < current);
            const float w = paint_.draw_text(label, x0 + 26.0f, row_y, 14.0f, color, Align::Left, x1 - x0 - 30.0f);
            if (now) paint_.underline(x0 + 26.0f, x0 + 26.0f + std::max(w, 120.0f) + 6.0f, row_y + 9.0f);
        }
        y += 26.0f;
    }
    y += 18.0f;

    // The bar: a dark track, the done part in the gold of the menus (red while stopping).
    const bool stopping =
        v.phase == iu::Phase::Cancelling || v.phase == iu::Phase::Cancelled || v.phase == iu::Phase::Failed;
    if (paint) {
        const Rect track = paint_.box(x0, y, x1, y + 12.0f);
        paint_.fill(track, {14, 14, 16}, 255);
        paint_.outline(track, std::max(1.0f, paint_.scale()), {70, 70, 76});
        const double f = std::clamp(v.fraction, 0.0, 1.0);
        const float fx = x0 + 2.0f + static_cast<float>(f) * (x1 - x0 - 4.0f);
        if (f > 0.0) {
            const Rect done = paint_.box(x0 + 2.0f, y + 2.0f, fx, y + 10.0f);
            const Rgb a = stopping ? Rgb{120, 30, 28} : paint_.style().underline[1];
            const Rgb b = stopping ? Rgb{190, 50, 44} : kGold;
            if (paint_.mirrored()) {
                paint_.fill_gradient(done, b, 255, a, 255, true);
            } else {
                paint_.fill_gradient(done, a, 255, b, 255, true);
            }
        }
        if (v.indeterminate) {
            // A soft light sweeping along the track: the step's own progress is not known.
            const float span = x1 - x0 - 4.0f;
            const float w = span * 0.18f;
            const float phase = static_cast<float>(SDL_GetTicks() % 1600) / 1600.0f;
            const float c = x0 + 2.0f - w + phase * (span + w);
            const float l = std::max(x0 + 2.0f, c), r = std::min(x1 - 2.0f, c + w);
            if (r > l) {
                const float mid = std::clamp(c + w * 0.5f, l, r);
                paint_.fill_gradient(paint_.box(l, y + 2.0f, mid, y + 10.0f), kGold, 0, kGold, 200, true);
                paint_.fill_gradient(paint_.box(mid, y + 2.0f, r, y + 10.0f), kGold, 200, kGold, 0, true);
            }
        }
    }
    y += 12.0f + 18.0f;
    if (paint) {
        paint_.draw_text(std::to_string(std::clamp(v.percent, 0, 100)) + "%", x0, y, 22.0f, kWhite, Align::Left);
        paint_.draw_text(v.eta_text, x1, y, 13.0f, kGrey, Align::Right, (x1 - x0) * 0.6f);
    }
    y += 24.0f;
    for (const std::string& p : v.body) {
        paragraph(p, y, 13.5f, 19.0f, kBody, paint);
        y += 4.0f;
    }
    if (!v.message.empty()) {
        y += 6.0f;
        paragraph(v.message, y, 13.0f, 18.0f, v.message_is_error ? kRed : kBody, paint);
    }
    y += 12.0f;
    return y - start;
}

void ProgressWindow::Screen::draw_progress(const iu::ViewModel& v) {
    begin_frame();
    std::string label = iu::format(lang_, iu::Str::ActionCancel);
    bool enabled = v.phase == iu::Phase::Importing;
    for (const iu::Action& a : v.actions) {
        if (a.id == iu::ActionId::Cancel || a.id == iu::ActionId::Quit) {
            label = a.label;
            enabled = a.enabled && v.phase != iu::Phase::Cancelling;
            break;
        }
    }
    stops_ = {Target::Cancel};
    if (focus_ != Target::Cancel) focus_ = Target::None;
    body_px_ = {0, 0, static_cast<float>(pixel_w_), paint_.sy(paint_.view().height)};
    // Cancel is a plain button: the import going on is the main thing here.
    const float x0 = column_x0(), x1 = column_x1();
    const float bw = std::min(x1 - x0, std::max(150.0f, paint_.width(label, 15.0f) + 40.0f));
    const float bottom = paint_.view().height - 16.0f;
    const float mid = (x0 + x1) * 0.5f;
    button(paint_.box(mid - bw * 0.5f, bottom - kButtonH, mid + bw * 0.5f, bottom), label, Target::Cancel, enabled,
           false);
    body_px_.y1 = paint_.sy(bottom - kButtonH - 14.0f);
    scrolled_body(0.0f, bottom - kButtonH - 14.0f, [&](float y, bool paint) { return progress_body(v, y, paint); });
}

void ProgressWindow::Screen::handle_progress(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            stop_ = true;
            break;
        case SDL_EVENT_KEY_DOWN:
            switch (e.key.key) {
                case SDLK_ESCAPE:
                case SDLK_AC_BACK:
                    stop_ = true;
                    break;
                case SDLK_TAB:
                case SDLK_UP:
                case SDLK_DOWN:
                    focus_ = Target::Cancel;
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    if (focus_ == Target::Cancel) stop_ = true;
                    break;
                default:
                    break;
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            gamepad_added(e.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) stop_ = true;
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN || e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP)
                focus_ = Target::Cancel;
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH && focus_ == Target::Cancel) stop_ = true;
            break;
        default:
            pointer_event(e, false);
            break;
    }
}

// The script's verbs once the import runs: wait <percent> (until the import has come that far), stage <name> (until
// that stage runs: read, raw, convert, verify, commit, prepare), shot <file.bmp>, size <w>x<h>, cancel. False while a
// wait holds the script.
bool ProgressWindow::Screen::run_progress_command(const kit::ScriptCommand& c, const iu::ViewModel& v) {
    if (c.verb == "wait") {
        return v.percent >= std::atoi(c.arg.c_str());
    }
    if (c.verb == "stage") {
        return v.stage == c.arg;
    }
    std::fprintf(stderr, "import screen script: %s %s\n", c.verb.c_str(), c.arg.c_str());
    if (c.verb == "shot") {
        draw_progress(v);
        save_shot(c.arg);
    } else if (c.verb == "size") {
        int w = 0, h = 0;
        if (std::sscanf(c.arg.c_str(), "%dx%d", &w, &h) == 2 && w >= 320 && h >= 240) {
            SDL_SetWindowSize(window_, w, h);
            SDL_SyncWindow(window_);
        }
    } else if (c.verb == "cancel") {
        stop_ = true;
    } else {
        std::fprintf(stderr, "import screen script: %s: not a command of the progress screen\n", c.verb.c_str());
    }
    return true;
}

void ProgressWindow::Screen::draw(const iu::ViewModel& v) {
    if (renderer_ == nullptr) return;
    std::string title = "Silent Hill - " + v.title;
    if (v.has_progress) title += ": " + v.stage_text + " " + std::to_string(v.percent) + "%";
    if (title != title_) {
        title_ = title;
        SDL_SetWindowTitle(window_, title_.c_str());
    }
    while (scripted_ && script_at_ < script_.size() && run_progress_command(script_[script_at_], v)) ++script_at_;
    draw_progress(v);
    end_frame();
}

bool ProgressWindow::Screen::stop_requested() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) handle_progress(e);
    return stop_;
}

// ---- the facade ----

ProgressWindow::ProgressWindow(WindowOptions options) : screen_(std::make_unique<Screen>(std::move(options))) {}

ProgressWindow::~ProgressWindow() = default;

bool ProgressWindow::open() { return screen_->open(); }

void ProgressWindow::draw(const iu::ViewModel& view) { screen_->draw(view); }

bool ProgressWindow::stop_requested() { return screen_->stop_requested(); }

bool ProgressWindow::can_pick() const { return screen_->can_pick(); }

void ProgressWindow::pick(iu::ImagePicker& picker, iu::Lang lang, const std::string& gamedata_dir) {
    screen_->pick(picker, lang, gamedata_dir);
}

void ProgressWindow::set_subject(std::string subject) { screen_->set_subject(std::move(subject)); }

void ProgressWindow::close() { screen_->close(); }

}  // namespace sh::first_run
