// The disc movies' decoders (src/media/fmv/codec): the picture decoder, the sound decoder and the stream that
// joins sectors into frames, on streams made here; and, when the disc image is there, on three of its movies.

#include "framework/sh_test.h"

#include "formats/disc/disc_image.h"
#include "media/fmv/codec/mdec_decoder.h"
#include "media/fmv/codec/str_stream.h"
#include "media/fmv/codec/movie_sound.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

namespace {

using namespace sh::fmv;

// Writes a picture bitstream: bits most significant first into 16-bit little-endian words.
class BitWriter {
public:
    void put(std::uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) {
            word_ = static_cast<std::uint16_t>(word_ << 1 | ((value >> i) & 1));
            if (++held_ == 16) flush();
        }
    }
    void header(int scale, int version) {
        put(0x0000, 16);
        put(0x3800, 16);
        put(static_cast<std::uint32_t>(scale), 16);
        put(static_cast<std::uint32_t>(version), 16);
    }
    void dc(int value) { put(static_cast<std::uint32_t>(value) & 0x3FF, 10); }
    void end_of_block() { put(0b10, 2); }
    // A block that is only its DC value.
    void flat_block(int value) {
        dc(value);
        end_of_block();
    }
    std::vector<std::uint8_t> bytes() {
        while (held_ != 0) put(0, 1);
        return bytes_;
    }

private:
    void flush() {
        bytes_.push_back(static_cast<std::uint8_t>(word_ & 0xFF));
        bytes_.push_back(static_cast<std::uint8_t>(word_ >> 8));
        word_ = 0;
        held_ = 0;
    }
    std::vector<std::uint8_t> bytes_;
    std::uint16_t word_ = 0;
    int held_ = 0;
};

// A macroblock of one grey: no chroma, the four luma blocks at `value`.
void grey_macroblock(BitWriter& w, int value) {
    w.flat_block(0);
    w.flat_block(0);
    for (int i = 0; i < 4; ++i) w.flat_block(value);
}

std::uint64_t fnv(std::uint64_t h, const void* data, std::size_t size) {
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}
constexpr std::uint64_t kFnvStart = 1469598103934665603ull;

}  // namespace

SH_TEST(mdec_flat_blocks_give_flat_grey) {
    BitWriter w;
    w.header(1, 2);
    grey_macroblock(w, 200);  // DC 200 is a sample of 50 above the middle
    const std::vector<std::uint8_t> stream = w.bytes();
    std::vector<std::uint8_t> rgb(16 * 16 * 3, 7);
    SH_CHECK(MdecDecoder().decode(stream, 16, 16, rgb.data()));
    bool flat = true;
    for (std::uint8_t v : rgb) flat = flat && v == 178;
    SH_CHECK(flat);
}

SH_TEST(mdec_chroma_shifts_the_colours) {
    BitWriter w;
    w.header(1, 2);
    w.flat_block(160);  // Cr: +40
    w.flat_block(-80);  // Cb: -20
    for (int i = 0; i < 4; ++i) w.flat_block(0);
    const std::vector<std::uint8_t> stream = w.bytes();
    std::vector<std::uint8_t> rgb(16 * 16 * 3);
    SH_CHECK(MdecDecoder().decode(stream, 16, 16, rgb.data()));
    // R = 1.402 * 40, G = -0.3437 * -20 - 0.7143 * 40, B = 1.772 * -20, each cut to a whole number toward zero.
    SH_CHECK(rgb[0] == 128 + 56);
    SH_CHECK(rgb[1] == 128 - 21);
    SH_CHECK(rgb[2] == 128 - 35);
    SH_CHECK(std::memcmp(rgb.data(), rgb.data() + 255 * 3, 3) == 0);
}

SH_TEST(mdec_macroblocks_run_down_the_columns) {
    BitWriter w;
    w.header(1, 2);
    const int values[4] = {-200, -100, 100, 200};  // top left, bottom left, top right, bottom right
    for (int v : values) grey_macroblock(w, v);
    const std::vector<std::uint8_t> stream = w.bytes();
    std::vector<std::uint8_t> rgb(32 * 32 * 3);
    SH_CHECK(MdecDecoder().decode(stream, 32, 32, rgb.data()));
    const auto at = [&](int x, int y) { return rgb[static_cast<std::size_t>(y * 32 + x) * 3]; };
    SH_CHECK(at(0, 0) == 128 - 50);
    SH_CHECK(at(15, 31) == 128 - 25);
    SH_CHECK(at(16, 15) == 128 + 25);
    SH_CHECK(at(31, 16) == 128 + 50);
}

