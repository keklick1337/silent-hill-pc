// SPDX-License-Identifier: GPL-3.0-or-later
// A replacement movie FFmpeg reads (.mp4, .mkv, .webm, .mov, or an .avi the built-in reader does not take).
//
// A worker thread does all of FFmpeg's work: it reads the file, decodes, queues the sound to the device and turns
// each picture into RGBA in a short queue. The caller's thread only waits for a picture's time and shows it, so a
// slow stretch of decoding eats into the queue and not into the pace.
//
// The pace is the wall clock against the pictures' timestamps. It is not the sound device's clock: a device that
// stalls would stall the pictures with it, and the wall clock cannot stall. The clock starts when the first
// picture is ready, and the sound with it, so the time spent opening the file is not counted as played.

#include "media/fmv/movie_sources.h"

#if !defined(SH_FMV_FFMPEG)

namespace sh::fmv {

bool container_movies_built() { return false; }
bool play_container_movie(const std::string&) { return false; }

}  // namespace sh::fmv

#else

#include "media/fmv/ffmpeg_library.h"
#include "media/fmv/movie_output.h"

#include "engine/log/sh_log.h"
#include "engine/timing/hp_timer.h"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace sh::fmv {

namespace {

// A picture this far behind the clock is not shown, so slow decoding stays near real time, only less smooth.
constexpr double kLateSeconds = 0.10;

struct ReadyPicture {
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
    double due = 0.0;  // seconds from the first picture
};

// Pictures from the worker to the caller's thread. One producer, one consumer.
class PictureQueue {
public:
    static constexpr std::size_t kDepth = 3;

    // Producer: the slot to fill, once one is free; null when the consumer has stopped the queue. The slot is not
    // seen by the consumer until commit().
    ReadyPicture* next_slot() {
        std::unique_lock lock(mutex_);
        space_.wait(lock, [this] { return count_ < kDepth || stopped_; });
        return stopped_ ? nullptr : &slots_[(first_ + count_) % kDepth];
    }
    void commit() {
        const std::lock_guard lock(mutex_);
        ++count_;
    }
    // Producer: no more pictures will come.
    void finish() {
        const std::lock_guard lock(mutex_);
        finished_ = true;
    }

    struct State {
        std::size_t count;
        bool finished;
    };
    State state() {
        const std::lock_guard lock(mutex_);
        return {count_, finished_};
    }
    // Consumer: the oldest picture; it stays valid until pop().
    ReadyPicture& front() { return slots_[first_]; }
    void pop() {
        {
            const std::lock_guard lock(mutex_);
            first_ = (first_ + 1) % kDepth;
            --count_;
        }
        space_.notify_one();
    }
    void stop() {
        {
            const std::lock_guard lock(mutex_);
            stopped_ = true;
        }
        space_.notify_one();
    }
    bool stopped() {
        const std::lock_guard lock(mutex_);
        return stopped_;
    }

private:
    std::mutex mutex_;
    std::condition_variable space_;
    std::array<ReadyPicture, kDepth> slots_;
    std::size_t first_ = 0;
    std::size_t count_ = 0;
    bool finished_ = false;
    bool stopped_ = false;
};

class ContainerMovie {
public:
    ContainerMovie(const FfmpegLibrary& ff, std::string path) : ff_(ff), path_(std::move(path)) {}
    ~ContainerMovie() {
        stop_worker();
        if (scaler_ != nullptr) ff_.sws_freeContext(scaler_);
        if (resampler_ != nullptr) ff_.swr_free(&resampler_);
        ff_.av_channel_layout_uninit(&sound_layout_);
        if (picture_decoder_ != nullptr) ff_.avcodec_free_context(&picture_decoder_);
        if (sound_decoder_ != nullptr) ff_.avcodec_free_context(&sound_decoder_);
        if (input_ != nullptr) ff_.avformat_close_input(&input_);
    }
    ContainerMovie(const ContainerMovie&) = delete;
    ContainerMovie& operator=(const ContainerMovie&) = delete;

    bool open();
    // False when the file gave no picture at all (and was not skipped): the caller plays the disc movie.
    bool play();

private:
    bool find_streams();
    bool open_picture_decoder();
    void open_sound();
    void stop_worker();

