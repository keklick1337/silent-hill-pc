// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/screen_kit/text_layout.h"

#include "i18n/core/unicode.h"

#include <algorithm>

namespace sh::screen_kit {

namespace {

std::string trim_end(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

char32_t first_char(std::string_view s) {
    std::size_t pos = 0;
    return s.empty() ? U'\0' : sh::i18n::decode_utf8(s, pos);
}

}  // namespace

std::vector<std::string> break_pieces(std::string_view text) {
    std::vector<std::string> pieces;
    std::string current;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t start = i;
        const char32_t cp = sh::i18n::decode_utf8(text, i);
        const std::string_view ch = text.substr(start, i - start);
        if (cp == U'\n') {
            if (!current.empty()) pieces.push_back(std::move(current));
            current.clear();
            pieces.emplace_back("\n");
        } else if (cp == U' ') {
            current += ch;
            pieces.push_back(std::move(current));
            current.clear();
        } else if (sh::i18n::breaks_anywhere(cp)) {
            if (!current.empty()) pieces.push_back(std::move(current));
            pieces.emplace_back(ch);
            current.clear();
        } else {
            current += ch;
        }
    }
    if (!current.empty()) pieces.push_back(std::move(current));
    return pieces;
}

std::vector<std::string> wrap_lines(std::string_view text, float max_width, const Measure& measure) {
    // A word wider than a line (a long path) breaks after its separators, else between any two characters.
    std::vector<std::string> pieces;
    for (std::string& piece : break_pieces(text)) {
        if (piece == "\n" || measure(trim_end(piece)) <= max_width) {
            pieces.push_back(std::move(piece));
            continue;
        }
        std::vector<std::string> parts(1);
        for (char c : piece) {
            parts.back() += c;
            if (c == '/' || c == '\\') parts.emplace_back();
        }
        for (std::string& part : parts) {
            if (part.empty()) continue;
            if (measure(trim_end(part)) <= max_width) {
                pieces.push_back(std::move(part));
                continue;
            }
            for (std::size_t i = 0; i < part.size();) {
                const std::size_t start = i;
                sh::i18n::decode_utf8(part, i);
                pieces.push_back(part.substr(start, i - start));
            }
        }
    }
    std::vector<std::string> lines;
    std::string line;
    std::size_t last = 0;  // where the line's last piece starts
    bool open = false;     // a line was started, even an empty one after a forced break
    for (const std::string& piece : pieces) {
        if (piece == "\n") {
            lines.push_back(trim_end(std::move(line)));
            line.clear();
            last = 0;
            open = false;
            continue;
        }
        const std::string candidate = line + piece;
        open = true;
        if (line.empty() || measure(trim_end(candidate)) <= max_width) {
            last = line.size();
            line = candidate;
        } else if (!sh::i18n::no_line_start(first_char(piece))) {
            lines.push_back(trim_end(std::move(line)));
            line = piece;
            last = 0;
        } else if (last > 0) {
            // A closing mark may not start a line: the piece before it goes down with it.
            lines.push_back(trim_end(line.substr(0, last)));
            line = line.substr(last) + piece;
            last = 0;
        } else {
            line = candidate;  // nothing to take along: the mark stays, a little over the width
        }
    }
    if (open || !line.empty()) lines.push_back(trim_end(std::move(line)));
    return lines;
}

View fit_view(int pixel_w, int pixel_h, Insets insets, std::span<const Size> designs, float min_scale,
              float max_scale) {
    View v;
    const float w = std::max(1.0f, static_cast<float>(pixel_w) - insets.left - insets.right);
    const float h = std::max(1.0f, static_cast<float>(pixel_h) - insets.top - insets.bottom);
    float scale = 0.0f;
    for (const Size& d : designs)
        scale = std::max(scale, std::min(w / std::max(1.0f, d.width), h / std::max(1.0f, d.height)));
    scale = std::min(scale, std::max(min_scale, max_scale));
    scale = std::max(scale, min_scale);
    v.scale = std::max(0.01f, scale);
    v.origin_x = insets.left;
    v.origin_y = insets.top;
    v.width = w / v.scale;
    v.height = h / v.scale;
    return v;
}

float clamp_scroll(float offset, float content, float viewport) {
    return std::clamp(offset, 0.0f, std::max(0.0f, content - viewport));
}

std::vector<ButtonSlot> lay_out_buttons(std::span<const float> widths, float left, float right, float gap,
                                        float min_width) {
    std::vector<ButtonSlot> slots;
    if (widths.empty()) return slots;
    const float each = std::max(min_width, *std::max_element(widths.begin(), widths.end()));
    const float n = static_cast<float>(widths.size());
    const float total = each * n + gap * (n - 1.0f);
    if (total <= right - left) {
        float x = (left + right - total) * 0.5f;
        for (std::size_t i = 0; i < widths.size(); ++i) {
            slots.push_back({x, x + each, 0});
            x += each + gap;
        }
        return slots;
    }
    for (std::size_t i = 0; i < widths.size(); ++i) slots.push_back({left, right, static_cast<int>(i)});
    return slots;
}

}  // namespace sh::screen_kit
