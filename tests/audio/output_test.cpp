// The sound output (engine/audio/output): the frame queue, the device formats, the mix feed, the replay's VBlank
// sink with its capture, the choice of a sink and the settings of config.cfg. Expected values are worked out by
// hand in the comments.

#include "engine/audio/output/device_format.h"
#include "engine/audio/output/device_sink.h"
#include "engine/audio/output/frame_queue.h"
#include "engine/audio/output/mix_feed.h"
#include "engine/audio/output/output_device.h"
#include "engine/audio/output/vblank_sink.h"

#include "framework/sh_test.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace out = sh::audio::output;

// A mix whose frame n is (n, -n), counted over all calls; it records what it was asked for and may deliver less.
struct CountingMix {
    std::uint32_t next = 0;
    std::vector<std::uint32_t> asked;
    std::uint32_t deliver_at_most = ~0u;

    out::MixSource source() {
        return [this](std::int16_t* interleaved, std::uint32_t frames) {
            asked.push_back(frames);
            const std::uint32_t given = frames < deliver_at_most ? frames : deliver_at_most;
            for (std::uint32_t i = 0; i < given; ++i, ++next) {
                interleaved[2 * i] = static_cast<std::int16_t>(next);
                interleaved[2 * i + 1] = static_cast<std::int16_t>(-static_cast<int>(next));
            }
            // What is not delivered is left as rubbish: the feed has to silence it.
            for (std::uint32_t i = given; i < frames; ++i) interleaved[2 * i] = interleaved[2 * i + 1] = 0x5555;
            return given;
        };
    }
};

std::vector<std::uint8_t> bytes_of(out::StereoFrame frame, out::SampleFormat format) {
    std::vector<std::uint8_t> bytes(out::frame_bytes(format), 0xEE);
    out::encode_frame(frame, format, bytes.data());
    return bytes;
}

struct FakeSinkLog {
    std::vector<out::OutputSink> made;
    std::vector<out::SinkRequest> requests;
    std::vector<int> blanks;
    int closed = 0;
};

class FakeSink final : public out::DeviceSink {
public:
    FakeSink(out::OutputSink kind, out::OutputResult result, FakeSinkLog& log) : kind_(kind), result_(result), log_(log) {}
    out::OutputResult open(const out::SinkRequest& request) override {
        log_.requests.push_back(request);
        if (result_ != out::OutputResult::Ok) {
            request.board->fail(result_, 77, "fake failure");
            return result_;
        }
        out::OpenedDevice device;
        device.sink = kind_;
        device.sharing = request.sharing;
        device.device_rate = request.device_rate;
        device.name = "fake";
        request.board->opened(device);
        request.board->set_state(out::OutputState::Playing);
        return out::OutputResult::Ok;
    }
    void close() override { ++log_.closed; }
    void vblank(int blanks_per_second) override { log_.blanks.push_back(blanks_per_second); }

private:
    out::OutputSink kind_;
    out::OutputResult result_;
    FakeSinkLog& log_;
};

// A machine with SDL, with or without a sink of its own (which opens or fails with `own_result`).
out::SinkProvider machine(FakeSinkLog& log, bool has_own, out::OutputResult own_result, bool virtual_time = false) {
    out::SinkProvider provider;
    provider.make = [&log, has_own, own_result](out::OutputSink kind) -> std::unique_ptr<out::DeviceSink> {
        if (kind == out::OutputSink::Wasapi && !has_own) return nullptr;
        log.made.push_back(kind);
        return std::make_unique<FakeSink>(kind, kind == out::OutputSink::Wasapi ? own_result : out::OutputResult::Ok, log);
    };
    provider.virtual_time = [virtual_time] { return virtual_time; };
    return provider;
}

out::MixSource silence() {
    return [](std::int16_t*, std::uint32_t frames) { return frames; };
}

}  // namespace

