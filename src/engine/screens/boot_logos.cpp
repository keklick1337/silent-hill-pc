// SPDX-License-Identifier: GPL-3.0-or-later
// The boot logo screens and gamedata/menu/boot_logos.json.

#include "engine/screens/boot_logos.h"

#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::screens {

const LogoScreen* BootLogos::find(std::string_view name) const {
    for (const LogoScreen& s : screens) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

void BootLogoScreen::run(BootLogoHost& host) {
    while (host.active()) {
        frame(host);
    }
}

void BootLogoScreen::skip(BootLogoHost& host) {
    const bool pressed = host.skip_pressed();
    skipped_ = skipped_ || pressed;
    if (!(screen_.skip_latched ? skipped_ : pressed)) {
        return;
    }
    const std::int32_t step = host.step(0);
    if (!screen_.card_check) {
        // The skip only reaches the visible phases; the loads started at the init are
        // still awaited when the fade-out finishes.
        if (step == kStepWaitFade || step == hold_step()) {
            host.set_fade({FadeState::OutSteps, false});
            host.set_fade_step(screen_.skip_fade_step);
            host.set_step(0, finish_step());
        }
        return;
    }
    const Fade f = host.fade();
    if (step == kStepWaitFade && !(f.state == FadeState::None && !f.white)) {
        host.set_fade_step(screen_.skip_fade_step);
    } else if (step == hold_step()) {
        host.set_fade({FadeState::OutSteps, false});
        host.set_fade_step(screen_.skip_fade_step);
        host.set_step(0, finish_step());
    }
}

void BootLogoScreen::frame(BootLogoHost& host) {
    host.poll_input();
    skip(host);

    const std::int32_t step = host.step(0);
    if (step == kStepInit) {
        host.prepare(screen_.name);
        host.set_fade({FadeState::InStart, false});
        host.set_fade_step(screen_.fade_in_step);
        host.start_loads(screen_.name);
        host.set_step(0, step + 1);
    } else if (step == kStepWaitFade) {
        const Fade f = host.fade();
        if (f.state == FadeState::None && !f.white) {
            if (!screen_.card_check) {
                host.set_step(0, hold_step());
            } else {
                host.wait_loads();
                while (host.step(0) < static_cast<std::int32_t>(CardOutcome::NoCard)) {
                    host.set_step(0, static_cast<std::int32_t>(host.check_cards()));
                    host.wait_vblank();
                }
            }
        }
    } else if (screen_.card_check && step >= static_cast<std::int32_t>(CardOutcome::NoCard) &&
               step <= static_cast<std::int32_t>(CardOutcome::HasSave)) {
        host.card_outcome(static_cast<CardOutcome>(step));
        host.set_step(0, hold_step());
        host.reset_counters();
    } else if (step == hold_step()) {
        if (host.any_button_held() || host.frame_counter() > screen_.hold_frames) {
            host.set_fade({FadeState::OutSteps, false});
            host.set_fade_step(screen_.fade_out_step);
            host.set_step(0, finish_step());
        }
    } else if (step == finish_step()) {
        if (host.fade().state == FadeState::OutComplete) {
            if (screen_.card_check) {
                host.finish_card_check();
            } else {
                host.wait_loads();
                host.set_next_state(screen_.next_state);
            }
        }
    }

    draw(host);
    host.end_frame();
}

void BootLogoScreen::draw(BootLogoHost& host) const {
    for (const LogoSegment& s : screen_.segments) {
        host.draw_segment(screen_.image, s, logos_.order);
    }
    host.draw_backdrop(logos_.backdrop_rgb, logos_.backdrop, logos_.order);
}

namespace {

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

std::int32_t int_value(const Value& v, std::int32_t lo, std::int32_t hi) {
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(at(v, "an integer in " + std::to_string(lo) + ".." + std::to_string(hi) + " is expected"));
    }
    return static_cast<std::int32_t>(v.integer());
}

std::int32_t int_of(const Value& parent, std::string_view key, std::int32_t lo, std::int32_t hi) {
    return int_value(member(parent, key), lo, hi);
}

// A fraction of a whole as the game's Q12: multiplied in float and truncated.
std::int32_t q12_of(const Value& parent, std::string_view key) {
    const Value& v = member(parent, key);
    if (!v.is_number() || v.number() < 0.0 || v.number() > 64.0) {
        fail(at(v, "\"" + std::string(key) + "\" must be a number in 0..64"));
    }
    volatile float f = static_cast<float>(v.number()) * 4096.0f;
    return static_cast<std::int32_t>(f);
}

std::string string_of(const Value& parent, std::string_view key) {
    const Value& v = member(parent, key);
    if (!v.is_string() || v.string().empty()) {
        fail(at(v, "\"" + std::string(key) + "\" must be a non-empty string"));
    }
    return v.string();
}

bool bool_of(const Value& parent, std::string_view key) {
    const Value& v = member(parent, key);
    if (!v.is_bool()) {
        fail(at(v, "\"" + std::string(key) + "\" must be true or false"));
    }
    return v.boolean();
}

LogoScreen screen_of(const Value& s) {
    reject_unknown(s, {"name", "image", "segments", "card_check", "fade_in_step", "fade_out_step", "hold_frames",
                       "skip_fade_step", "skip_latched", "next_state"});
    LogoScreen out;
    out.name = string_of(s, "name");
    out.image = string_of(s, "image");
    out.card_check = bool_of(s, "card_check");
    out.fade_in_step = q12_of(s, "fade_in_step");
    out.fade_out_step = q12_of(s, "fade_out_step");
    out.hold_frames = int_of(s, "hold_frames", 0, 1 << 20);
    out.skip_fade_step = q12_of(s, "skip_fade_step");
    out.skip_latched = bool_of(s, "skip_latched");
    if (!out.card_check) {
        out.next_state = string_of(s, "next_state");
    } else if (s.find("next_state") != nullptr) {
        fail(at(s, "a screen with a card check takes its next state from the cards"));
    }
    const Value& segs = member(s, "segments");
    if (!segs.is_array()) {
        fail(at(segs, "\"segments\" must be a list"));
    }
    for (const Value& g : segs.array()) {
        // [image x, image y, width, height, screen x, screen y]
        if (!g.is_array() || g.array().size() != 6) {
            fail(at(g, "a segment is [image_x, image_y, width, height, x, y]"));
        }
        const auto& a = g.array();
        LogoSegment seg;
        seg.image_x = int_value(a[0], 0, 1023);
        seg.image_y = int_value(a[1], 0, 511);
        seg.width = int_value(a[2], 1, 256);
        seg.height = int_value(a[3], 1, 256);
        seg.x = int_value(a[4], -1024, 1024);
        seg.y = int_value(a[5], -1024, 1024);
        out.segments.push_back(seg);
    }
    return out;
}

}  // namespace

