#include "import/index.h"

#include <array>
#include <charconv>
#include <cstddef>

namespace sh::import {

namespace {

constexpr std::string_view kHeader = "path\tsize\tcrc32\tsource\tformat\tconverted";
constexpr std::size_t kColumns = 6;

std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find(separator, start);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

}  // namespace

std::string crc32_hex(std::uint32_t value) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string text(8, '0');
    for (std::size_t i = 0; i < 8; ++i) {
        text[7 - i] = kDigits[(value >> (4 * i)) & 15];
    }
    return text;
}

std::string format_index(const std::vector<IndexRow>& rows) {
    std::string text(kHeader);
    text += '\n';
    for (const IndexRow& row : rows) {
        text += row.path;
        text += '\t';
        text += std::to_string(row.size);
        text += '\t';
        text += crc32_hex(row.crc32);
        text += '\t';
        text += row.source;
        text += '\t';
        text += row.format;
        text += '\t';
        for (std::size_t i = 0; i < row.converted.size(); ++i) {
            if (i > 0) {
                text += ';';
            }
            text += row.converted[i];
        }
        text += '\n';
    }
    return text;
}

Result<std::vector<IndexRow>> parse_index(std::string_view text) {
    using R = Result<std::vector<IndexRow>>;
    if (text.empty() || text.back() != '\n') {
        return R::fail("index.tsv does not end with a line feed");
    }
    text.remove_suffix(1);
    const std::vector<std::string_view> lines = split(text, '\n');
    if (lines[0] != kHeader) {
        return R::fail("index.tsv has an unexpected header");
    }

    std::vector<IndexRow> rows;
    rows.reserve(lines.size() - 1);
    for (std::size_t n = 1; n < lines.size(); ++n) {
        const std::vector<std::string_view> columns = split(lines[n], '\t');
        if (columns.size() != kColumns) {
            return R::fail("index.tsv line " + std::to_string(n + 1) + " has " + std::to_string(columns.size()) +
                           " columns, expected " + std::to_string(kColumns));
        }
        IndexRow row;
        row.path = std::string(columns[0]);
        const auto [size_end, size_ec] =
            std::from_chars(columns[1].data(), columns[1].data() + columns[1].size(), row.size);
        std::uint32_t crc = 0;
        const auto [crc_end, crc_ec] =
            std::from_chars(columns[2].data(), columns[2].data() + columns[2].size(), crc, 16);
        if (size_ec != std::errc() || size_end != columns[1].data() + columns[1].size() || columns[2].size() != 8 ||
            crc_ec != std::errc() || crc_end != columns[2].data() + columns[2].size()) {
            return R::fail("index.tsv line " + std::to_string(n + 1) + " has a bad size or crc32");
        }
        row.crc32 = crc;
        row.source = std::string(columns[3]);
        row.format = std::string(columns[4]);
        if (!columns[5].empty()) {
            for (const std::string_view part : split(columns[5], ';')) {
                row.converted.emplace_back(part);
            }
        }
        rows.push_back(std::move(row));
    }
    return R::ok(std::move(rows));
}

}  // namespace sh::import
