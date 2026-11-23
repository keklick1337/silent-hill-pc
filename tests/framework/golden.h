// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Recorded behaviour (docs/TESTING.md, recorded tables): a text table under tests/data/ holds inputs and the
// outputs observed for them, and a test feeds the inputs to the engine and compares. A line is cells separated by
// blanks, the first cell names the kind of the row; `#` starts a comment line. Long seeded runs are recorded as
// digests of their outputs: `Sequence` makes the inputs, the same on every platform, and `Digest` folds the outputs.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace sh::test {

struct GoldenRow {
    std::vector<std::string> cells;  // without the kind
    int line = 0;

    const std::string& str(std::size_t i) const {
        static const std::string none;
        return i < cells.size() ? cells[i] : none;
    }
    std::int64_t num(std::size_t i) const { return std::strtoll(str(i).c_str(), nullptr, 0); }
    std::uint64_t hex(std::size_t i) const { return std::strtoull(str(i).c_str(), nullptr, 16); }
    bool flag(std::size_t i) const { return num(i) != 0; }
};

// The rows of one kind of tests/data/<relative>, in file order; none when the file is missing.
inline std::vector<GoldenRow> golden_rows(std::string_view relative, std::string_view kind) {
    std::vector<GoldenRow> rows;
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/tests/data/" + std::string(relative));
    std::string text;
    for (int line = 1; std::getline(in, text); ++line) {
        std::istringstream cells(text);
        std::string cell;
        if (!(cells >> cell) || cell[0] == '#' || cell != kind) {
            continue;
        }
        GoldenRow row;
        row.line = line;
        while (cells >> cell) {
            row.cells.push_back(cell);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

// Says which row of which table disagreed; the caller still counts the failure with SH_CHECK.
inline void golden_mismatch(std::string_view relative, const GoldenRow& row, const std::string& got) {
    std::fprintf(stderr, "  tests/data/%.*s:%d: the engine gives %s\n", static_cast<int>(relative.size()),
                 relative.data(), row.line, got.c_str());
}

// Seeded inputs (SplitMix64). Every draw is its own statement in the tests, so the order never depends on the
// compiler.
class Sequence {
public:
    explicit Sequence(std::uint64_t seed) : state_(seed) {}

    std::uint32_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return static_cast<std::uint32_t>((z ^ (z >> 31)) >> 32);
    }
    // 0 .. n-1
    int below(int n) { return static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
    bool one_in(int n) { return below(n) == 0; }

private:
    std::uint64_t state_;
};

// FNV-1a over the outputs of a run.
class Digest {
public:
    void add(std::int64_t v) {
        for (int i = 0; i < 8; ++i) {
            byte(static_cast<unsigned char>(static_cast<std::uint64_t>(v) >> (8 * i)));
        }
    }
    void add(std::string_view s) {
        for (const char c : s) {
            byte(static_cast<unsigned char>(c));
        }
        byte(0);
    }
    std::uint64_t value() const { return h_; }
    std::string hex() const {
        char buf[20];
        std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h_));
        return buf;
    }

private:
    void byte(unsigned char b) {
        h_ ^= b;
        h_ *= 0x100000001B3ull;
    }
    std::uint64_t h_ = 0xCBF29CE484222325ull;
};

// True when the digest of a run equals the recorded one; prints both when it does not.
inline bool golden_digest_is(std::string_view relative, const GoldenRow& row, std::size_t cell, const Digest& got) {
    if (row.hex(cell) == got.value()) {
        return true;
    }
    golden_mismatch(relative, row, "the digest " + got.hex());
    return false;
}

}  // namespace sh::test