SH_TEST(output_frame_queue_is_first_in_first_out_and_bounded) {
    out::FrameQueue queue(3);
    SH_CHECK(queue.capacity() == 3 && queue.empty() && queue.space() == 3);
    const std::int16_t five[] = {1, -1, 2, -2, 3, -3, 4, -4, 5, -5};
    SH_CHECK(queue.push(five, 5) == 3);  // only three fit
    SH_CHECK(queue.size() == 3 && queue.space() == 0);
    out::StereoFrame frame;
    SH_CHECK(queue.pop(frame) && frame == (out::StereoFrame{1, -1}));
    // The freed slot is reused: the write position wraps round.
    SH_CHECK(queue.push(five + 6, 2) == 1);
    SH_CHECK(queue.pop(frame) && frame == (out::StereoFrame{2, -2}));
    SH_CHECK(queue.pop(frame) && frame == (out::StereoFrame{3, -3}));
    SH_CHECK(queue.pop(frame) && frame == (out::StereoFrame{4, -4}));
    frame = {9, 9};
    SH_CHECK(!queue.pop(frame) && frame == (out::StereoFrame{9, 9}));
    queue.push(five, 2);
    queue.clear();
    SH_CHECK(queue.empty() && queue.space() == 3);
    SH_CHECK(out::FrameQueue(0).capacity() == 2);
}

SH_TEST(output_formats_hold_the_sample_in_their_top_bits) {
    using F = out::SampleFormat;
    SH_CHECK(out::frame_bytes(F::S16) == 4 && out::frame_bytes(F::S24) == 6 && out::frame_bytes(F::S24In32) == 8 &&
             out::frame_bytes(F::S32) == 8 && out::frame_bytes(F::Unknown) == 0);
    SH_CHECK(out::valid_bits(F::S24In32) == 24 && out::container_bits(F::S24In32) == 32 && out::valid_bits(F::S32) == 32);

    // 0x1234 and -2 (0xFFFE), least significant byte first.
    const out::StereoFrame frame{0x1234, -2};
    SH_CHECK(bytes_of(frame, F::S16) == (std::vector<std::uint8_t>{0x34, 0x12, 0xFE, 0xFF}));
    // 24 bits: the sample times 256 — 0x123400 and 0xFFFE00.
    SH_CHECK(bytes_of(frame, F::S24) == (std::vector<std::uint8_t>{0x00, 0x34, 0x12, 0x00, 0xFE, 0xFF}));
    // 32 bits: the sample times 65536 — 0x12340000 and 0xFFFE0000; 24 bits in a 32-bit word are the same word.
    const std::vector<std::uint8_t> wide{0x00, 0x00, 0x34, 0x12, 0x00, 0x00, 0xFE, 0xFF};
    SH_CHECK(bytes_of(frame, F::S32) == wide && bytes_of(frame, F::S24In32) == wide);
    // The extremes stay the extremes: -32768 is 0x80000000, 32767 is 0x7FFF0000.
    SH_CHECK(bytes_of({-32768, 32767}, F::S32) == (std::vector<std::uint8_t>{0, 0, 0x00, 0x80, 0, 0, 0xFF, 0x7F}));
    SH_CHECK(bytes_of({-32768, 32767}, F::S24) == (std::vector<std::uint8_t>{0, 0x00, 0x80, 0, 0xFF, 0x7F}));
}

SH_TEST(output_rates_are_the_mix_rate_and_its_doublings) {
    SH_CHECK(out::rate_multiple(44100) == 1 && out::rate_multiple(88200) == 2 && out::rate_multiple(176400) == 4 &&
             out::rate_multiple(352800) == 8);
    SH_CHECK(out::rate_multiple(0) == 0 && out::rate_multiple(48000) == 0 && out::rate_multiple(132300) == 0 &&
             out::rate_multiple(705600) == 0);
}

