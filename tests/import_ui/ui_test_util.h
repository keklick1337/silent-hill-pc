#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

// Small fake images in .tmp/import-ui/tests: only the size and the first sector matter to the checks.
namespace sh::import::ui::testing {

inline std::filesystem::path work_dir(const std::string& name) {
    std::filesystem::path dir = std::filesystem::path(SH_PROJECT_ROOT) / ".tmp" / "import-ui" / "tests" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
#ifdef _WIN32
    // A cross-built runner gets a Unix SH_PROJECT_ROOT with no drive letter; check_image()
    // returns absolute paths, so the expected paths have to carry the drive as well.
    dir = std::filesystem::absolute(dir);
#endif
    return dir;
}

inline std::filesystem::path write_file(const std::filesystem::path& path, std::size_t size, bool cd_sync) {
    std::vector<char> bytes(size, 0);
    if (cd_sync && size >= 12) {
        for (std::size_t i = 1; i <= 10; ++i) {
            bytes[i] = static_cast<char>(0xFF);
        }
    }
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path;
}

inline std::filesystem::path write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    return path;
}

constexpr std::size_t kRaw = 2352;
constexpr std::size_t kCooked = 2048;

}  // namespace sh::import::ui::testing
