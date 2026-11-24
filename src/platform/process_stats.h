/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

// What the operating system and the GPU driver tell about the running process, for the profiler overlay.
// Every reader returns a negative value where the platform does not answer.

#include <cstdint>

namespace sh::platform {

struct ProcessStats {
    double processCpuSec = -1.0;  // user + kernel time of all threads since the process started
    double threadCpuSec = -1.0;   // the same for the calling thread
    std::int64_t rssKb = -1;      // resident set (working set on Windows, the memory footprint on macOS)
};

ProcessStats read_process_stats();

struct GpuUsage {
    int busyPercent = -1;
    std::int64_t vramUsedKb = -1;
    std::int64_t vramTotalKb = -1;
};

// Adapter-wide load from the vendor's own interface: NVML when the driver's library is already installed,
// the amdgpu sysfs nodes on Linux, the IOAccelerator statistics on macOS (memory: the unified memory). Loads
// nothing until the first call.
GpuUsage read_gpu_usage();

}  // namespace sh::platform
