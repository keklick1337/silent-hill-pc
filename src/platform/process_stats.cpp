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
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
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

#ifdef __APPLE__
// What Activity Monitor shows as the process's memory: resident pages plus compressed ones, the GPU's included.
std::int64_t footprint_kb() {
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return -1;
    return static_cast<std::int64_t>(info.phys_footprint / 1024);
}

std::int64_t number_of(CFDictionaryRef dict, const char* key) {
    const CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
    const void* value = name ? CFDictionaryGetValue(dict, name) : nullptr;
    if (name) CFRelease(name);
    long long out = -1;
    if (value == nullptr || CFGetTypeID(value) != CFNumberGetTypeID() ||
        !CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberLongLongType, &out))
        return -1;
    return out;
}

// The GPU's own statistics (IOAccelerator "PerformanceStatistics", the source of Activity Monitor's GPU history).
// Apple silicon has no memory of the GPU's own: the total is the unified memory, as MoltenVK reports its heap.
GpuUsage apple_gpu_usage() {
    GpuUsage out;
    io_iterator_t it = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &it) != KERN_SUCCESS)
        return out;
    for (io_object_t service = IOIteratorNext(it); service != IO_OBJECT_NULL; service = IOIteratorNext(it)) {
        const CFTypeRef stats =
            IORegistryEntryCreateCFProperty(service, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0);
        IOObjectRelease(service);
        if (stats == nullptr) continue;
        if (CFGetTypeID(stats) == CFDictionaryGetTypeID() && out.busyPercent < 0) {
            const CFDictionaryRef dict = static_cast<CFDictionaryRef>(stats);
            const std::int64_t busy = number_of(dict, "Device Utilization %");
            const std::int64_t used = number_of(dict, "In use system memory");
            if (busy >= 0) out.busyPercent = static_cast<int>(busy);
            if (used >= 0) out.vramUsedKb = used / 1024;
        }
        CFRelease(stats);
    }
    IOObjectRelease(it);
    std::uint64_t memory = 0;
    std::size_t size = sizeof(memory);
    if (out.vramUsedKb >= 0 && sysctlbyname("hw.memsize", &memory, &size, nullptr, 0) == 0)
        out.vramTotalKb = static_cast<std::int64_t>(memory / 1024);
    return out;
}
#endif

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
#ifdef __APPLE__
    out.rssKb = footprint_kb();
#endif
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
#ifdef __APPLE__
    return apple_gpu_usage();
#endif
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