SH_TEST(mdec_coefficients_follow_the_cosine_transform) {
    // One luma block with DC 80, the coefficient after it (run 0, level 1) and an escaped one (run 1, level -5).
    const int scale = 8;
    BitWriter w;
    w.header(scale, 2);
    w.flat_block(0);
    w.flat_block(0);
    w.dc(80);
    w.put(0b11, 2);  // run 0, level 1
    w.put(0, 1);     // positive
    w.put(0b000001, 6);  // escape
    w.put(1, 6);         // run 1
    w.put(static_cast<std::uint32_t>(-5) & 0x3FF, 10);
    w.end_of_block();
    for (int i = 0; i < 3; ++i) w.flat_block(0);
    const std::vector<std::uint8_t> stream = w.bytes();
    std::vector<std::uint8_t> rgb(16 * 16 * 3);
    SH_CHECK(MdecDecoder().decode(stream, 16, 16, rgb.data()));

    // The same block by the textbook transform. Scan position 1 is row 0, column 1 (quantiser 16); position 3 is
    // row 2, column 0 (quantiser 19); an escaped magnitude is made odd.
    double coefficient[8][8] = {};
    coefficient[0][0] = 160;
    coefficient[0][1] = (1 * scale * 16) >> 3;
    coefficient[2][0] = -((((5 * scale * 19) >> 3) - 1) | 1);
    const double pi = std::acos(-1.0);
    int worst = 0;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            double sum = 0;
            for (int v = 0; v < 8; ++v) {
                for (int u = 0; u < 8; ++u) {
                    const double cu = u == 0 ? std::sqrt(0.5) : 1.0, cv = v == 0 ? std::sqrt(0.5) : 1.0;
                    sum += cu * cv * coefficient[v][u] * std::cos((2 * x + 1) * u * pi / 16) * std::cos((2 * y + 1) * v * pi / 16);
                }
            }
            const int expected = 128 + static_cast<int>(std::lround(sum / 4));
            const int got = rgb[static_cast<std::size_t>(y * 16 + x) * 3];
            worst = std::max(worst, std::abs(expected - got));
        }
    }
    SH_CHECK(worst <= 1);
    // The other luma blocks stay grey.
    SH_CHECK(rgb[(8 * 16 + 8) * 3] == 128);
}

SH_TEST(mdec_refuses_a_bad_header_and_survives_a_cut_stream) {
    std::vector<std::uint8_t> rgb(16 * 16 * 3, 9);
    BitWriter zero_scale;
    zero_scale.header(0, 2);
    grey_macroblock(zero_scale, 10);
    SH_CHECK(!MdecDecoder().decode(zero_scale.bytes(), 16, 16, rgb.data()));
    BitWriter version3;
    version3.header(1, 3);
    grey_macroblock(version3, 10);
    SH_CHECK(!MdecDecoder().decode(version3.bytes(), 16, 16, rgb.data()));
    const std::uint8_t tiny[6] = {};
    SH_CHECK(!MdecDecoder().decode(tiny, 16, 16, rgb.data()));
    SH_CHECK(rgb[0] == 9 && rgb.back() == 9);

    // A stream that stops after its header reads as zeros: every block is a zero DC and no code.
    BitWriter cut;
    cut.header(1, 2);
    std::vector<std::uint8_t> wide(32 * 16 * 3, 9);
    SH_CHECK(MdecDecoder().decode(cut.bytes(), 32, 16, wide.data()));
    SH_CHECK(wide[0] == 128 && wide.back() == 128);
}