SH_TEST(output_shared_device_is_taken_only_in_the_mix_format) {
    using F = out::SampleFormat;
    const auto layout = [](bool pcm, int channels, int rate, int bits, int valid, int align) {
        out::WaveLayout l;
        l.integer_pcm = pcm;
        l.channels = static_cast<std::uint16_t>(channels);
        l.rate = static_cast<std::uint32_t>(rate);
        l.container_bits = static_cast<std::uint16_t>(bits);
        l.valid_bits = static_cast<std::uint16_t>(valid);
        l.block_align = static_cast<std::uint16_t>(align);
        return l;
    };
    SH_CHECK(out::format_for_layout(layout(true, 2, 44100, 16, 16, 4), 44100) == F::S16);
    SH_CHECK(out::format_for_layout(layout(true, 2, 44100, 24, 24, 6), 44100) == F::S24);
    SH_CHECK(out::format_for_layout(layout(true, 2, 88200, 32, 24, 8), 88200) == F::S24In32);
    SH_CHECK(out::format_for_layout(layout(true, 2, 44100, 32, 32, 8), 44100) == F::S32);
    // Windows' mixer: 32-bit floating point, usually at 48 kHz.
    SH_CHECK(!out::format_for_layout(layout(false, 2, 48000, 32, 32, 8), 44100));
    SH_CHECK(!out::format_for_layout(layout(false, 2, 44100, 32, 32, 8), 44100));
    SH_CHECK(!out::format_for_layout(layout(true, 2, 48000, 16, 16, 4), 44100));  // another rate
    SH_CHECK(!out::format_for_layout(layout(true, 2, 88200, 16, 16, 4), 44100));  // not the asked multiple
    SH_CHECK(!out::format_for_layout(layout(true, 6, 44100, 16, 16, 12), 44100));  // surround
    SH_CHECK(!out::format_for_layout(layout(true, 2, 44100, 16, 16, 8), 44100));  // padded frames
    SH_CHECK(!out::format_for_layout(layout(true, 2, 44100, 32, 20, 8), 44100));  // 20 bits

    const std::vector<out::ExclusiveOffer> offers = out::exclusive_offers();
    SH_CHECK(offers.size() == 5 && offers[0].format == F::S16 && !offers[0].plain_header);
    SH_CHECK(offers[1].format == F::S16 && offers[1].plain_header && offers[4].format == F::S32);
}

SH_TEST(output_feed_renders_ahead_in_chunks_and_fills_gaps_with_silence) {
    out::FrameCounters counters;
    CountingMix mix;
    out::MixFeed feed(mix.source(), 3000, counters);

    // 2500 frames: two whole chunks of 1024 and the rest.
    feed.render_until(2500);
    SH_CHECK(mix.asked == (std::vector<std::uint32_t>{1024, 1024, 452}));
    SH_CHECK(feed.rendered() == 2500 && feed.queued() == 2500 && counters.mixed.load() == 2500);
    feed.render_until(2400);  // already there
    SH_CHECK(mix.asked.size() == 3);
    // The queue has room for 500 more, however much is asked.
    feed.render_until(100000);
    SH_CHECK(mix.asked.back() == 500 && feed.rendered() == 3000 && feed.queued() == 3000);

    bool in_order = true;
    for (int n = 0; n < 3000; ++n) {
        const out::StereoFrame frame = feed.take();
        in_order = in_order && frame.left == static_cast<std::int16_t>(n) && frame.right == static_cast<std::int16_t>(-n);
    }
    SH_CHECK(in_order && feed.taken() == 3000 && counters.missing.load() == 0);

    // An empty queue: silence, counted.
    SH_CHECK(feed.take() == (out::StereoFrame{0, 0}) && counters.missing.load() == 1 && feed.taken() == 3001);

    // A source that delivers 10 of 40 frames: 30 frames of silence, counted, and the count of rendered frames
    // still moves by 40.
    mix.deliver_at_most = 10;
    feed.render_until(feed.rendered() + 40);
    SH_CHECK(counters.missing.load() == 31 && feed.queued() == 40);
    for (int n = 0; n < 10; ++n) SH_CHECK(feed.take().left == static_cast<std::int16_t>(3000 + n));
    bool silent = true;
    for (int n = 0; n < 30; ++n) silent = silent && feed.take() == (out::StereoFrame{0, 0});
    SH_CHECK(silent && counters.missing.load() == 31);

    // A queue smaller than a chunk bounds the chunk.
    CountingMix small_mix;
    out::MixFeed small(small_mix.source(), 100, counters);
    small.render_until(1000);
    SH_CHECK(small_mix.asked == (std::vector<std::uint32_t>{100}));
}

