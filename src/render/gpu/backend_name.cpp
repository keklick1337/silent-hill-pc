#include "render/gpu/backend_name.h"

#include <array>

namespace sh::gpu {

namespace {

struct NameEntry {
    std::string_view name;
    BackendKind backend;
};

constexpr std::array kNames{
    NameEntry{"gl", BackendKind::Gl},
    NameEntry{"opengl", BackendKind::Gl},
    NameEntry{"vulkan", BackendKind::Vulkan},
    NameEntry{"vk", BackendKind::Vulkan},
    NameEntry{"sdlgpu", BackendKind::SdlGpu},
    NameEntry{"sdl_gpu", BackendKind::SdlGpu},
};

constexpr char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equal_nocase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

}  // namespace

std::optional<BackendKind> backend_from_name(std::string_view name) {
    for (const NameEntry& e : kNames)
        if (equal_nocase(name, e.name)) return e.backend;
    return std::nullopt;
}

BackendKind resolve_backend(std::string_view name) { return backend_from_name(name).value_or(BackendKind::Gl); }

std::string_view backend_name(BackendKind backend) {
    switch (backend) {
        case BackendKind::Gl: return "gl";
        case BackendKind::Vulkan: return "vulkan";
        case BackendKind::SdlGpu: return "sdlgpu";
    }
    return "gl";
}

std::string_view backend_description(BackendKind backend) {
    switch (backend) {
        case BackendKind::Gl: return "OpenGL 3.3 (native)";
        case BackendKind::Vulkan: return "Vulkan (native)";
        case BackendKind::SdlGpu: return "Vulkan (SDL3 GPU)";
    }
    return "OpenGL 3.3 (native)";
}

}  // namespace sh::gpu
