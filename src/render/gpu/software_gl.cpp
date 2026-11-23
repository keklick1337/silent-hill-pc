#include "render/gpu/software_gl.h"

#include "engine/log/sh_log.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace sh::gpu {

namespace {

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool contains_nocase(std::string_view text, std::string_view word) {
    for (std::size_t i = 0; i + word.size() <= text.size(); ++i) {
        std::size_t j = 0;
        while (j < word.size() && lower(text[i + j]) == word[j]) ++j;
        if (j == word.size()) return true;
    }
    return false;
}

#ifdef _WIN32
std::wstring widen(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    w.pop_back();
    return w;
}

// Mesa picks d3d12 or zink before llvmpipe when they load; neither is shipped, and a driver that half-works is
// worse than the CPU. Mesa reads GALLIUM_DRIVER through the UCRT, which keeps its own copy of the environment once
// it is initialised, so the copy is set as well as the process block.
void select_llvmpipe() {
    SetEnvironmentVariableW(L"GALLIUM_DRIVER", L"llvmpipe");
    if (HMODULE ucrt = GetModuleHandleW(L"ucrtbase.dll")) {
        using PutEnv = int(__cdecl*)(const wchar_t*, const wchar_t*);
        if (auto put = reinterpret_cast<PutEnv>(reinterpret_cast<void*>(GetProcAddress(ucrt, "_wputenv_s"))))
            put(L"GALLIUM_DRIVER", L"llvmpipe");
    }
}
#endif

}  // namespace

bool is_software_adapter(std::string_view adapter) {
    static constexpr std::array<std::string_view, 7> kNames = {
        "llvmpipe", "softpipe", "lavapipe", "swiftshader", "microsoft basic render", "gdi generic", "swrast"};
    for (std::string_view name : kNames)
        if (contains_nocase(adapter, name)) return true;
    return false;
}

void request_system_software_gl() {
#ifndef _WIN32
    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
#endif
}

bool use_bundled_software_gl() {
#ifdef _WIN32
    const char* base = SDL_GetBasePath();
    if (!base) return false;
    const std::string dir = std::string(base) + "software_gl\\";
    const std::string library = dir + "opengl32.dll";
    const std::wstring wide = widen(library);
    if (GetFileAttributesW(wide.c_str()) == INVALID_FILE_ATTRIBUTES) {
        SH_WARN("[GPU] no OpenGL 3.3 driver, and no %s to draw on the CPU", library.c_str());
        return false;
    }
    const bool chosen = GetEnvironmentVariableW(L"GALLIUM_DRIVER", nullptr, 0) != 0;
    if (!chosen) select_llvmpipe();
    // The folder is not on the DLL search path: the altered search path finds libgallium_wgl.dll beside
    // opengl32.dll. The module stays loaded for the life of the process, so SDL's load by the same path returns it.
    if (!LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) {
        SH_WARN("[GPU] %s did not load (error %lu)", library.c_str(), static_cast<unsigned long>(GetLastError()));
        return false;
    }
    if (!chosen) select_llvmpipe();
    SDL_SetHint(SDL_HINT_OPENGL_LIBRARY, library.c_str());
    SH_LOG("[GPU] rendering on the CPU: Mesa llvmpipe from %s", dir.c_str());
    return true;
#else
    return false;
#endif
}

}  // namespace sh::gpu