SH_TEST(output_writer_repeats_frames_for_a_faster_device) {
    out::FrameCounters counters;
    CountingMix mix;
    mix.next = 1;
    out::MixFeed feed(mix.source(), 64, counters);
    out::DeviceWriter writer(feed, out::SampleFormat::S16, 4);  // a device at 176.4 kHz

    // 6 device frames need 2 mix frames: the first four times, the second twice (two copies stay pending).
    SH_CHECK(writer.mix_frames_for(6) == 2 && writer.mix_frames_for(4) == 1 && writer.mix_frames_for(5) == 2);
    feed.render_until(8);
    std::vector<std::uint8_t> bytes(6 * 4);
    writer.write(bytes.data(), 6);
    const auto left_at = [&bytes](int frame) { return bytes[frame * 4] | bytes[frame * 4 + 1] << 8; };
    SH_CHECK(left_at(0) == 1 && left_at(3) == 1 && left_at(4) == 2 && left_at(5) == 2 && feed.taken() == 2);
    // Two copies of frame 2 are pending: 2 more device frames need nothing, 3 need one mix frame.
    SH_CHECK(writer.mix_frames_for(2) == 0 && writer.mix_frames_for(3) == 1 && writer.mix_frames_for(7) == 2);
    writer.write(bytes.data(), 3);
    SH_CHECK(left_at(0) == 2 && left_at(1) == 2 && left_at(2) == 3 && feed.taken() == 3);

    // At the mix rate a device frame is a mix frame.
    out::DeviceWriter plain(feed, out::SampleFormat::S24, 1);
    SH_CHECK(plain.mix_frames_for(735) == 735);
    std::vector<std::uint8_t> packed(2 * 6);
    plain.write(packed.data(), 2);
    // Frames 4 and 5 (left 4, right -4 = 0xFFFC): 24-bit words 0x000400, 0xFFFC00, 0x000500, 0xFFFB00.
    SH_CHECK(packed == (std::vector<std::uint8_t>{0, 4, 0, 0, 0xFC, 0xFF, 0, 5, 0, 0, 0xFB, 0xFF}));
}