namespace {

// A sound sector: 18 groups, every unit with the same parameter byte and the same nibble.
std::vector<std::uint8_t> sound_sector(std::uint8_t coding, std::uint8_t parameter, std::uint8_t even_nibble,
                                       std::uint8_t odd_nibble) {
    std::vector<std::uint8_t> sector(kMovieSectorBytes, 0);
    sector[2] = 0x64;
    sector[3] = coding;
    for (int g = 0; g < 18; ++g) {
        std::uint8_t* group = sector.data() + 8 + g * 128;
        std::memset(group, parameter, 16);
        std::memset(group + 16, static_cast<std::uint8_t>(odd_nibble << 4 | even_nibble), 112);
    }
    return sector;
}

}  // namespace

SH_TEST(xa_plain_units_are_the_nibbles_scaled) {
    // Filter 0, range 12: a sample is its nibble as a signed 4-bit number. Stereo: even units left, odd right.
    const std::vector<std::uint8_t> sector = sound_sector(0x01, 0x0C, 0x3, 0xE);
    std::int16_t out[kMovieSectorSamples];
    MovieSoundDecoder decoder;
    SH_CHECK(decoder.decode_sector(sector.data(), out) == kMovieSectorSamples);
    bool as_expected = true;
    for (int i = 0; i < kMovieSectorSamples; i += 2) as_expected = as_expected && out[i] == 3 && out[i + 1] == -2;
    SH_CHECK(as_expected);

    const auto coding = MovieSoundDecoder::coding(sector.data());
    SH_CHECK(coding && coding->stereo && coding->sample_rate == 37800 && coding->bits == 4);
    const std::vector<std::uint8_t> slow_mono = sound_sector(0x04, 0x0C, 1, 1);
    const auto slow = MovieSoundDecoder::coding(slow_mono.data());
    SH_CHECK(slow && !slow->stereo && slow->sample_rate == 18900);
}

SH_TEST(xa_prediction_runs_on_across_units_and_sectors) {
    // Mono, filter 1 (60/64 of the sample before), range 0: every nibble 1 adds 4096.
    const std::vector<std::uint8_t> sector = sound_sector(0x00, 0x10, 0x1, 0x1);
    std::int16_t out[kMovieSectorSamples];
    MovieSoundDecoder decoder;
    SH_CHECK(decoder.decode_sector(sector.data(), out) == kMovieSectorSamples);
    std::int32_t expected = 0;
    bool same = true;
    for (int i = 0; i < 200; ++i) {
        expected = 4096 + ((expected * 60 + 32) >> 6);
        same = same && out[i] == (expected > 32767 ? 32767 : expected);
    }
    SH_CHECK(same);
    SH_CHECK(out[0] == 4096 && out[1] == 4096 + 3840);
    // The history carries into the next sector; reset() drops it.
    std::int16_t next[kMovieSectorSamples];
    decoder.decode_sector(sector.data(), next);
    SH_CHECK(next[0] != 4096);
    decoder.reset();
    decoder.decode_sector(sector.data(), next);
    SH_CHECK(next[0] == 4096);

    const std::vector<std::uint8_t> eight_bit = sound_sector(0x10, 0x0C, 1, 1);
    SH_CHECK(decoder.decode_sector(eight_bit.data(), next) == 0);

    // A movie keeps the history unclamped: after a peak that clips, the prediction runs on the value before the
    // clip. Filter 1, range 0, nibble 7 adds 28672 a sample: 28672, then 28672 + 26880 = 55552 (heard as 32767),
    // then 28672 + ((55552 * 60 + 32) >> 6) = 80752, where a clamped history would give 28672 + 30719.
    const std::vector<std::uint8_t> loud = sound_sector(0x00, 0x10, 0x7, 0x7);
    decoder.reset();
    decoder.decode_sector(loud.data(), next);
    SH_CHECK(next[0] == 28672 && next[1] == 32767 && next[2] == 32767);
    const std::vector<std::uint8_t> falling = sound_sector(0x00, 0x10, 0x8, 0x8);
    std::int32_t unclamped = 0;
    for (int i = 0; i < kMovieSectorSamples; ++i) unclamped = 28672 + ((unclamped * 60 + 32) >> 6);
    decoder.decode_sector(falling.data(), next);
    const std::int32_t first_after = -32768 + ((unclamped * 60 + 32) >> 6);
    SH_CHECK(next[0] == (first_after > 32767 ? 32767 : first_after) && unclamped > 32767);
}

