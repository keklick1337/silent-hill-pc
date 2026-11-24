// SPDX-License-Identifier: GPL-3.0-or-later
//
// macOS: ncnn's own Vulkan loader expects MoltenVK linked in (it calls vkGetInstanceProcAddr directly). The package
// ships no Vulkan, so this entry point opens MoltenVK or the Vulkan loader at run time, as ncnn does by itself on
// the other platforms. Without either it hands out global functions that fail: ncnn then finds no GPU, and the tool
// says so instead of crashing.

#include <dlfcn.h>

#include <cstdint>
#include <cstring>
#include <string>

#include <mach-o/dyld.h>

namespace {

using VoidFunction = void (*)();
using GetInstanceProcAddr = VoidFunction (*)(void* instance, const char* name);

constexpr std::int32_t kErrorInitializationFailed = -3;  // VK_ERROR_INITIALIZATION_FAILED

std::int32_t fail_enumerate(const char*, std::uint32_t* count, void*) {
    if (count != nullptr) *count = 0;
    return kErrorInitializationFailed;
}
std::int32_t fail_create_instance(const void*, const void*, void*) { return kErrorInitializationFailed; }
std::int32_t fail_enumerate_layers(std::uint32_t* count, void*) {
    if (count != nullptr) *count = 0;
    return kErrorInitializationFailed;
}

std::string exe_dir() {
    char path[4096];
    std::uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) return {};
    std::string dir(path);
    const std::size_t slash = dir.find_last_of('/');
    return slash == std::string::npos ? std::string() : dir.substr(0, slash + 1);
}

GetInstanceProcAddr driver() {
    static const GetInstanceProcAddr found = [] {
        // Beside the tool first (a user's own copy), then the dynamic loader's paths, then Homebrew's (molten-vk,
        // vulkan-loader), which those paths do not include.
        const std::string here = exe_dir();
        const std::string candidates[] = {
            here + "libvulkan.1.dylib",
            here + "libMoltenVK.dylib",
            "libvulkan.1.dylib",
            "libMoltenVK.dylib",
            "/opt/homebrew/lib/libvulkan.1.dylib",
            "/opt/homebrew/lib/libMoltenVK.dylib",
            "/usr/local/lib/libvulkan.1.dylib",
            "/usr/local/lib/libMoltenVK.dylib",
        };
        for (const std::string& name : candidates) {
            if (void* library = dlopen(name.c_str(), RTLD_LOCAL | RTLD_NOW)) {
                if (void* entry = dlsym(library, "vkGetInstanceProcAddr")) {
                    return reinterpret_cast<GetInstanceProcAddr>(entry);
                }
                dlclose(library);
            }
        }
        return static_cast<GetInstanceProcAddr>(nullptr);
    }();
    return found;
}

}  // namespace

extern "C" VoidFunction vkGetInstanceProcAddr(void* instance, const char* name) {
    if (const GetInstanceProcAddr real = driver()) return real(instance, name);
    if (name == nullptr) return nullptr;
    if (std::strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0) {
        return reinterpret_cast<VoidFunction>(&fail_enumerate);
    }
    if (std::strcmp(name, "vkCreateInstance") == 0) return reinterpret_cast<VoidFunction>(&fail_create_instance);
    if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0) {
        return reinterpret_cast<VoidFunction>(&fail_enumerate_layers);
    }
    return nullptr;
}