    void decode_all();
    bool queue_picture(const AVFrame& frame);
    bool match_scaler(const AVFrame& frame);
    void queue_sound(const AVFrame& frame);

    const FfmpegLibrary& ff_;
    std::string path_;

    AVFormatContext* input_ = nullptr;
    AVCodecContext* picture_decoder_ = nullptr;
    AVCodecContext* sound_decoder_ = nullptr;
    int picture_stream_ = -1;
    int sound_stream_ = -1;

    // The decoder's sound format, asked through options: the fields that hold it move between FFmpeg releases.
    AVChannelLayout sound_layout_{};
    int sound_rate_ = 0;
    SoundOutput sound_;

    std::thread worker_;
    PictureQueue queue_;

    // The worker's own state.
    SwsContext* scaler_ = nullptr;
    int scaler_width_ = 0, scaler_height_ = 0, scaler_format_ = -1;
    SwrContext* resampler_ = nullptr;
    bool resampler_failed_ = false;
    std::vector<std::uint8_t> sound_buffer_;
    double tick_seconds_ = 0.0;     // one unit of the picture stream's timestamps
    double guessed_period_ = 0.0;   // for pictures without a timestamp
    std::int64_t first_timestamp_ = AV_NOPTS_VALUE;
    double last_due_ = 0.0;
};

bool ContainerMovie::find_streams() {
    if (ff_.avformat_open_input(&input_, path_.c_str(), nullptr, nullptr) != 0) {
        SH_LOG_DEBUG(FMV, "[FMV] FFmpeg does not open %s", path_.c_str());
        return false;
    }
    // FFmpeg may read up to five seconds of a file before it names the streams; that is for formats that have
    // to be searched. The movie containers declare their streams up front, so the search is held to two seconds
    // and, should that ever miss them, run once more at FFmpeg's own limits. This bounds only how the streams are
    // found: the file is then read whole.
    const Uint64 began = SDL_GetTicks();
    ff_.av_opt_set_int(input_, "probesize", 8 * 1024 * 1024, 0);
    ff_.av_opt_set_int(input_, "analyzeduration", 2 * 1000 * 1000, 0);
    int found = ff_.avformat_find_stream_info(input_, nullptr);
    if (found < 0) {
        ff_.av_opt_set_int(input_, "probesize", 5 * 1000 * 1000, 0);
        ff_.av_opt_set_int(input_, "analyzeduration", 5 * 1000 * 1000, 0);
        found = ff_.avformat_find_stream_info(input_, nullptr);
    }
    if (found < 0) {
        SH_LOG_DEBUG(FMV, "[FMV] %s: FFmpeg finds no streams", path_.c_str());
        return false;
    }
    SH_LOG_DEBUG(FMV, "[FMV] %s: streams found in %u ms", path_.c_str(), static_cast<unsigned>(SDL_GetTicks() - began));

    picture_stream_ = ff_.av_find_best_stream(input_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (picture_stream_ < 0) {
        SH_LOG_DEBUG(FMV, "[FMV] %s: no picture stream", path_.c_str());
        return false;
    }
    sound_stream_ = ff_.av_find_best_stream(input_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    return true;
}

bool ContainerMovie::open_picture_decoder() {
    const AVCodecParameters* parameters = input_->streams[picture_stream_]->codecpar;
    const AVCodec* codec = ff_.avcodec_find_decoder(parameters->codec_id);
    if (codec == nullptr) {
        SH_LOG_DEBUG(FMV, "[FMV] %s: this FFmpeg has no decoder for %s", path_.c_str(), ff_.avcodec_get_name(parameters->codec_id));
        return false;
    }
    picture_decoder_ = ff_.avcodec_alloc_context3(codec);
    if (picture_decoder_ == nullptr) return false;
    ff_.avcodec_parameters_to_context(picture_decoder_, parameters);
    // All cores, by frames or by slices as the codec allows: one core does not keep up with 4K HEVC.
    ff_.av_opt_set_int(picture_decoder_, "threads", 0, 0);
    ff_.av_opt_set_int(picture_decoder_, "thread_type", FF_THREAD_FRAME | FF_THREAD_SLICE, 0);
    if (ff_.avcodec_open2(picture_decoder_, codec, nullptr) < 0) {
        SH_LOG_DEBUG(FMV, "[FMV] %s: the %s decoder does not open", path_.c_str(), ff_.avcodec_get_name(parameters->codec_id));
        return false;
    }
    return true;
}

// Any failure here only makes the movie silent.
void ContainerMovie::open_sound() {
    if (sound_stream_ < 0) return;
    const AVCodecParameters* parameters = input_->streams[sound_stream_]->codecpar;
    if (const AVCodec* codec = ff_.avcodec_find_decoder(parameters->codec_id)) {
        sound_decoder_ = ff_.avcodec_alloc_context3(codec);
        if (sound_decoder_ != nullptr) {
            ff_.avcodec_parameters_to_context(sound_decoder_, parameters);
            if (ff_.avcodec_open2(sound_decoder_, codec, nullptr) < 0) ff_.avcodec_free_context(&sound_decoder_);
        }
    }
    std::int64_t rate = 0;
    if (sound_decoder_ == nullptr || ff_.av_opt_get_int(sound_decoder_, "ar", 0, &rate) < 0 ||
        ff_.av_opt_get_chlayout(sound_decoder_, "ch_layout", 0, &sound_layout_) < 0 || rate <= 0 ||
        sound_layout_.nb_channels <= 0) {
        SH_LOG_DEBUG(FMV, "[FMV] %s: the sound cannot be decoded, the movie plays silent", path_.c_str());
        sound_stream_ = -1;
        return;
    }
    sound_rate_ = static_cast<int>(rate);
    // The resampler is made by the worker from the first decoded frame.
    sound_.open(sound_rate_, sound_layout_.nb_channels, SampleFormat::S16);
}

bool ContainerMovie::open() {
    if (!find_streams() || !open_picture_decoder()) return false;
    open_sound();
    std::int64_t threads = 0;
    ff_.av_opt_get_int(picture_decoder_, "threads", 0, &threads);
    const AVStream* stream = input_->streams[picture_stream_];
    SH_LOG_DEBUG(FMV, "[FMV] %s: %s%s, %d decoder threads", path_.c_str(), ff_.avcodec_get_name(stream->codecpar->codec_id),
                 sound_stream_ >= 0 ? " with sound" : "", static_cast<int>(threads));
    tick_seconds_ = av_q2d(stream->time_base);
    const double rate = av_q2d(stream->avg_frame_rate);
    guessed_period_ = rate > 1.0 ? 1.0 / rate : 1.0 / 30.0;
    return true;
}

// The converter is made for the decoded picture and not for what the container declared: with 10-bit video or a
// stream that changes size the two differ. FFmpeg's converter assumes BT.601 limited range, which shifts the
// colours of HD video, so the stream's own matrix and range are set.
bool ContainerMovie::match_scaler(const AVFrame& frame) {
    if (scaler_ != nullptr && scaler_width_ == frame.width && scaler_height_ == frame.height && scaler_format_ == frame.format) {
        return true;
    }
    if (scaler_ != nullptr) ff_.sws_freeContext(scaler_);
    // Same size in and out (the GPU fits the picture to the window), so the method only matters for the chroma.
    scaler_ = ff_.sws_getContext(frame.width, frame.height, static_cast<AVPixelFormat>(frame.format), frame.width,
                                 frame.height, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    scaler_width_ = frame.width;
    scaler_height_ = frame.height;
    scaler_format_ = frame.format;
    if (scaler_ == nullptr) {
        SH_LOG_DEBUG(FMV, "[FMV] %s: no converter for %dx%d pictures of pixel format %d", path_.c_str(), frame.width,
                     frame.height, frame.format);
        return false;
    }
    // Asked of the decoder by name: the frame's own colour fields move between FFmpeg releases.
    std::int64_t matrix_tag = AVCOL_SPC_UNSPECIFIED, range_tag = AVCOL_RANGE_UNSPECIFIED;
    ff_.av_opt_get_int(picture_decoder_, "colorspace", 0, &matrix_tag);
    ff_.av_opt_get_int(picture_decoder_, "color_range", 0, &range_tag);
    int matrix = frame.height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;  // untagged HD is BT.709 nearly always
    if (matrix_tag == AVCOL_SPC_BT709) matrix = SWS_CS_ITU709;
    if (matrix_tag == AVCOL_SPC_BT2020_NCL || matrix_tag == AVCOL_SPC_BT2020_CL) matrix = SWS_CS_BT2020;
    if (matrix_tag == AVCOL_SPC_SMPTE170M || matrix_tag == AVCOL_SPC_BT470BG) matrix = SWS_CS_ITU601;
    const int full_range = range_tag == AVCOL_RANGE_JPEG ? 1 : 0;
    const int* coefficients = ff_.sws_getCoefficients(matrix);
    ff_.sws_setColorspaceDetails(scaler_, coefficients, full_range, coefficients, 1, 0, 1 << 16, 1 << 16);
    SH_LOG_DEBUG(FMV, "[FMV] %s: %dx%d pictures, pixel format %d, matrix %s, %s range", path_.c_str(), frame.width,
                 frame.height, frame.format, matrix == SWS_CS_ITU709 ? "709" : matrix == SWS_CS_BT2020 ? "2020" : "601",
                 full_range != 0 ? "full" : "limited");
    return true;
}

// Worker: one decoded picture into the queue, waiting for room. False ends the decoding.
bool ContainerMovie::queue_picture(const AVFrame& frame) {
    if (frame.pts != AV_NOPTS_VALUE) {
        if (first_timestamp_ == AV_NOPTS_VALUE) first_timestamp_ = frame.pts;
        last_due_ = static_cast<double>(frame.pts - first_timestamp_) * tick_seconds_;
    } else {
        last_due_ += guessed_period_;
    }
    ReadyPicture* slot = queue_.next_slot();
    if (slot == nullptr || !match_scaler(frame)) return false;
    slot->rgba.resize(static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4);
    std::uint8_t* const planes[4] = {slot->rgba.data(), nullptr, nullptr, nullptr};
    const int strides[4] = {frame.width * 4, 0, 0, 0};
    ff_.sws_scale(scaler_, frame.data, frame.linesize, 0, frame.height, planes, strides);
    slot->width = frame.width;
    slot->height = frame.height;
    slot->due = last_due_;
    queue_.commit();
    return true;
}

// Worker: one decoded sound frame to the device as interleaved 16-bit samples. The resampler takes its input
// format from the frame: some decoders only settle it with their first output.
void ContainerMovie::queue_sound(const AVFrame& frame) {
    const int channels = sound_layout_.nb_channels;
    if (resampler_ == nullptr) {
        if (resampler_failed_) return;
        AVChannelLayout out_layout{};
        ff_.av_channel_layout_default(&out_layout, channels);
        int made = ff_.swr_alloc_set_opts2(&resampler_, &out_layout, AV_SAMPLE_FMT_S16, sound_rate_, &sound_layout_,
                                           static_cast<AVSampleFormat>(frame.format), sound_rate_, 0, nullptr);
        if (made >= 0) made = ff_.swr_init(resampler_);
        ff_.av_channel_layout_uninit(&out_layout);
        if (made < 0) {
            SH_LOG_DEBUG(FMV, "[FMV] %s: no resampler for sample format %d, the movie plays silent", path_.c_str(), frame.format);
            if (resampler_ != nullptr) ff_.swr_free(&resampler_);
            resampler_failed_ = true;
            return;
        }
    }
    const int room = ff_.swr_get_out_samples(resampler_, frame.nb_samples);
    if (room <= 0) return;
    const std::size_t sample_bytes = static_cast<std::size_t>(channels) * sizeof(std::int16_t);
    if (sound_buffer_.size() < static_cast<std::size_t>(room) * sample_bytes) {
        sound_buffer_.resize(static_cast<std::size_t>(room) * sample_bytes);
    }
    std::uint8_t* out[1] = {sound_buffer_.data()};
    const int made = ff_.swr_convert(resampler_, out, room, const_cast<const std::uint8_t**>(frame.extended_data), frame.nb_samples);
    if (made > 0) sound_.queue(sound_buffer_.data(), static_cast<std::size_t>(made) * sample_bytes);
}

void ContainerMovie::decode_all() {
    AVPacket* packet = ff_.av_packet_alloc();
    AVFrame* frame = ff_.av_frame_alloc();
    bool go_on = packet != nullptr && frame != nullptr;
    while (go_on && !queue_.stopped()) {
        bool pictures = false, sounds = false;
        const bool at_end = ff_.av_read_frame(input_, packet) < 0;
        if (at_end) {
            // An empty packet makes each decoder give up what it still holds.
            ff_.avcodec_send_packet(picture_decoder_, nullptr);
            pictures = true;
            if (sound_stream_ >= 0) {
                ff_.avcodec_send_packet(sound_decoder_, nullptr);
                sounds = true;
            }
        } else {
            if (packet->stream_index == picture_stream_) {
                pictures = ff_.avcodec_send_packet(picture_decoder_, packet) >= 0;
            } else if (sound_stream_ >= 0 && packet->stream_index == sound_stream_) {
                sounds = ff_.avcodec_send_packet(sound_decoder_, packet) >= 0;
            }
            ff_.av_packet_unref(packet);
        }
        if (sounds && sound_.is_open()) {
            while (ff_.avcodec_receive_frame(sound_decoder_, frame) == 0) {
                queue_sound(*frame);
                ff_.av_frame_unref(frame);
            }
        }
        if (pictures) {
            while (go_on && ff_.avcodec_receive_frame(picture_decoder_, frame) == 0) {
                go_on = queue_picture(*frame);
                ff_.av_frame_unref(frame);
            }
        }
        if (at_end) break;
    }
    if (frame != nullptr) ff_.av_frame_free(&frame);
    if (packet != nullptr) ff_.av_packet_free(&packet);
    queue_.finish();
}

void ContainerMovie::stop_worker() {
    if (!worker_.joinable()) return;
    queue_.stop();
    worker_.join();
}

bool ContainerMovie::play() {
    worker_ = std::thread([this] { decode_all(); });

    SkipWatch skip;
    timing::HpTimer clock;
    double played = 0.0;
    bool clock_started = false;
    bool skipped = false;
    int shown = 0;
    while (!skipped) {
        PictureQueue::State state = queue_.state();
        while (state.count == 0 && !state.finished && !skipped) {
            skipped = skip.requested();
            if (!skipped) SDL_Delay(1);
            state = queue_.state();
        }
        if (skipped || state.count == 0) break;
        ReadyPicture& picture = queue_.front();

        if (!clock_started) {
            clock_started = true;
            sound_.start();
            clock.reset();
        } else {
            played += clock.lap();
        }
        // The skip is asked, and the window's events are taken, at least once a picture even when the picture is
        // late already: a slow stretch must not leave the window dead.
        for (;;) {
            skipped = skip.requested();
            if (skipped || played >= picture.due) break;
            SDL_Delay(1);
            played += clock.lap();
        }
        if (skipped) break;

        // The last picture is shown however late it is.
        const PictureQueue::State now = queue_.state();
        const bool last = now.count <= 1 && now.finished;
        if (last || picture.due >= played - kLateSeconds) {
            show_frame(picture.rgba.data(), picture.width, picture.height, true);
            ++shown;
            played += clock.lap();
        }
        queue_.pop();
    }
    stop_worker();
    sound_.close();
    skip.settle();

    if (shown == 0 && !skipped) {
        SH_LOG_DEBUG(FMV, "[FMV] %s: no picture came out of the file", path_.c_str());
        return false;
    }
    SH_LOG_DEBUG(FMV, "[FMV] %s: %d frames played%s", path_.c_str(), shown, skipped ? ", skipped" : "");
    return true;
}

}  // namespace

bool container_movies_built() { return true; }

bool play_container_movie(const std::string& path) {
    const FfmpegLibrary* ff = FfmpegLibrary::get();
    if (ff == nullptr) return false;
    ContainerMovie movie(*ff, path);
    return movie.open() && movie.play();
}

}  // namespace sh::fmv

#endif  // SH_FMV_FFMPEG