namespace {

void put16(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(std::uint8_t* p, std::uint32_t v) {
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}

using Sector = std::vector<std::uint8_t>;

// Sector `place` of `count` of frame `number`: the chunk is filled with `fill`, and the header repeats the
// chunk's first bytes as a first sector's must.
Sector picture_sector(std::uint32_t number, int place, int count, std::uint32_t words, std::uint8_t fill) {
    Sector s(kMovieSectorBytes, 0);
    s[2] = 0x48;
    std::uint8_t* h = s.data() + 8;
    put16(h, 0x0160);
    put16(h + 2, 0x8001);
    put16(h + 4, static_cast<std::uint32_t>(place));
    put16(h + 6, static_cast<std::uint32_t>(count));
    put32(h + 8, number);
    put32(h + 12, words);
    put16(h + 16, 320);
    put16(h + 18, 208);
    std::memset(h + 32, fill, StrStream::kChunkBytes);
    std::memset(h + 20, fill, 8);
    return s;
}

Sector filler_sector() { return Sector(kMovieSectorBytes, 0); }

StrStream stream_over(const std::vector<Sector>& sectors) {
    return StrStream(static_cast<std::uint32_t>(sectors.size()), [&sectors](std::uint32_t index, std::uint8_t* dst) {
        std::memcpy(dst, sectors[index].data(), kMovieSectorBytes);
        return true;
    });
}

}  // namespace

SH_TEST(str_stream_joins_sectors_and_passes_sound_on) {
    std::vector<Sector> sectors;
    sectors.push_back(picture_sector(1, 0, 2, 1500, 0x11));
    sectors.push_back(sound_sector(0x01, 0x0C, 1, 1));
    sectors.push_back(filler_sector());
    sectors.push_back(picture_sector(1, 1, 2, 1500, 0x22));
    sectors.push_back(picture_sector(2, 0, 1, 5000, 0x33));  // claims more words than its one sector holds
    sectors.push_back(sound_sector(0x01, 0x0C, 2, 2));

    StrStream stream = stream_over(sectors);
    int sounds = 0;
    stream.set_sound_sink([&](const std::uint8_t* sector) {
        ++sounds;
        SH_CHECK(sector[3] == 0x01);
    });
    StrFrame frame;
    SH_CHECK(stream.next(frame) == StrStream::Status::Frame);
    SH_CHECK(frame.number == 1 && frame.width == 320 && frame.height == 208);
    SH_CHECK(frame.bitstream.size() == 3000);
    SH_CHECK(frame.bitstream[0] == 0x11 && frame.bitstream[2015] == 0x11 && frame.bitstream[2016] == 0x22);
    SH_CHECK(sounds == 1);
    SH_CHECK(stream.next(frame) == StrStream::Status::Frame);
    SH_CHECK(frame.number == 2 && frame.bitstream.size() == StrStream::kChunkBytes);
    SH_CHECK(stream.next(frame) == StrStream::Status::End);
    SH_CHECK(sounds == 2);
    SH_CHECK(stream.next(frame) == StrStream::Status::End);
}

SH_TEST(str_stream_handles_broken_frames) {
    // A first sector whose header does not repeat the chunk's start: the whole frame is passed over.
    {
        std::vector<Sector> sectors;
        sectors.push_back(picture_sector(1, 0, 2, 2016, 0x11));
        sectors[0][8 + 20] ^= 0xFF;
        sectors.push_back(picture_sector(1, 1, 2, 2016, 0x12));
        sectors.push_back(picture_sector(2, 0, 1, 1008, 0x21));
        StrStream stream = stream_over(sectors);
        StrFrame frame;
        SH_CHECK(stream.next(frame) == StrStream::Status::Frame);
        SH_CHECK(frame.number == 2 && frame.bitstream.size() == 2016);
    }
    // The next frame starts before this one is whole: what was gathered goes out, then the next frame.
    {
        std::vector<Sector> sectors;
        sectors.push_back(picture_sector(1, 0, 3, 3000, 0x11));
        sectors.push_back(picture_sector(2, 0, 1, 1008, 0x21));
        StrStream stream = stream_over(sectors);
        StrFrame frame;
        SH_CHECK(stream.next(frame) == StrStream::Status::Frame);
        SH_CHECK(frame.number == 1 && frame.bitstream.size() == StrStream::kChunkBytes);
        SH_CHECK(stream.next(frame) == StrStream::Status::Frame);
        SH_CHECK(frame.number == 2);
    }
    // The file ends inside a frame, or a sector cannot be read.
    {
        std::vector<Sector> sectors;
        sectors.push_back(picture_sector(1, 0, 3, 3000, 0x11));
        StrStream stream = stream_over(sectors);
        StrFrame frame;
        SH_CHECK(stream.next(frame) == StrStream::Status::Frame);
        SH_CHECK(stream.next(frame) == StrStream::Status::End);
        StrStream unreadable(4, [](std::uint32_t, std::uint8_t*) { return false; });
        SH_CHECK(unreadable.next(frame) == StrStream::Status::End);
    }
    // Sectors out of their order, and a frame of too many sectors.
    {
        std::vector<Sector> sectors;
        sectors.push_back(picture_sector(1, 0, 3, 3000, 0x11));
        sectors.push_back(picture_sector(1, 2, 3, 3000, 0x11));
        StrStream stream = stream_over(sectors);
        StrFrame frame;
        SH_CHECK(stream.next(frame) == StrStream::Status::OutOfOrder);

        std::vector<Sector> huge;
        for (int i = 0; i <= StrStream::kMaxFrameSectors; ++i) huge.push_back(picture_sector(1, i, 40, 40000, 0x11));
        StrStream big = stream_over(huge);
        SH_CHECK(big.next(frame) == StrStream::Status::TooLarge);
    }
}

// The decoders against the disc: every picture and every sound sample of three movies, as one hash each. The
// numbers are reference hashes of a known-good decode.
SH_TEST(disc_movies_decode_as_before) {
    namespace fs = std::filesystem;
    const fs::path path = fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).bin";
    if (!fs::exists(path)) {
        SH_SKIP("data/Silent Hill (v1.1).bin is not present");
    }
    const auto image = sh::disc::Image::open(path);
    SH_CHECK(image != nullptr);
    if (!image) return;