SH_TEST(output_vblank_sink_renders_a_blanks_share_and_keeps_it) {
    namespace fs = std::filesystem;
    const fs::path wav = fs::temp_directory_path() / "sh_tests_output_capture.wav";
    fs::remove(wav);
    out::StatusBoard board;
    CountingMix mix;
    {
        out::VBlankSink sink(wav);
        out::SinkRequest request;
        request.source = mix.source();
        request.board = &board;
        SH_CHECK(sink.open(request) == out::OutputResult::Ok);
        SH_CHECK(board.snapshot().sink == out::OutputSink::VBlank && board.snapshot().state == out::OutputState::Playing);

        // 44100 / 60 = 735 frames a blank.
        sink.vblank(60);
        SH_CHECK(mix.asked == (std::vector<std::uint32_t>{735}));
        // 44100 / 50 = 882.
        sink.vblank(50);
        SH_CHECK(mix.asked.back() == 882);
        // 44100 / 30 = 1470: more than a chunk.
        sink.vblank(30);
        SH_CHECK(mix.asked.size() == 4 && mix.asked[2] == 1024 && mix.asked[3] == 446);
        sink.vblank(0);
        SH_CHECK(mix.asked.size() == 4);
        const out::OutputStatus status = board.snapshot();
        SH_CHECK(status.mix_frames == 735 + 882 + 1470 && status.device_frames_played == status.mix_frames);
        sink.close();
    }
    // The file: a 44-byte header and 3087 frames of 4 bytes.
    std::ifstream file(wav, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::uint32_t data_bytes = 3087 * 4;
    SH_CHECK(bytes.size() == 44 + data_bytes);
    const auto le32 = [&bytes](std::size_t at) {
        return bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | static_cast<std::uint32_t>(bytes[at + 3]) << 24;
    };
    SH_CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "RIFF" && le32(4) == 36 + data_bytes);
    SH_CHECK(std::string(bytes.begin() + 8, bytes.begin() + 16) == "WAVEfmt " && le32(16) == 16);
    SH_CHECK(le32(20) == (1u | 2u << 16) && le32(24) == 44100 && le32(28) == 176400 && le32(32) == (4u | 16u << 16));
    SH_CHECK(std::string(bytes.begin() + 36, bytes.begin() + 40) == "data" && le32(40) == data_bytes);
    // Frame 3086, the last: left 3086 = 0x0C0E, right -3086 = 0xF3F2.
    SH_CHECK(bytes[44] == 0 && bytes[45] == 0 && bytes[bytes.size() - 4] == 0x0E && bytes[bytes.size() - 3] == 0x0C &&
             bytes[bytes.size() - 2] == 0xF2 && bytes[bytes.size() - 1] == 0xF3);
    file.close();
    fs::remove(wav);

    // A blank rate that does not divide 44100: 59 blanks are still exactly one second (747 * 59 = 44073, and
    // the 27 left over are spread as one more frame on 27 of the blanks).
    out::VBlankSink odd;
    CountingMix odd_mix;
    out::SinkRequest request;
    request.source = odd_mix.source();
    request.board = &board;
    odd.open(request);
    for (int blank = 0; blank < 59; ++blank) odd.vblank(59);
    std::uint32_t total = 0, longer = 0;
    for (const std::uint32_t frames : odd_mix.asked) {
        total += frames;
        longer += frames == 748 ? 1 : 0;
        SH_CHECK(frames == 747 || frames == 748);
    }
    SH_CHECK(total == 44100 && longer == 27 && odd_mix.asked[0] == 747);
}

