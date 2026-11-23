// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/audio/output/mix_capture.h"

#include "engine/audio/output/device_format.h"

#include <array>
#include <cstring>

namespace sh::audio::output {

namespace {

constexpr std::uint32_t kHeaderBytes = 44;
constexpr std::uint32_t kFrameBytes = 4;

void put16(std::uint8_t* at, std::uint32_t value) {
    at[0] = static_cast<std::uint8_t>(value);
    at[1] = static_cast<std::uint8_t>(value >> 8);
}

void put32(std::uint8_t* at, std::uint32_t value) {
    put16(at, value);
    put16(at + 2, value >> 16);
}

}  // namespace

bool MixCapture::open(const std::filesystem::path& path) {
    close();
    file_ = std::fopen(path.string().c_str(), "wb");
    data_bytes_ = 0;
    if (file_ != nullptr) seal();
    return file_ != nullptr;
}

void MixCapture::append(const std::int16_t* interleaved, std::uint32_t frames) {
    if (file_ == nullptr) return;
    data_bytes_ += kFrameBytes * std::fwrite(interleaved, kFrameBytes, frames, file_);
}

void MixCapture::seal() {
    if (file_ == nullptr) return;
    const std::uint32_t data = static_cast<std::uint32_t>(data_bytes_);
    std::array<std::uint8_t, kHeaderBytes> header{};
    std::memcpy(&header[0], "RIFF", 4);
    put32(&header[4], kHeaderBytes - 8 + data);
    std::memcpy(&header[8], "WAVEfmt ", 8);
    put32(&header[16], 16);            // the format chunk's size
    put16(&header[20], 1);             // integer PCM
    put16(&header[22], 2);             // channels
    put32(&header[24], kMixRate);
    put32(&header[28], kMixRate * kFrameBytes);
    put16(&header[32], kFrameBytes);
    put16(&header[34], 16);            // bits per sample
    std::memcpy(&header[36], "data", 4);
    put32(&header[40], data);
    std::fseek(file_, 0, SEEK_SET);
    std::fwrite(header.data(), 1, header.size(), file_);
    std::fseek(file_, 0, SEEK_END);
}

void MixCapture::close() {
    if (file_ == nullptr) return;
    seal();
    std::fclose(file_);
    file_ = nullptr;
}

}  // namespace sh::audio::output
