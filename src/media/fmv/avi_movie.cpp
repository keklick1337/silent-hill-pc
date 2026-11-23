// SPDX-License-Identifier: GPL-3.0-or-later
// An .avi replacement movie without FFmpeg: Motion JPEG pictures through libjpeg, or uncompressed ones, and PCM
// sound. The chunks are taken in file order: sound is queued as it comes, a picture waits for its time.

#include "media/fmv/movie_sources.h"

#include "media/fmv/codec/avi_file.h"
#include "media/fmv/codec/raw_video.h"
#include "media/fmv/movie_output.h"

#include "engine/log/sh_log.h"
#include "engine/timing/hp_timer.h"

#include <SDL3/SDL.h>

#include <csetjmp>
#include <cstdio>
#include <vector>

#include <jpeglib.h>

namespace sh::fmv {

namespace {

constexpr double kStallSeconds = 1.0;
// A file that names no frame period plays at the disc movies' rate.
constexpr double kDefaultFramePeriod = 1.0 / 15.0;

// libjpeg leaves the process on an error unless its handler is replaced; this one returns to the decoder.
struct JpegFailure {
    jpeg_error_mgr base;
    std::jmp_buf back;
};

void jpeg_failed(j_common_ptr info) { std::longjmp(reinterpret_cast<JpegFailure*>(info->err)->back, 1); }

// One JPEG picture to RGB24 of its own size. False for a picture libjpeg does not take.
bool decode_jpeg(const std::vector<std::uint8_t>& data, std::vector<std::uint8_t>& rgb, int& width, int& height) {
    jpeg_decompress_struct jpeg;
    JpegFailure failure;
    jpeg.err = jpeg_std_error(&failure.base);
    failure.base.error_exit = jpeg_failed;
    if (setjmp(failure.back) != 0) {
        jpeg_destroy_decompress(&jpeg);
        return false;
    }
    jpeg_create_decompress(&jpeg);
    // Older libjpeg takes the source as a pointer to non-const bytes; it only reads them.
    jpeg_mem_src(&jpeg, const_cast<unsigned char*>(data.data()), static_cast<unsigned long>(data.size()));
    if (jpeg_read_header(&jpeg, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&jpeg);
        return false;
    }
    jpeg.out_color_space = JCS_RGB;  // grey pictures too
    jpeg_start_decompress(&jpeg);
    const std::size_t row_bytes = static_cast<std::size_t>(jpeg.output_width) * static_cast<std::size_t>(jpeg.output_components);
    rgb.resize(row_bytes * jpeg.output_height);
    while (jpeg.output_scanline < jpeg.output_height) {
        JSAMPROW row = rgb.data() + row_bytes * jpeg.output_scanline;
        jpeg_read_scanlines(&jpeg, &row, 1);
    }
    width = static_cast<int>(jpeg.output_width);
    height = static_cast<int>(jpeg.output_height);
    jpeg_finish_decompress(&jpeg);
    jpeg_destroy_decompress(&jpeg);
    return true;
}

// How the file's sound reaches the device.
struct SoundPlan {
    bool play = false;
    SampleFormat format = SampleFormat::S16;
    int narrow_from_bits = 0;  // 24 or 32: integer samples cut down to 16 bits first
};

SoundPlan plan_sound(const AviAudioFormat& audio) {
    constexpr int kPcm = 1, kFloat = 3;
    SoundPlan plan;
    if (audio.sample_rate == 0 || audio.channels == 0) return plan;  // no sound stream
    plan.play = true;
    if (audio.format_tag == kFloat && audio.bits_per_sample == 32) {
        plan.format = SampleFormat::F32;
    } else if (audio.format_tag == kPcm && audio.bits_per_sample == 16) {
        plan.format = SampleFormat::S16;
    } else if (audio.format_tag == kPcm && audio.bits_per_sample == 8) {
        plan.format = SampleFormat::U8;
    } else if (audio.format_tag == kPcm && (audio.bits_per_sample == 24 || audio.bits_per_sample == 32)) {
        plan.narrow_from_bits = audio.bits_per_sample;
    } else {
        plan.play = false;
        SH_LOG_DEBUG(FMV, "[FMV] sound format 0x%X (%d bits) has no decoder here: the movie plays silent. PCM plays: "
                          "ffmpeg -i in.avi -c:v copy -c:a pcm_s16le out.avi",
                     static_cast<unsigned>(audio.format_tag), audio.bits_per_sample);
    }
    return plan;
}

}  // namespace

bool play_avi_movie(const std::string& path) {
    Result<AviFile> opened = AviFile::open(open_file_source(path));
    if (!opened) {
        SH_LOG_DEBUG(FMV, "[FMV] %s: %s", path.c_str(), opened.error().c_str());
        return false;
    }
    const AviFile& file = opened.value();
    const AviVideoFormat& video = file.video();
    const AviCodec codec = avi_codec(video);
    if (codec == AviCodec::Unsupported) {
        SH_LOG_DEBUG(FMV, "[FMV] %s: picture codec '%.4s' (%d bits) is not built in. Built in: Motion JPEG, uncompressed "
                          "RGB, YUY2, UYVY, I420, YV12, NV12; ffmpeg -i in.mp4 -c:v mjpeg -q:v 3 -c:a pcm_s16le out.avi "
                          "makes one",
                     path.c_str(), video.fourcc.data(), video.bits_per_pixel);
        return false;
    }
    const double period = file.microseconds_per_frame() > 0 ? file.microseconds_per_frame() / 1000000.0 : kDefaultFramePeriod;
    SH_LOG_DEBUG(FMV, "[FMV] %s: %dx%d, %u frames, %.1f a second", path.c_str(), video.width, video.height,
                 static_cast<unsigned>(file.declared_frames()), 1.0 / period);

    const AviAudioFormat& audio = file.audio();
    const SoundPlan plan = plan_sound(audio);
    SoundOutput sound;
    if (plan.play && sound.open(audio.sample_rate, audio.channels, plan.format)) {
        SH_LOG_DEBUG(FMV, "[FMV] %s: sound %d Hz, %d channels, %d bits%s", path.c_str(), audio.sample_rate, audio.channels,
                     audio.bits_per_sample, plan.narrow_from_bits != 0 ? " played as 16" : "");
        sound.start();
    }

    std::vector<std::uint8_t> chunk_bytes, rgb;
    std::vector<std::int16_t> narrowed;
    SkipWatch skip;
    timing::HpTimer clock;
    double wait = 0.0;
    int shown = 0;
    // The file plays to its own end: the game's frame limit counts the disc movie's frames, not this file's.
    for (std::size_t next = 0;;) {
        const double passed = clock.lap();
        if (passed <= kStallSeconds) wait -= passed;
        if (skip.requested()) {
            SH_LOG_DEBUG(FMV, "[FMV] %s: skipped after %d frames", path.c_str(), shown);
            break;
        }
        if (wait > 0.0) {
            SDL_Delay(1);
            continue;
        }
        if (next >= file.chunks().size()) break;
        const AviFile::Chunk& chunk = file.chunks()[next++];
        file.read(chunk, chunk_bytes);

        if (chunk.kind == AviFile::ChunkKind::Sound) {
            if (!sound.is_open()) continue;
            if (plan.narrow_from_bits != 0) {
                narrow_to_s16(chunk_bytes, plan.narrow_from_bits, narrowed);
                sound.queue(narrowed.data(), narrowed.size() * sizeof(std::int16_t));
            } else {
                sound.queue(chunk_bytes.data(), chunk_bytes.size());
            }
            continue;
        }

        int width = video.width;
        int height = video.height < 0 ? -video.height : video.height;
        const bool decoded = codec == AviCodec::MotionJpeg ? decode_jpeg(chunk_bytes, rgb, width, height)
                                                           : unpack_raw_frame(codec, video, chunk_bytes, rgb);
        if (decoded) show_frame(rgb.data(), width, height, false);
        wait += period;
        ++shown;
    }
    sound.close();
    skip.settle();
    SH_LOG_DEBUG(FMV, "[FMV] %s: %d frames played", path.c_str(), shown);
    return true;
}

}  // namespace sh::fmv
