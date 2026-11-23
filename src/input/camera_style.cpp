// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "input/camera_style.h"

#include <array>
#include <cctype>
#include <cstdlib>

namespace sh::input {
namespace {

struct StyleText {
    const char* id;
    const char* name;
};

constexpr std::array<StyleText, kCameraStyles> kStyleText = {{
    {"classic", "Classic (Default)"},
    {"tps", "Thirdperson Shooter"},
    {"ots", "Over the Shoulder"},
    {"fps", "First Person"},
}};

constexpr std::array<CameraStyle, 3> kRound = {CameraStyle::Classic, CameraStyle::ThirdPerson,
                                               CameraStyle::FirstPerson};

constexpr int kLastShape = 3;

}  // namespace

CameraStyle camera_style_from(int value) {
    return value >= 0 && value < kCameraStyles ? static_cast<CameraStyle>(value) : CameraStyle::Classic;
}

const char* camera_style_id(CameraStyle style) {
    const int i = static_cast<int>(style);
    return i >= 0 && i < kCameraStyles ? kStyleText[static_cast<std::size_t>(i)].id : "";
}

CameraStyle next_camera_style(CameraStyle style, int direction) {
    if (style == CameraStyle::OverShoulder) {
        style = CameraStyle::ThirdPerson;
    }
    std::size_t at = 0;
    for (std::size_t i = 0; i < kRound.size(); ++i) {
        if (kRound[i] == style) {
            at = i;
        }
    }
    const std::size_t step = direction < 0 ? kRound.size() - 1 : 1;
    return kRound[(at + step) % kRound.size()];
}

std::string camera_style_list() {
    std::string list;
    for (const CameraStyle style : kRound) {
        const StyleText& text = kStyleText[static_cast<std::size_t>(style)];
        if (!list.empty()) {
            list += '|';
        }
        list += text.id;
        list += ':';
        list += text.name;
    }
    return list;
}

Crosshair next_crosshair(Crosshair now) {
    if (!now.on) {
        return {true, 0};
    }
    if (now.shape < kLastShape) {
        return {true, now.shape + 1};
    }
    return {false, now.shape};
}

const char* crosshair_name(Crosshair mark) {
    static constexpr const char* kShapes[] = {"cross (+)", "dot", "circle", "dashes"};
    return mark.on ? kShapes[mark.shape & kLastShape] : "off";
}

int mouse_button_of_bind(std::string_view bind) {
    constexpr std::string_view kWord = "mouse";
    if (bind.size() < kWord.size()) {
        return 0;
    }
    for (std::size_t i = 0; i < kWord.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(bind[i])) != kWord[i]) {
            return 0;
        }
    }
    // The number is read as far as it goes: "Mouse2" and "mouse 2" are both the second button.
    const int n = std::atoi(std::string(bind.substr(kWord.size())).c_str());
    return n >= 1 && n <= 5 ? n : 0;
}

}  // namespace sh::input
