#include "sh-import/summary.h"

#include "sh-import/progress.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <system_error>

namespace sh::import_tool {

namespace fs = std::filesystem;

std::vector<DirSize> directory_sizes(const fs::path& dir) {
    std::error_code ec;
    std::map<std::string, DirSize> by_name;
    DirSize loose;
    loose.name = "(files)";

    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code entry_ec;
        if (!it->is_regular_file(entry_ec)) {
            continue;
        }
        const std::uint64_t size = it->file_size(entry_ec);
        if (entry_ec) {
            continue;
        }
        const fs::path relative = it->path().lexically_relative(dir);
        DirSize* target = &loose;
        if (std::distance(relative.begin(), relative.end()) > 1) {
            DirSize& slot = by_name[relative.begin()->string()];
            slot.name = relative.begin()->string();
            target = &slot;
        }
        ++target->files;
        target->bytes += size;
    }

    std::vector<DirSize> result;
    for (auto& [name, size] : by_name) {
        result.push_back(std::move(size));
    }
    if (loose.files > 0) {
        result.push_back(std::move(loose));
    }
    return result;
}

std::string format_bytes(std::uint64_t bytes) {
    static constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[48];
    if (unit == 0) {
        std::snprintf(buffer, sizeof buffer, "%llu B", static_cast<unsigned long long>(bytes));
    } else {
        std::snprintf(buffer, sizeof buffer, "%.1f %s", value, units[unit]);
    }
    return buffer;
}

namespace {

std::string pad(std::string text, std::size_t width) {
    if (text.size() < width) {
        text.insert(0, width - text.size(), ' ');
    }
    return text;
}

std::string left(std::string text, std::size_t width) {
    if (text.size() < width) {
        text.append(width - text.size(), ' ');
    }
    return text;
}

void append_table(std::string& out, const std::string& title, const std::vector<DirSize>& table) {
    if (table.empty()) {
        return;
    }
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    std::size_t width = 8;
    for (const DirSize& d : table) {
        files += d.files;
        bytes += d.bytes;
        width = std::max(width, d.name.size());
    }
    out += title + "\n";
    for (const DirSize& d : table) {
        out += "  " + left(d.name, width) + pad(std::to_string(d.files), 8) + " files" + pad(format_bytes(d.bytes), 12) + "\n";
    }
    out += "  " + left("total", width) + pad(std::to_string(files), 8) + " files" + pad(format_bytes(bytes), 12) + "\n";
}

}  // namespace

std::string format_failures(const sh::import::Report& report, std::size_t limit) {
    std::string out;
    for (std::size_t i = 0; i < report.failures.size() && i < limit; ++i) {
        const auto& f = report.failures[i];
        out += "  " + f.path + " [" + f.converter + "]: " + f.message + "\n";
    }
    if (report.failures.size() > limit) {
        out += "  ... and " + std::to_string(report.failures.size() - limit) + " more\n";
    }
    return out;
}

std::string format_summary(const sh::import::Report& report, const std::vector<DirSize>& raw,
                           const std::vector<DirSize>& converted, double seconds) {
    std::string out;
    if (!report.image_sha256.empty()) {
        out += "image sha256:   " + report.image_sha256 + "\n";
    }
    out += "raw:            " + std::to_string(report.raw_files) + " files, " + format_bytes(report.raw_bytes) + "\n";
    out += "crc32 checked:  " + std::to_string(report.crc_checked) + " files\n";
    out += "converted:      " + std::to_string(report.converted_files) + " files, " +
           format_bytes(report.converted_bytes) + "\n";
    for (const auto& c : report.converters) {
        out += "  " + left(c.name, 6) + " accepted " + pad(std::to_string(c.accepted), 5) + "  outputs " +
               pad(std::to_string(c.outputs), 6);
        if (c.lossless) {
            out += "  round trips " + std::to_string(c.round_trips);
        }
        out += "\n";
    }
    out += "failures:       " + std::to_string(report.failures.size()) + "\n";
    out += format_failures(report, 100);
    append_table(out, "raw/ by directory:", raw);
    append_table(out, "convert/ by directory:", converted);
    out += "time:           " + format_duration(seconds) + "\n";
    return out;
}

}  // namespace sh::import_tool
