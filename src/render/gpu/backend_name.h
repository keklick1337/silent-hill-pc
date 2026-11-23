// The `renderer` config key of the engine's primitive renderer (RENDER.md section 3): there are three backends,
// the GL 3.3 core RHI backend, the engine's own Vulkan backend and the SDL3 GPU (Vulkan) one. A config naming anything else (gles, d3d11, warp,
// software, angle-vulkan, a typo) or an empty value runs on GL, because a bad config value must never stop the
// game from booting.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace sh::gpu {

enum class BackendKind : std::uint8_t { Gl, Vulkan, SdlGpu };

// Exact names and the aliases people type ("opengl", "vk"), case-insensitive; nullopt for anything else.
std::optional<BackendKind> backend_from_name(std::string_view name);

// What a config value runs on: backend_from_name, or Gl.
BackendKind resolve_backend(std::string_view name);

std::string_view backend_name(BackendKind backend);         // "gl", "vulkan", "sdlgpu": what the config key spells
std::string_view backend_description(BackendKind backend);  // for the log and the settings screen

}  // namespace sh::gpu