    struct Golden {
        const char* name;
        std::uint32_t first_sector, sectors;
        int frames, sound_sectors;
        std::uint64_t pictures, sound;
    };
    const Golden golden[] = {
        {"M2_01190", 0x2b72f, 1190, 119, 19, 0x2b3be35446075223ull, 0xfdfaa20bdd959183ull},
        {"M7_01536", 0x2e41d, 1536, 150, 192, 0xb5b5115ee4698991ull, 0x4ea8a5afc5ce9763ull},
        {"Z4_01590", 0x38940, 1590, 159, 180, 0xf7949f8f178b0ea9ull, 0x3faba0f2a95d1703ull},
    };
    for (const Golden& g : golden) {
        StrStream stream(g.sectors, [&](std::uint32_t index, std::uint8_t* dst) {
            std::uint8_t raw[2352];
            if (!image->read_raw(g.first_sector + index, raw)) return false;
            std::memcpy(dst, raw + 16, kMovieSectorBytes);
            return true;
        });
        MovieSoundDecoder xa;
        std::uint64_t sound_hash = kFnvStart;
        int sound_sectors = 0;
        stream.set_sound_sink([&](const std::uint8_t* sector) {
            std::int16_t samples[kMovieSectorSamples];
            const int count = xa.decode_sector(sector, samples);
            sound_hash = fnv(sound_hash, samples, static_cast<std::size_t>(count) * sizeof(std::int16_t));
            ++sound_sectors;
        });
        MdecDecoder decoder;
        StrFrame frame;
        std::vector<std::uint8_t> rgb;
        std::uint64_t picture_hash = kFnvStart;
        int frames = 0;
        bool all_decoded = true;
        while (stream.next(frame) == StrStream::Status::Frame) {
            rgb.resize(static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 3);
            all_decoded = decoder.decode(frame.bitstream, frame.width, frame.height, rgb.data()) && all_decoded;
            picture_hash = fnv(picture_hash, rgb.data(), rgb.size());
            ++frames;
        }
        SH_CHECK(all_decoded);
        SH_CHECK(frames == g.frames);
        SH_CHECK(sound_sectors == g.sound_sectors);
        SH_CHECK(picture_hash == g.pictures);
        SH_CHECK(sound_hash == g.sound);
    }
}
