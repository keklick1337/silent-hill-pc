#pragma once

#include "formats/archive/sh_archive.h"
#include "formats/disc/disc_image.h"
#include "formats/iso9660/iso9660.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace sh::test_audio {

// The game files of the real image, read the way the importer reads them: SILENT. as 2048-byte sectors, HILL. as raw
// 2336-byte sectors.
struct Game {
    std::shared_ptr<const sh::ByteSource> silent;
    std::shared_ptr<const sh::ByteSource> hill;
    std::vector<sh::archive::Entry> table;

    std::optional<std::vector<std::uint8_t>> read(const sh::archive::Entry& entry) const {
        return sh::archive::read_entry(entry.region == sh::archive::Region::Silent ? *silent : *hill, entry);
    }
};

// Null when the image is absent (the caller skips). Aborts the test run when the image is present but unusable.
inline std::optional<Game> open_game() {
    namespace fs = std::filesystem;
    const fs::path path = fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).bin";
    if (!fs::exists(path)) {
        return std::nullopt;
    }
    const std::shared_ptr<const sh::disc::Image> image(sh::disc::Image::open(path));
    if (!image) {
        return std::nullopt;
    }
    const auto volume = sh::iso9660::Volume::open(image);
    if (!volume) {
        return std::nullopt;
    }
    const auto slus = volume->find("SLUS_007.07");
    const auto silent = volume->find("SILENT.");
    const auto hill = volume->find("HILL.");
    if (!slus || !silent || !hill) {
        return std::nullopt;
    }
    const auto sectors = [](std::uint32_t size) { return (size + 2047) / 2048; };
    const sh::archive::Layout silent_layout{silent->lba, sectors(silent->size)};
    const sh::archive::Layout hill_layout{hill->lba, sectors(hill->size)};
    const std::shared_ptr<const sh::ByteSource> exe(volume->open_file(*slus));

    Game game;
    game.silent = std::shared_ptr<const sh::ByteSource>(volume->open_file(*silent));
    game.hill = std::shared_ptr<const sh::ByteSource>(sh::archive::open_hill(image, hill_layout));
    if (!exe || !game.silent || !game.hill) {
        return std::nullopt;
    }
    auto table = sh::archive::read_file_table(*exe, silent_layout, hill_layout);
    if (!table) {
        return std::nullopt;
    }
    game.table = std::move(*table);
    return game;
}

// Syntax check of a JSON document (RFC 8259 values, no extensions).
class JsonChecker {
public:
    static bool valid(std::string_view text) {
        JsonChecker c(text);
        c.skip_ws();
        return c.value() && (c.skip_ws(), c.pos_ == text.size());
    }

private:
    explicit JsonChecker(std::string_view text) : text_(text) {}

    void skip_ws() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\t' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }
    bool eat(char c) {
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }
    bool literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) == word) {
            pos_ += word.size();
            return true;
        }
        return false;
    }
    bool string() {
        if (!eat('"')) {
            return false;
        }
        while (pos_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') {
                return true;
            }
            if (c < 0x20) {
                return false;
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    return false;
                }
                const char e = text_[pos_++];
                if (e == 'u') {
                    for (int i = 0; i < 4; ++i) {
                        if (pos_ >= text_.size() || !std::isxdigit(static_cast<unsigned char>(text_[pos_++]))) {
                            return false;
                        }
                    }
                } else if (std::string_view("\"\\/bfnrt").find(e) == std::string_view::npos) {
                    return false;
                }
            }
        }
        return false;
    }
    bool number() {
        const std::size_t start = pos_;
        eat('-');
        if (!(pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])))) {
            return false;
        }
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
        if (eat('.')) {
            if (!(pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])))) {
                return false;
            }
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
            }
        }
        return pos_ > start;
    }
    bool value() {
        skip_ws();
        if (pos_ >= text_.size()) {
            return false;
        }
        switch (text_[pos_]) {
            case '{': return object();
            case '[': return array();
            case '"': return string();
            case 't': return literal("true");
            case 'f': return literal("false");
            case 'n': return literal("null");
            default: return number();
        }
    }
    bool object() {
        eat('{');
        skip_ws();
        if (eat('}')) {
            return true;
        }
        for (;;) {
            skip_ws();
            if (!string()) {
                return false;
            }
            skip_ws();
            if (!eat(':') || !value()) {
                return false;
            }
            skip_ws();
            if (eat('}')) {
                return true;
            }
            if (!eat(',')) {
                return false;
            }
        }
    }
    bool array() {
        eat('[');
        skip_ws();
        if (eat(']')) {
            return true;
        }
        for (;;) {
            if (!value()) {
                return false;
            }
            skip_ws();
            if (eat(']')) {
                return true;
            }
            if (!eat(',')) {
                return false;
            }
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

}  // namespace sh::test_audio
