// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Shader sources from mods: a mod's shaders/<file> replaces the built-in shader of that
// file name ("shaders/scene.frag", "shaders/post.frag"; the SDL3 GPU backend reads SPIR-V from
// "shaders/<file>.spv"), the last mod in load order wins. The backends ask here before they compile a built-in
// shader; a mod shader that does not compile or link is dropped with a warning and the built-in one is used.
// The RHI knows nothing of mods: the game installs the reader.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace sh::rhi {

// "shaders/scene.frag" -> the file's bytes, or nothing when no mod has it.
using ShaderFileReader = std::function<std::optional<std::string>(std::string_view relative)>;
// Where the warnings and notes go (level 0 info, 1 warning); stderr when unset.
using ShaderLog = std::function<void(int level, const std::string& message)>;

void set_shader_file_reader(ShaderFileReader reader);
void set_shader_log(ShaderLog log);

// The file a shader is looked up by: the shader's name up to a " (" note ("prim_batch.frag (far)" ->
// "prim_batch.frag"); empty for a name that is not a file name.
std::string shader_file_name(std::string_view shader_name);

// The mod's text for the shader `shader_name` ("shaders/<file>" + `suffix`: "" for GLSL, ".spv" for SPIR-V).
std::optional<std::string> shader_override(std::string_view shader_name, std::string_view suffix);

void shader_log(int level, const std::string& message);

}  // namespace sh::rhi
