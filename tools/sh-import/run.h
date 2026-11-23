#pragma once

#include <atomic>
#include <cstdio>
#include <iosfwd>
#include <span>
#include <string>

namespace sh::import_tool {

// The whole tool behind main(): parses `args` (without the program name), reads --config, builds the converter list,
// runs the import and prints the summary to `out`. Warnings and errors go to `err`, the progress line to `progress`
// (null: no progress). `cancel` is polled by the import. Returns 0 on success, 1 when the import failed or was
// cancelled, 2 for a usage error.
int run(std::span<const std::string> args, std::ostream& out, std::ostream& err, std::FILE* progress,
        const std::atomic<bool>* cancel = nullptr);

}  // namespace sh::import_tool
