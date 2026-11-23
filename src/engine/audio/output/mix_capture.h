// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Keeps a rendered mix as a WAV file (16-bit stereo at the mix rate), for comparing two builds frame by frame.
// The header is brought up to date on every seal(), so the file is a
// valid WAV even when the game is killed.

#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace sh::audio::output {

class MixCapture {
public:
    MixCapture() = default;
    ~MixCapture() { close(); }
    MixCapture(const MixCapture&) = delete;
    MixCapture& operator=(const MixCapture&) = delete;

    bool open(const std::filesystem::path& path);
    bool is_open() const { return file_ != nullptr; }
    void append(const std::int16_t* interleaved, std::uint32_t frames);
    // Writes the header for what was appended so far.
    void seal();
    void close();

private:
    std::FILE* file_ = nullptr;
    std::uint64_t data_bytes_ = 0;
};

}  // namespace sh::audio::output
