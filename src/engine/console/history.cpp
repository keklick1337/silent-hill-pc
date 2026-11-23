// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/console/history.h"

#include <fstream>
#include <system_error>

namespace sh::console {

namespace {

std::string trimmed(std::string_view s) {
    const auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return std::string(s);
}

}  // namespace

void History::add(std::string_view line) {
    position_ = -1;
    std::string entry = trimmed(line);
    if (entry.empty() || capacity_ == 0) return;
    // The file format is one entry per line, so an embedded line break would split the entry on reload.
    for (char& c : entry) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    if (!entries_.empty() && entries_.back() == entry) return;
    entries_.push_back(std::move(entry));
    if (entries_.size() > capacity_) {
        entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - capacity_));
    }
}

const std::string* History::prev(std::string_view current) {
    if (entries_.empty()) return nullptr;
    if (position_ < 0) {
        parked_.assign(current);
        position_ = 0;
    } else if (static_cast<std::size_t>(position_) + 1 < entries_.size()) {
        ++position_;
    }
    return &entries_[entries_.size() - 1 - static_cast<std::size_t>(position_)];
}

const std::string* History::next() {
    if (position_ < 0) return nullptr;
    if (position_ == 0) {
        position_ = -1;
        return &parked_;
    }
    --position_;
    return &entries_[entries_.size() - 1 - static_cast<std::size_t>(position_)];
}

const std::string* History::find_prefix(std::string_view prefix) const {
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (std::string_view(*it).substr(0, prefix.size()) == prefix) return &*it;
    }
    return nullptr;
}

void History::clear() {
    entries_.clear();
    position_ = -1;
    parked_.clear();
}

Result<void> History::load(const std::filesystem::path& file) {
    entries_.clear();
    position_ = -1;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return Result<void>::ok();
    std::ifstream in(file, std::ios::binary);
    if (!in) return Result<void>::fail("cannot open history file " + file.string());
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        entries_.push_back(line);
    }
    if (entries_.size() > capacity_) {
        entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - capacity_));
    }
    return Result<void>::ok();
}

Result<void> History::save(const std::filesystem::path& file) const {
    std::error_code ec;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), ec);
        if (ec) return Result<void>::fail("cannot create " + file.parent_path().string() + ": " + ec.message());
    }
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return Result<void>::fail("cannot write " + tmp.string());
        for (const std::string& e : entries_) out << e << '\n';
        out.flush();
        if (!out) return Result<void>::fail("write failed for " + tmp.string());
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return Result<void>::fail("cannot replace " + file.string());
    }
    return Result<void>::ok();
}

}  // namespace sh::console