SH_TEST(output_device_takes_the_platform_sink_first_and_falls_back_to_sdl) {
    using R = out::OutputResult;
    using S = out::OutputSink;
    {
        // No sink of the platform's own (Linux): SDL is the first choice, not a fallback.
        FakeSinkLog log;
        out::OutputDevice device(machine(log, false, R::Ok));
        SH_CHECK(device.open({}, silence()) == R::Ok);
        SH_CHECK(log.made == (std::vector<S>{S::Sdl}));
        SH_CHECK(log.requests[0].sharing == out::OutputSharing::Shared && log.requests[0].device_rate == 44100 &&
                 log.requests[0].latency_ms == 20 && log.requests[0].queue_frames == 8192);
        const out::OutputStatus status = device.status();
        SH_CHECK(status.sink == S::Sdl && status.fallbacks == 0 && status.result == R::Ok &&
                 status.state == out::OutputState::Playing);
        SH_CHECK(device.open({}, silence()) == R::AlreadyOpen);
        device.close();
        SH_CHECK(log.closed == 1 && device.status().state == out::OutputState::Closed);
        SH_CHECK(device.open({}, silence()) == R::Ok);
    }
    {
        // The platform's sink plays: nothing else is made.
        FakeSinkLog log;
        out::OutputDevice device(machine(log, true, R::Ok));
        SH_CHECK(device.open({}, silence()) == R::Ok && log.made == (std::vector<S>{S::Wasapi}));
        SH_CHECK(log.requests[0].sharing == out::OutputSharing::Shared && device.status().sink == S::Wasapi);
    }
    {
        // It refuses the shared device's format: SDL plays, and the status keeps the reason.
        FakeSinkLog log;
        out::OutputDevice device(machine(log, true, R::UnsupportedFormat));
        SH_CHECK(device.open({}, silence()) == R::OkFallback);
        SH_CHECK(log.made == (std::vector<S>{S::Wasapi, S::Sdl}));
        const out::OutputStatus status = device.status();
        SH_CHECK(status.sink == S::Sdl && status.fallbacks == 1 && status.fallback_reason == R::UnsupportedFormat &&
                 status.result == R::OkFallback && status.native_error == 77 && !status.note.empty());
    }
    {
        // Exclusive use is asked of the platform's sink as such; SDL, as the fallback, is always shared.
        FakeSinkLog log;
        out::OutputDevice device(machine(log, true, R::DeviceBusy));
        out::OutputSettings settings;
        settings.sharing = out::OutputSharing::Exclusive;
        settings.device_rate = 88200;
        SH_CHECK(device.open(settings, silence()) == R::OkFallback);
        SH_CHECK(log.requests[0].sharing == out::OutputSharing::Exclusive && log.requests[0].device_rate == 88200);
        SH_CHECK(log.requests[1].sharing == out::OutputSharing::Shared && log.requests[1].device_rate == 88200);
    }
    {
        // SDL asked for by name: the platform's sink is not even made.
        FakeSinkLog log;
        out::OutputDevice device(machine(log, true, R::Ok));
        out::OutputSettings settings;
        settings.sink = S::Sdl;
        SH_CHECK(device.open(settings, silence()) == R::Ok && log.made == (std::vector<S>{S::Sdl}));
    }
    {
        // The platform's sink asked for by name where there is none: SDL, as a fallback.
        FakeSinkLog log;
        out::OutputDevice device(machine(log, false, R::Ok));
        out::OutputSettings settings;
        settings.sink = S::Wasapi;
        SH_CHECK(device.open(settings, silence()) == R::OkFallback);
        SH_CHECK(device.status().fallback_reason == R::SinkError && device.status().sink == S::Sdl);
    }
}

SH_TEST(output_device_bit_perfect_takes_only_an_exclusive_device) {
    using R = out::OutputResult;
    using S = out::OutputSink;
    out::OutputSettings settings;
    settings.bit_perfect = true;
    {
        // Exclusive is implied, and a failure is final: SDL would not be bit-perfect.
        FakeSinkLog log;
        out::OutputDevice device(machine(log, true, R::DeviceBusy));
        SH_CHECK(device.open(settings, silence()) == R::DeviceBusy);
        SH_CHECK(log.made == (std::vector<S>{S::Wasapi}) && log.requests[0].sharing == out::OutputSharing::Exclusive);
        SH_CHECK(device.status().state == out::OutputState::Failed && device.status().fallbacks == 0);
        // The device is not left open: another attempt is allowed.
        SH_CHECK(device.open(settings, silence()) == R::DeviceBusy);
    }
    {
        FakeSinkLog log;
        out::OutputDevice device(machine(log, false, R::Ok));
        out::OutputSettings by_name = settings;
        by_name.sink = S::Wasapi;
        SH_CHECK(device.open(by_name, silence()) == R::SinkError && log.made.empty());
    }
    {
        // Settings that contradict it are refused before any sink is made.
        FakeSinkLog log;
        out::OutputDevice device(machine(log, true, R::Ok));
        out::OutputSettings sdl = settings, shared = settings, doubled = settings;
        sdl.sink = S::Sdl;
        shared.sharing = out::OutputSharing::Shared;
        doubled.device_rate = 88200;
        SH_CHECK(device.open(sdl, silence()) == R::InvalidSettings);
        SH_CHECK(device.open(shared, silence()) == R::InvalidSettings);
        SH_CHECK(device.open(doubled, silence()) == R::InvalidSettings);
        SH_CHECK(log.made.empty() && device.status().state == out::OutputState::Failed);
    }
}