Result<BootLogos> parse_boot_logos(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<BootLogos>::fail(doc.error());
    }
    const Value& root = doc.value();
    reject_unknown(root, {"screens", "backdrop_rgb", "backdrop", "order"});
    BootLogos out;
    const Value& screens = member(root, "screens");
    if (!screens.is_array() || screens.array().empty()) {
        fail(at(screens, "\"screens\" must list at least one screen"));
    }
    for (const Value& s : screens.array()) {
        LogoScreen screen = screen_of(s);
        if (out.find(screen.name) != nullptr) {
            fail(at(s, "screen \"" + screen.name + "\" is listed twice"));
        }
        out.screens.push_back(std::move(screen));
    }
    const Value& rgb = member(root, "backdrop_rgb");
    if (!rgb.is_string() || rgb.string().size() != 7 || rgb.string()[0] != '#' ||
        rgb.string().find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos) {
        fail(at(rgb, "\"backdrop_rgb\" must be \"#RRGGBB\""));
    }
    // "#RRGGBB" -> the game's 0xBBGGRR colour word.
    const std::uint32_t v = static_cast<std::uint32_t>(std::stoul(rgb.string().substr(1), nullptr, 16));
    out.backdrop_rgb = ((v >> 16) & 0xFF) | (v & 0xFF00) | ((v & 0xFF) << 16);
    const Value& rect = member(root, "backdrop");
    if (!rect.is_array() || rect.array().size() != 4) {
        fail(at(rect, "\"backdrop\" is [x, y, w, h]"));
    }
    for (int k = 0; k < 4; k++) {
        out.backdrop[k] = int_value(rect.array()[static_cast<std::size_t>(k)], -4096, 4096);
    }
    out.order = int_of(root, "order", 0, 4095);
    return Result<BootLogos>::ok(std::move(out));
} catch (const std::exception& e) {
    return Result<BootLogos>::fail(e.what());
}

Result<BootLogos> load_boot_logos(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                  std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("menu") / "boot_logos.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<BootLogos>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<BootLogos> parsed = parse_boot_logos(text);
    if (!parsed) {
        return Result<BootLogos>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::screens
