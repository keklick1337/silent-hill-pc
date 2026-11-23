// SPDX-License-Identifier: GPL-3.0-or-later
// The disc's own movies: sectors through the game's one sector reader, pictures by MdecDecoder, sound by
// MovieSoundDecoder, 15 pictures a second.

#include "media/fmv/movie_sources.h"

#include "media/fmv/codec/mdec_decoder.h"
#include "media/fmv/codec/str_stream.h"
#include "media/fmv/codec/movie_sound.h"
#include "media/fmv/movie_output.h"

#include "engine/log/sh_log.h"
#include "engine/platform/disc.h"
#include "engine/timing/hp_timer.h"

#include <SDL3/SDL.h>

#include <array>
#include <vector>

namespace sh::fmv {

namespace {

constexpr double kFramePeriod = 1.0 / 15.0;
// The clock forgets a stall this long (a dragged window, a debugger) instead of racing to catch up.
constexpr double kStallSeconds = 1.0;

struct Picture {
    std::vector<std::uint8_t> rgb;
    int width = 0;
    int height = 0;
    std::uint32_t number = 0;
    bool decoded = false;
};

// Pictures decoded ahead of the clock. The sound sectors lie between the picture sectors, so reading ahead is
// also what keeps the sound device fed: read one frame per tick, it runs dry and crackles.
class Lookahead {
public:
    static constexpr std::size_t kDepth = 4;

    bool full() const { return count_ == kDepth; }
    bool empty() const { return count_ == 0; }
    Picture& back_slot() { return slots_[(head_ + count_) % kDepth]; }
    void pushed() { ++count_; }
    Picture& front() { return slots_[head_]; }
    void pop() {
        head_ = (head_ + 1) % kDepth;
        --count_;
    }

private:
    std::array<Picture, kDepth> slots_;
    std::size_t head_ = 0;
    std::size_t count_ = 0;
};

class DiscMovie {
public:
    DiscMovie(const Movie& movie, std::uint32_t first_sector)
        : movie_(movie), stream_(movie.sectors, [first_sector](std::uint32_t index, std::uint8_t* dst) {
              return disc::read_sector(static_cast<int>(first_sector + index), disc::SectorKind::Xa, dst);
          }) {
        stream_.set_sound_sink([this](const std::uint8_t* sector) { on_sound_sector(sector); });
    }

    void play(int frame_limit) {
        while (read_ahead()) {}

        SkipWatch skip;
        timing::HpTimer clock;
        double wait = 0.0;
        int shown = 0;
        for (;;) {
            if (skip.requested()) {
                SH_LOG_DEBUG(FMV, "[FMV] %s: skipped after %d frames", movie_.name.c_str(), shown);
                break;
            }
            const double passed = clock.lap();
            if (passed <= kStallSeconds) wait -= passed;
            if (wait > 0.0) {
                SDL_Delay(1);
                continue;
            }
            if (ahead_.empty() && !read_ahead()) break;
            Picture& picture = ahead_.front();
            if (past_frame_limit(picture.number, frame_limit)) break;
            ahead_.pop();
            // A frame the decoder refuses is not drawn: the picture before it stays, as on the console.
            if (picture.decoded) {
                show_frame(picture.rgb.data(), picture.width, picture.height, false);
                // The sound starts with the first picture, so both count from the same moment.
                if (!picture_shown_) {
                    picture_shown_ = true;
                    sound_.start();
                }
            }
            wait += kFramePeriod;
            ++shown;
            read_ahead();
        }
        sound_.close();
        skip.settle();
        SH_LOG_DEBUG(FMV, "[FMV] %s: %d frames played from the disc", movie_.name.c_str(), shown);
    }

private:
    // Reads and decodes one more frame; false when the lookahead is full or the movie has no more.
    bool read_ahead() {
        if (ended_ || ahead_.full()) return false;
        const StrStream::Status status = stream_.next(frame_);
        if (status != StrStream::Status::Frame) {
            if (status != StrStream::Status::End) {
                SH_LOG_DEBUG(FMV, "[FMV] %s: the stream breaks after %d frames (%s)", movie_.name.c_str(), read_,
                             status == StrStream::Status::TooLarge ? "a frame too large" : "sectors out of order");
            }
            ended_ = true;
            return false;
        }
        ++read_;
        Picture& picture = ahead_.back_slot();
        if (picture.width != frame_.width || picture.height != frame_.height) {
            picture.width = frame_.width;
            picture.height = frame_.height;
            picture.rgb.assign(static_cast<std::size_t>(frame_.width) * static_cast<std::size_t>(frame_.height) * 3, 0);
        }
        picture.number = frame_.number;
        picture.decoded = decoder_.decode(frame_.bitstream, frame_.width, frame_.height, picture.rgb.data());
        if (!picture.decoded) {
            SH_LOG_DEBUG(FMV, "[FMV] %s: frame %u does not decode, the picture before it stays", movie_.name.c_str(),
                         static_cast<unsigned>(frame_.number));
        }
        ahead_.pushed();
        return true;
    }

    // The first sound sector says what the device is to play.
    void on_sound_sector(const std::uint8_t* sector) {
        if (!sound_.is_open()) {
            const auto coding = MovieSoundDecoder::coding(sector);
            if (!coding) return;
            const int rate = static_cast<int>(coding->sample_rate);
            if (!sound_.open(rate, coding->stereo ? 2 : 1, SampleFormat::S16)) return;
            SH_LOG_DEBUG(FMV, "[FMV] %s: sound %d Hz %s", movie_.name.c_str(), rate, coding->stereo ? "stereo" : "mono");
            if (picture_shown_) sound_.start();
        }
        std::int16_t samples[kMovieSectorSamples];
        const int count = xa_.decode_sector(sector, samples);
        sound_.queue(samples, static_cast<std::size_t>(count) * sizeof(std::int16_t));
    }

    const Movie& movie_;
    StrStream stream_;
    StrFrame frame_;
    MdecDecoder decoder_;
    MovieSoundDecoder xa_;
    SoundOutput sound_;
    Lookahead ahead_;
    int read_ = 0;
    bool ended_ = false;
    bool picture_shown_ = false;
};

}  // namespace

bool play_disc_movie(const Movie& movie, std::uint32_t first_sector, int frame_limit) {
    if (!disc::available()) {
        SH_LOG_DEBUG(FMV, "[FMV] %s: no disc to read", movie.name.c_str());
        return false;
    }
    DiscMovie(movie, first_sector).play(frame_limit);
    return true;
}

}  // namespace sh::fmv