SH_TEST(output_device_checks_settings_and_serves_a_replay_from_its_clock) {
    using R = out::OutputResult;
    using S = out::OutputSink;
    {
        FakeSinkLog log;
        out::OutputDevice device(machine(log, false, R::Ok));
        out::OutputSettings settings;
        settings.device_rate = 48000;
        SH_CHECK(device.open(settings, silence()) == R::InvalidSettings);
        settings = {};
        settings.sink = S::VBlank;  // not a choice of the settings
        SH_CHECK(device.open(settings, silence()) == R::InvalidSettings);
        SH_CHECK(device.open({}, nullptr) == R::InvalidSettings && log.made.empty());

        // Latency and queue are kept within 3..500 ms and 2048..65536 frames.
        settings = {};
        settings.latency_ms = 1;
        settings.queue_frames = 1;
        SH_CHECK(device.open(settings, silence()) == R::Ok);
        SH_CHECK(log.requests.back().latency_ms == 3 && log.requests.back().queue_frames == 2048);
        device.close();
        settings.latency_ms = 9999;
        settings.queue_frames = 1 << 20;
        SH_CHECK(device.open(settings, silence()) == R::Ok);
        SH_CHECK(log.requests.back().latency_ms == 500 && log.requests.back().queue_frames == 65536);
        // No replay: a blank is nobody's business, but it is passed to the open sink, which ignores it.
        device.vblank(60);
    }
    {
        // A replay's clock takes the VBlank sink whatever the settings name.
        FakeSinkLog log;
        out::OutputDevice device(machine(log, true, R::Ok, true));
        out::OutputSettings settings;
        settings.sink = S::Wasapi;
        device.vblank(60);  // nothing open yet
        SH_CHECK(device.open(settings, silence()) == R::Ok && log.made == (std::vector<S>{S::VBlank}));
        device.vblank(60);
        device.vblank(50);
        SH_CHECK(log.blanks == (std::vector<int>{60, 50}));
        device.close();
        device.vblank(60);
        SH_CHECK(log.blanks.size() == 2);
    }
}

SH_TEST(output_settings_are_read_from_the_config_text) {
    using S = out::OutputSink;
    SH_CHECK(out::parse_output_settings("") == out::OutputSettings{});
    SH_CHECK(out::parse_output_settings("# audio_backend = sdl\n;audio_rate = 88200\nfps_cap = 60\n") ==
             out::OutputSettings{});

    out::OutputSettings s = out::parse_output_settings(
        "  audio_backend =  wasapi   # the platform's own\r\n"
        "audio_mode=exclusive\n"
        "audio_rate = 88200 Hz\n"
        "audio_bit_perfect = 1 ; yes\n");
    SH_CHECK(s.sink == S::Wasapi && s.sharing == out::OutputSharing::Exclusive && s.device_rate == 88200 && s.bit_perfect);

    // A later line replaces an earlier one; unknown words are the default; only the supported rates are taken.
    s = out::parse_output_settings("audio_backend = sdl\naudio_backend = alsa\naudio_mode = shared\n"
                                   "audio_rate = 176400\naudio_rate = 48000\naudio_bit_perfect = yes\n");
    SH_CHECK(s.sink == S::Auto && s.sharing == out::OutputSharing::Shared && s.device_rate == 0 && !s.bit_perfect);
    SH_CHECK(out::parse_output_settings("audio_rate = auto").device_rate == 0);
    SH_CHECK(out::parse_output_settings("audio_rate = 352800").device_rate == 352800);
    SH_CHECK(out::parse_output_settings("audio_rate = -44100").device_rate == 0);
    SH_CHECK(out::parse_output_settings("audio_backend = SDL").sink == S::Auto);  // the words are lower case
    SH_CHECK(out::parse_output_settings("audio_backend = sdl").sink == S::Sdl);
    SH_CHECK(out::parse_output_settings("audio_bit_perfect = 2").bit_perfect);
    SH_CHECK(!out::parse_output_settings("audio_bit_perfect = 0").bit_perfect);
}
