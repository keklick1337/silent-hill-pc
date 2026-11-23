// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Command history: a bounded list of entered lines with Up/Down browsing that parks the half-typed line, and a
// plain text file (one line per entry, newest last) so history survives restarts.

#pragma once

#include "formats/common/status.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sh::console {

class History {
public:
    explicit History(std::size_t capacity = 500) : capacity_(capacity) {}

    // Appends a line (trimmed; empty lines and an exact repeat of the newest entry are skipped) and resets
    // browsing. Oldest entries fall off at capacity.
    void add(std::string_view line);

    // Browsing. `current` is the live line: the first prev() parks it, and next() past the newest entry
    // returns it. prev() at the oldest entry keeps returning that entry; both return nullptr when there is
    // nothing to move to (empty history for prev; not browsing for next).
    const std::string* prev(std::string_view current);
    const std::string* next();
    bool browsing() const { return position_ >= 0; }
    void reset_browsing() { position_ = -1; }

    // Newest-first search for the latest entry starting with `prefix` (case-sensitive); null when none.
    const std::string* find_prefix(std::string_view prefix) const;

    const std::vector<std::string>& entries() const { return entries_; }  // oldest first
    std::size_t size() const { return entries_.size(); }
    std::size_t capacity() const { return capacity_; }
    void clear();

    // File persistence. load() replaces the entries (a missing file is not an error: empty history).
    // save() writes atomically (temp file + rename) and creates the directory.
    Result<void> load(const std::filesystem::path& file);
    Result<void> save(const std::filesystem::path& file) const;

private:
    std::size_t capacity_;
    std::vector<std::string> entries_;
    int position_ = -1;       // -1 live line; else index from the newest (0 = newest)
    std::string parked_;
};

}  // namespace sh::console
