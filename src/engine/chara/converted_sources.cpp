// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// ConvertedSources (chara_library.h): which disc file a converted file of the import came from.
// character.json names only converted files; the code that fills the disc file table
// (engine/chara/game/chara_file_table.cpp) asks this, so the disc path never has to appear in the data.

#include "engine/chara/chara_library.h"

#include "formats/common/json.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <iterator>

namespace sh::chara {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool ends_with_nocase(std::string_view text, std::string_view tail) {
    return text.size() >= tail.size() && lower(text.substr(text.size() - tail.size())) == lower(tail);
}

std::vector<std::string_view> split_on(std::string_view text, char sep) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == sep) {
            out.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

bool read_text(const std::filesystem::path& file, std::string& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// A string member at `path` ("source", "path") of a JSON file, or empty.
std::string manifest_string(const std::filesystem::path& file, std::initializer_list<std::string_view> path) {
    std::error_code ec;
    std::string text;
    if (!std::filesystem::is_regular_file(file, ec) || !read_text(file, text)) {
        return {};
    }
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return {};
    }
    const json::Value* at = &doc.value();
    for (const std::string_view key : path) {
        at = at->is_object() ? at->find(key) : nullptr;
        if (at == nullptr) {
            return {};
        }
    }
    return at->is_string() ? at->string() : std::string();
}

// "textures/CHARA/HERO.p03" / "textures/CHARA/HERO.alt1" -> "textures/CHARA/HERO": a palette or baked
// variant picture belongs to its TIM, whose name never has a dot (the same rule as the scene's tim_json_path).
std::string tim_stem(std::string stem) {
    const std::size_t slash = stem.find_last_of('/');
    const std::size_t dot = stem.find('.', slash == std::string::npos ? 0 : slash + 1);
    if (dot != std::string::npos) {
        stem.resize(dot);
    }
    return stem;
}

}  // namespace

ConvertedSources ConvertedSources::from_index(std::string_view index_tsv) {
    ConvertedSources out;
    const std::vector<std::string_view> lines = split_on(index_tsv, '\n');
    if (lines.empty() || !lines.front().starts_with("path\t")) {
        return out;
    }
    // The "converted" column lists the converter's outputs of a row, ';'-separated.
    const std::vector<std::string_view> header = split_on(lines.front(), '\t');
    std::size_t column = header.size();
    for (std::size_t i = 0; i < header.size(); ++i) {
        std::string_view name = header[i];
        while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) {
            name.remove_suffix(1);
        }
        if (name == "converted") {
            column = i;
        }
    }
    if (column == header.size()) {
        return out;
    }
    for (std::size_t l = 1; l < lines.size(); ++l) {
        std::string_view line = lines[l];
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const std::vector<std::string_view> cols = split_on(line, '\t');
        if (cols.size() <= column || cols[0].empty() || cols[column].empty()) {
            continue;
        }
        for (const std::string_view converted : split_on(cols[column], ';')) {
            if (!converted.empty()) {
                out.by_converted_.emplace(lower(converted), std::string(cols[0]));
            }
        }
    }
    return out;
}

ConvertedSources ConvertedSources::load(const std::filesystem::path& gamedata_dir,
                                        std::vector<std::filesystem::path> convert_roots) {
    ConvertedSources out;
    std::string text;
    if (read_text(gamedata_dir / "index.tsv", text)) {
        out = from_index(text);
    }
    out.roots_ = std::move(convert_roots);
    out.roots_.push_back(gamedata_dir / "convert");
    return out;
}

std::string ConvertedSources::disc_path(std::string_view converted) const {
    if (converted.empty()) {
        return {};
    }
    if (const auto it = by_converted_.find(lower(converted)); it != by_converted_.end()) {
        return it->second;
    }
    // Not in the index (a mod's own file): the manifest the converter writes next to it.
    const std::string path(converted);
    for (const std::filesystem::path& root : roots_) {
        std::string found;
        if (ends_with_nocase(path, ".glb")) {
            found = manifest_string(root / (path.substr(0, path.size() - 4) + ".model.json"), {"source", "path"});
        } else if (ends_with_nocase(path, ".anim.json")) {
            found = manifest_string(root / path, {"source", "path"});
        } else if (ends_with_nocase(path, ".png")) {
            found = manifest_string(root / (tim_stem(path.substr(0, path.size() - 4)) + ".tim.json"),
                                    {"game_path"});
        }
        if (!found.empty()) {
            return found;
        }
    }
    return {};
}

std::string drawn_model_glb(std::string_view data_model, int data_file, int current_file,
                            std::string_view current_disc_path) {
    constexpr std::string_view kModels = "models/";
    if (data_file >= 0 && data_file == current_file && data_model.starts_with(kModels) &&
        ends_with_nocase(data_model, ".glb")) {
        return std::string(data_model.substr(kModels.size()));
    }
    if (current_disc_path.empty()) {
        return {};
    }
    std::string path(current_disc_path);
    const std::size_t slash = path.find_last_of('/');
    const std::size_t dot = path.rfind('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        path.resize(dot);
    }
    return path + ".glb";
}

}  // namespace sh::chara
