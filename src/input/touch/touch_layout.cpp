// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/touch/touch_layout.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdio>

namespace sh::input::touch {
namespace {

constexpr std::array<std::string_view, kControlCount> kNames = {
    "stick", "action", "aim", "attack", "map", "inventory", "pause", "struggle", "camera", "dpad", "run"};

// Thumbs rest at the lower corners: the stick on the left, the buttons a thumb presses most (attack, aim, action)
// round the right one, the screens' buttons along the top.
constexpr std::array<Place, kControlCount> kDefaults = {{
    {0.14f, 0.70f, 0.38f},  // stick
    {0.80f, 0.48f, 0.18f},  // action
    {0.68f, 0.80f, 0.18f},  // aim
    {0.88f, 0.74f, 0.23f},  // attack
    {0.92f, 0.11f, 0.13f},  // map
    {0.80f, 0.11f, 0.13f},  // inventory
    {0.06f, 0.11f, 0.13f},  // pause
    {0.80f, 0.64f, 0.32f},  // struggle: large, under the right thumb, where aim, attack and action give way to it
    {0.68f, 0.11f, 0.13f},  // camera
    {0.16f, 0.70f, 0.38f},  // dpad: where the stick's thumb rests, a little in from the edge of a 4:3 tablet
    {0.29f, 0.40f, 0.14f},  // run: above and right of the d-pad, out of reach of a thumb sliding over the arrows
}};

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

std::optional<float> number(std::string_view s) {
    s = trim(s);
    if (s.empty()) return std::nullopt;
    std::string copy(s);
    char* end = nullptr;
    const float v = std::strtof(copy.c_str(), &end);
    if (end == copy.c_str() || *end != '\0' || !std::isfinite(v)) return std::nullopt;
    return v;
}

}  // namespace

std::string_view control_name(Control control) {
    const int i = static_cast<int>(control);
    return i >= 0 && i < kControlCount ? kNames[static_cast<std::size_t>(i)] : std::string_view("?");
}

std::optional<Control> find_control(std::string_view name) {
    for (int i = 0; i < kControlCount; ++i) {
        if (kNames[static_cast<std::size_t>(i)] == name) return static_cast<Control>(i);
    }
    return std::nullopt;
}

Layout default_layout() {
    Layout l;
    l.places = kDefaults;
    return l;
}

Place clamp_place(Control control, Place place) {
    const float base = kDefaults[static_cast<std::size_t>(control)].size;
    place.size = std::clamp(place.size, base * kMinSizeFactor, base * kMaxSizeFactor);
    place.x = std::clamp(place.x, 0.0f, 1.0f);
    place.y = std::clamp(place.y, 0.0f, 1.0f);
    return place;
}

namespace {

// 0..1 with three decimals at most and no leading zero or trailing zeros: ".14", ".7", "1", "0".
void append_number(std::string& out, float v) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%.3f", v);
    std::string text = buffer;
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    if (text.size() > 1 && text[0] == '0' && text[1] == '.') text.erase(0, 1);
    if (text.empty() || text == "-0") text = "0";
    out += text;
}

}  // namespace

std::string format_layout(const Layout& layout) {
    std::string out;
    for (int i = 0; i < kControlCount; ++i) {
        const Place& p = layout.places[static_cast<std::size_t>(i)];
        if (i != 0) out += ';';
        out += kNames[static_cast<std::size_t>(i)];
        out += ':';
        append_number(out, p.x);
        out += ',';
        append_number(out, p.y);
        out += ',';
        append_number(out, p.size);
    }
    return out;
}

Layout parse_layout(std::string_view text, std::vector<std::string>* warnings) {
    Layout layout = default_layout();
    const auto warn = [warnings](std::string message) {
        if (warnings != nullptr) warnings->push_back(std::move(message));
    };
    while (!trim(text).empty()) {
        const std::size_t end = text.find(';');
        const std::string_view entry = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (entry.empty()) continue;
        const std::size_t colon = entry.find(':');
        const std::optional<Control> control =
            colon == std::string_view::npos ? std::nullopt : find_control(trim(entry.substr(0, colon)));
        if (!control) {
            warn("unknown control in '" + std::string(entry) + "'");
            continue;
        }
        std::array<float, 3> values{};
        std::string_view rest = entry.substr(colon + 1);
        bool ok = true;
        for (std::size_t v = 0; v < values.size() && ok; ++v) {
            const std::size_t comma = rest.find(',');
            const std::optional<float> n = number(rest.substr(0, comma));
            ok = n.has_value() && (v + 1 == values.size()) == (comma == std::string_view::npos);
            if (n) values[v] = *n;
            rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
        }
        if (!ok) {
            warn("'" + std::string(entry) + "' is not <name>:<x>,<y>,<size>");
            continue;
        }
        layout[*control] = clamp_place(*control, {values[0], values[1], values[2]});
    }
    return layout;
}

Circle circle_of(const Place& place, const Area& area, float scale) {
    return {area.x + place.x * area.w, area.y + place.y * area.h, 0.5f * place.size * area.h * scale};
}

std::optional<Control> hit_test(const Layout& layout, const Area& area, float scale, std::uint32_t visible, float px,
                                float py, float slop) {
    std::optional<Control> best;
    float best_distance = 0.0f;
    for (int i = 0; i < kControlCount; ++i) {
        const Control c = static_cast<Control>(i);
        if (c == Control::Stick || (visible & bit(c)) == 0) continue;
        const Circle circle = circle_of(layout[c], area, scale);
        const float d = std::hypot(px - circle.x, py - circle.y);
        if (d > circle.r + slop) continue;
        if (!best || d - circle.r < best_distance) {
            best = c;
            best_distance = d - circle.r;
        }
    }
    return best;
}

bool in_stick_zone(const Area& area, float px, float py) {
    return px >= area.x - 1.0f && px < area.x + 0.45f * area.w && py >= area.y + 0.25f * area.h &&
           py <= area.y + area.h + 1.0f;
}

}  // namespace sh::input::touch
