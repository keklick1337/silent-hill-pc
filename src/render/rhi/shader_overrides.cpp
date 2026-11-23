// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "render/rhi/shader_overrides.h"

#include <cstdio>
#include <mutex>
#include <utility>

namespace sh::rhi {

namespace {

struct State {
    std::mutex mutex;
    ShaderFileReader reader;
    ShaderLog log;
};

State& state() {
    static State s;
    return s;
}

bool file_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
           c == '.';
}

}  // namespace

void set_shader_file_reader(ShaderFileReader reader) {
    State& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);
    s.reader = std::move(reader);
}

void set_shader_log(ShaderLog log) {
    State& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);
    s.log = std::move(log);
}

std::string shader_file_name(std::string_view shader_name) {
    const std::size_t note = shader_name.find(" (");
    if (note != std::string_view::npos) {
        shader_name = shader_name.substr(0, note);
    }
    // Only a plain file name with an extension: "scene.vert", not a pipeline's "far city".
    if (shader_name.empty() || shader_name.find('.') == std::string_view::npos || shader_name.front() == '.') {
        return {};
    }
    for (const char c : shader_name) {
        if (!file_char(c)) {
            return {};
        }
    }
    return std::string(shader_name);
}

std::optional<std::string> shader_override(std::string_view shader_name, std::string_view suffix) {
    const std::string file = shader_file_name(shader_name);
    if (file.empty()) {
        return std::nullopt;
    }
    ShaderFileReader reader;
    {
        State& s = state();
        const std::lock_guard<std::mutex> lock(s.mutex);
        reader = s.reader;
    }
    if (!reader) {
        return std::nullopt;
    }
    return reader("shaders/" + file + std::string(suffix));
}

void shader_log(int level, const std::string& message) {
    ShaderLog log;
    {
        State& s = state();
        const std::lock_guard<std::mutex> lock(s.mutex);
        log = s.log;
    }
    if (log) {
        log(level, message);
    } else {
        std::fprintf(stderr, "%s[RHI] %s\n", level != 0 ? "warning: " : "", message.c_str());
    }
}

}  // namespace sh::rhi
