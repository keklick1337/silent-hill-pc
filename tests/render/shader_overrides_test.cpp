// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Mod shaders (render/rhi/shader_overrides.h): which file a built-in shader is looked up by, and that the lookup
// goes through the reader the game installs (the backends' compile fallback needs a GPU and is checked in a game run).

#include "framework/sh_test.h"
#include "render/rhi/shader_overrides.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rhi = sh::rhi;

SH_TEST(shader_override_file_names) {
    SH_CHECK(rhi::shader_file_name("scene.frag") == "scene.frag");
    SH_CHECK(rhi::shader_file_name("prim_batch.frag (far)") == "prim_batch.frag");
    SH_CHECK(rhi::shader_file_name("far city").empty());           // a pipeline's name, not a file
    SH_CHECK(rhi::shader_file_name("../scene.frag").empty());      // never leaves shaders/
    SH_CHECK(rhi::shader_file_name("a/b.frag").empty());
    SH_CHECK(rhi::shader_file_name(".frag").empty());
    SH_CHECK(rhi::shader_file_name("").empty());
}

SH_TEST(shader_override_reads_through_the_installed_reader) {
    std::vector<std::string> asked;
    const std::map<std::string, std::string> files{{"shaders/post.frag", "void main() {}"},
                                                   {"shaders/post.frag.spv", "spv"}};
    SH_CHECK(!rhi::shader_override("post.frag", "").has_value());  // no reader: the built-in
    rhi::set_shader_file_reader([&](std::string_view relative) -> std::optional<std::string> {
        asked.emplace_back(relative);
        const auto it = files.find(std::string(relative));
        return it != files.end() ? std::optional<std::string>(it->second) : std::nullopt;
    });
    SH_CHECK(rhi::shader_override("post.frag", "") == std::optional<std::string>("void main() {}"));
    SH_CHECK(rhi::shader_override("post.frag", ".spv") == std::optional<std::string>("spv"));
    SH_CHECK(!rhi::shader_override("scene.vert", "").has_value());
    SH_CHECK(!rhi::shader_override("far city", "").has_value());
    SH_CHECK((asked == std::vector<std::string>{"shaders/post.frag", "shaders/post.frag.spv", "shaders/scene.vert"}));

    std::vector<std::string> logged;
    rhi::set_shader_log([&](int level, const std::string& m) { logged.push_back(std::to_string(level) + m); });
    rhi::shader_log(1, "x");
    SH_CHECK((logged == std::vector<std::string>{"1x"}));
    rhi::set_shader_log({});
    rhi::set_shader_file_reader({});
}
