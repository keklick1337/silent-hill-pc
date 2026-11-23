/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "platform/process_stats.h"

#include <SDL3/SDL.h>

#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// Version 2 binds GetProcessMemoryInfo to kernel32, so psapi.dll is not linked.
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>
#else
#include <time.h>
#include <unistd.h>
#endif

namespace sh::platform {

namespace {

#ifdef _WIN32
double filetime_sec(const FILETIME& t) {
    const std::uint64_t ticks = (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    return static_cast<double>(ticks) * 1.0e-7;
}
#else
double clock_sec(clockid_t id) {
    timespec ts{};
    if (clock_gettime(id, &ts) != 0) return -1.0;
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1.0e-9;
}

bool read_number(const char* path, long long* out) {
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    const bool ok = std::fscanf(f, "%lld", out) == 1;
    std::fclose(f);
    return ok;
}
#endif

// The slice of NVML the overlay reads, bound by name at run time: the library ships with the NVIDIA driver
// and has no development package on a player's machine.
struct Nvml {
    struct Utilization {
        unsigned gpu, memory;
    };
    struct Memory {
        unsigned long long total, free, used;
    };
    using Handle = void*;
    int (*init)() = nullptr;
    int (*deviceByIndex)(unsigned, Handle*) = nullptr;
    int (*utilization)(Handle, Utilization*) = nullptr;
    int (*memory)(Handle, Memory*) = nullptr;
    Handle device = nullptr;
    bool tried = false;

    bool ready() {
        if (tried) return device != nullptr;
        tried = true;
#ifdef _WIN32
        const char* const names[] = {"nvml.dll"};
#else
        const char* const names[] = {"libnvidia-ml.so.1", "libnvidia-ml.so"};
#endif
        SDL_SharedObject* lib = nullptr;
        for (const char* name : names) {
            lib = SDL_LoadObject(name);
            if (lib) break;
        }
        if (lib) {
            init = reinterpret_cast<int (*)()>(SDL_LoadFunction(lib, "nvmlInit_v2"));
            deviceByIndex = reinterpret_cast<int (*)(unsigned, Handle*)>(
                SDL_LoadFunction(lib, "nvmlDeviceGetHandleByIndex_v2"));
            utilization = reinterpret_cast<int (*)(Handle, Utilization*)>(
                SDL_LoadFunction(lib, "nvmlDeviceGetUtilizationRates"));
            memory =
                reinterpret_cast<int (*)(Handle, Memory*)>(SDL_LoadFunction(lib, "nvmlDeviceGetMemoryInfo"));
            Handle found = nullptr;
            if (init && deviceByIndex && utilization && memory && init() == 0 && deviceByIndex(0, &found) == 0)
                device = found;
        }
        // A missing library is the normal case on other vendors' drivers, not an error to leave behind.
        SDL_ClearError();
        return device != nullptr;
    }
};

Nvml g_nvml;

}  // namespace

ProcessStats read_process_stats() {
    ProcessStats out;
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        out.processCpuSec = filetime_sec(kernel) + filetime_sec(user);
    if (GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
        out.threadCpuSec = filetime_sec(kernel) + filetime_sec(user);
    PROCESS_MEMORY_COUNTERS mem{};
    mem.cb = sizeof(mem);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &mem, sizeof(mem)))
        out.rssKb = static_cast<std::int64_t>(mem.WorkingSetSize / 1024);
#else
    out.processCpuSec = clock_sec(CLOCK_PROCESS_CPUTIME_ID);
    out.threadCpuSec = clock_sec(CLOCK_THREAD_CPUTIME_ID);
    if (FILE* f = std::fopen("/proc/self/statm", "r")) {
        long long total = 0, resident = 0;
        if (std::fscanf(f, "%lld %lld", &total, &resident) == 2)
            out.rssKb = static_cast<std::int64_t>(resident) * (sysconf(_SC_PAGESIZE) / 1024);
        std::fclose(f);
    }
#endif
    return out;
}

GpuUsage read_gpu_usage() {
    GpuUsage out;
    if (g_nvml.ready()) {
        Nvml::Utilization u{};
        if (g_nvml.utilization(g_nvml.device, &u) == 0) out.busyPercent = static_cast<int>(u.gpu);
        Nvml::Memory m{};
        if (g_nvml.memory(g_nvml.device, &m) == 0) {
            out.vramUsedKb = static_cast<std::int64_t>(m.used / 1024);
            out.vramTotalKb = static_cast<std::int64_t>(m.total / 1024);
        }
        return out;
    }
#ifndef _WIN32
    for (int card = 0; card < 4; ++card) {
        char path[96];
        long long v = 0;
        std::snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/gpu_busy_percent", card);
        if (!read_number(path, &v)) continue;
        out.busyPercent = static_cast<int>(v);
        std::snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/mem_info_vram_used", card);
        if (read_number(path, &v)) out.vramUsedKb = v / 1024;
        std::snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/mem_info_vram_total", card);
        if (read_number(path, &v)) out.vramTotalKb = v / 1024;
        break;
    }
#endif
    return out;
}

}  // namespace sh::platform
