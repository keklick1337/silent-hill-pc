// The replacement movies' side of src/media/fmv/codec: the AVI reader, the uncompressed pictures, the sound
// samples, the movie list and the choice of a replacement file.

#include "framework/sh_test.h"

#include "media/fmv/codec/avi_file.h"
#include "media/fmv/codec/movie_list.h"
#include "media/fmv/codec/pcm.h"
#include "media/fmv/codec/raw_video.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace sh::fmv;
using Bytes = std::vector<std::uint8_t>;

class MemorySource final : public sh::ByteSource {
public:
    explicit MemorySource(Bytes bytes) : bytes_(std::move(bytes)) {}
    std::uint64_t size() const override { return bytes_.size(); }
    bool read_at(std::uint64_t offset, void* dst, std::size_t count) const override {
        if (offset > bytes_.size() || count > bytes_.size() - offset) return false;
        std::memcpy(dst, bytes_.data() + offset, count);
        return true;
    }

private:
    Bytes bytes_;
};

void u16(Bytes& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}
void u32(Bytes& out, std::uint32_t v) {
    u16(out, v & 0xFFFF);
    u16(out, v >> 16);
}
void tag(Bytes& out, const char* name) { out.insert(out.end(), name, name + 4); }

// id, size, payload, and the pad byte of an odd payload.
Bytes chunk(const char* id, const Bytes& payload) {
    Bytes out;
    tag(out, id);
    u32(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    if (payload.size() & 1) out.push_back(0);
    return out;
}
Bytes list(const char* id, const char* type, const std::vector<Bytes>& children) {
    Bytes payload;
    tag(payload, type);
    for (const Bytes& child : children) payload.insert(payload.end(), child.begin(), child.end());
    return chunk(id, payload);
}

Bytes main_header(std::uint32_t microseconds, std::uint32_t frames) {
    Bytes p(56, 0);
    Bytes head;
    u32(head, microseconds);
    std::memcpy(p.data(), head.data(), 4);
    head.clear();
    u32(head, frames);
    std::memcpy(p.data() + 16, head.data(), 4);
    return chunk("avih", p);
}
Bytes stream_header(const char* type) {
    Bytes p(56, 0);
    std::memcpy(p.data(), type, 4);
    return chunk("strh", p);
}
Bytes picture_format(std::int32_t width, std::int32_t height, int bits, const char* fourcc) {
    Bytes p;
    u32(p, 40);
    u32(p, static_cast<std::uint32_t>(width));
    u32(p, static_cast<std::uint32_t>(height));
    u16(p, 1);
    u16(p, static_cast<std::uint32_t>(bits));
    p.insert(p.end(), fourcc, fourcc + 4);
    p.resize(40, 0);
    return chunk("strf", p);
}
Bytes sound_format(int format, int channels, int rate, int bits, int subformat = -1) {
    Bytes p;
    u16(p, static_cast<std::uint32_t>(format));
    u16(p, static_cast<std::uint32_t>(channels));
    u32(p, static_cast<std::uint32_t>(rate));
    u32(p, static_cast<std::uint32_t>(rate * channels * bits / 8));
    u16(p, static_cast<std::uint32_t>(channels * bits / 8));
    u16(p, static_cast<std::uint32_t>(bits));
    if (subformat >= 0) {
        u16(p, 22);
        u16(p, static_cast<std::uint32_t>(bits));
        u32(p, 3);
        u16(p, static_cast<std::uint32_t>(subformat));
        p.resize(40, 0);
    }
    return chunk("strf", p);
}

Bytes headers(const Bytes& sound) {
    return list("LIST", "hdrl",
                {main_header(40000, 3), list("LIST", "strl", {stream_header("vids"), picture_format(4, -2, 24, "MJPG")}),
                 chunk("JUNK", Bytes(10, 0xEE)), list("LIST", "strl", {stream_header("auds"), sound}),
                 list("LIST", "odml", {chunk("dmlh", Bytes(4, 0))})});
}

sh::Result<AviFile> open_bytes(Bytes file) { return AviFile::open(std::make_unique<MemorySource>(std::move(file))); }

Bytes riff(const char* type, const std::vector<Bytes>& children) { return list("RIFF", type, children); }

}  // namespace

SH_TEST(avi_reads_formats_and_chunks_in_file_order) {
    Bytes file = riff("AVI ", {headers(sound_format(1, 2, 44100, 16)),
                               list("LIST", "movi",
                                    {chunk("00dc", {1, 2, 3}), chunk("01wb", {4, 5, 6, 7}),
                                     list("LIST", "rec ", {chunk("00db", {8}), chunk("01wb", {9, 10})}),
                                     chunk("ix00", Bytes(6, 0x77)), chunk("00pc", {1, 1}), chunk("00dc", {}),
                                     chunk("00dc", {11, 12})}),
                               chunk("idx1", Bytes(32, 0xFF))});
    const Bytes second = riff("AVIX", {list("LIST", "movi", {chunk("00dc", {13}), chunk("01wb", {14, 15})})});
    file.insert(file.end(), second.begin(), second.end());

    const sh::Result<AviFile> opened = open_bytes(file);
    SH_CHECK(static_cast<bool>(opened));
    if (!opened) return;
    const AviFile& avi = opened.value();
    SH_CHECK(avi.microseconds_per_frame() == 40000 && avi.declared_frames() == 3);
    SH_CHECK(avi.video().width == 4 && avi.video().height == -2 && avi.video().bits_per_pixel == 24);
    SH_CHECK(std::memcmp(avi.video().fourcc.data(), "MJPG", 4) == 0);
    SH_CHECK(avi.audio().format_tag == 1 && avi.audio().channels == 2 && avi.audio().sample_rate == 44100 &&
             avi.audio().bits_per_sample == 16);

    // Index chunks, palette changes and empty chunks are not part of the run.
    using Kind = AviFile::ChunkKind;
    const std::vector<std::pair<Kind, Bytes>> expected = {
        {Kind::Picture, {1, 2, 3}}, {Kind::Sound, {4, 5, 6, 7}}, {Kind::Picture, {8}}, {Kind::Sound, {9, 10}},
        {Kind::Picture, {11, 12}},  {Kind::Picture, {13}},       {Kind::Sound, {14, 15}}};
    SH_CHECK(avi.chunks().size() == expected.size());
    Bytes payload;
    for (std::size_t i = 0; i < expected.size() && i < avi.chunks().size(); ++i) {
        avi.read(avi.chunks()[i], payload);
        SH_CHECK(avi.chunks()[i].kind == expected[i].first);
        SH_CHECK(payload == expected[i].second);
    }
}

SH_TEST(avi_takes_the_subformat_of_an_extensible_sound_format) {
    const Bytes file = riff("AVI ", {headers(sound_format(0xFFFE, 6, 48000, 32, 3)), list("LIST", "movi", {chunk("00dc", {1})})});
    const sh::Result<AviFile> opened = open_bytes(file);
    SH_CHECK(static_cast<bool>(opened));
    if (!opened) return;
    SH_CHECK(opened.value().audio().format_tag == 3 && opened.value().audio().channels == 6 &&
             opened.value().audio().bits_per_sample == 32);
}

SH_TEST(avi_refuses_other_files_and_survives_cut_ones) {
    SH_CHECK(!AviFile::open(nullptr));
    SH_CHECK(!open_bytes({}));
    SH_CHECK(!open_bytes(riff("WAVE", {chunk("data", {1, 2})})));
    // Headers and no data.
    SH_CHECK(!open_bytes(riff("AVI ", {headers(sound_format(1, 2, 44100, 16))})));

    // A file cut in the middle of its last chunk: the chunks before it play, the cut one is zero-filled.
    Bytes file = riff("AVI ", {headers(sound_format(1, 2, 44100, 16)),
                               list("LIST", "movi", {chunk("00dc", {1, 2, 3, 4}), chunk("00dc", Bytes(100, 0x55))})});
    file.resize(file.size() - 60);
    const sh::Result<AviFile> opened = open_bytes(file);
    SH_CHECK(static_cast<bool>(opened));
    if (!opened) return;
    SH_CHECK(opened.value().chunks().size() == 2);
    Bytes payload;
    opened.value().read(opened.value().chunks()[1], payload);
    SH_CHECK(payload.size() == 100 && payload[0] == 0x55 && payload[39] == 0x55 && payload[40] == 0 && payload[99] == 0);
    // Every prefix of the file opens or fails; none hangs or reads outside.
    for (std::size_t length = 0; length < file.size(); length += 7) {
        Bytes prefix(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(length));
        (void)open_bytes(std::move(prefix));
    }
}

namespace {

AviVideoFormat format_of(int width, int height, int bits, const char* fourcc) {
    AviVideoFormat f;
    f.width = width;
    f.height = height;
    f.bits_per_pixel = bits;
    std::memcpy(f.fourcc.data(), fourcc, 4);
    return f;
}

}  // namespace

SH_TEST(avi_codecs_are_known_by_their_fourcc) {
    SH_CHECK(avi_codec(format_of(8, 8, 24, "MJPG")) == AviCodec::MotionJpeg);
    SH_CHECK(avi_codec(format_of(8, 8, 24, "mjpg")) == AviCodec::MotionJpeg);
    SH_CHECK(avi_codec(format_of(8, 8, 24, "dmb1")) == AviCodec::MotionJpeg);
    SH_CHECK(avi_codec(format_of(8, 8, 24, "\0\0\0\0")) == AviCodec::Rgb);
    SH_CHECK(avi_codec(format_of(8, 8, 32, "DIB ")) == AviCodec::Rgb);
    SH_CHECK(avi_codec(format_of(8, 8, 16, "\0\0\0\0")) == AviCodec::Unsupported);
    SH_CHECK(avi_codec(format_of(8, 8, 16, "YUYV")) == AviCodec::Yuy2);
    SH_CHECK(avi_codec(format_of(8, 8, 16, "UYVY")) == AviCodec::Uyvy);
    SH_CHECK(avi_codec(format_of(8, 8, 12, "IYUV")) == AviCodec::I420);
    SH_CHECK(avi_codec(format_of(8, 8, 12, "YV12")) == AviCodec::Yv12);
    SH_CHECK(avi_codec(format_of(8, 8, 12, "NV12")) == AviCodec::Nv12);
    SH_CHECK(avi_codec(format_of(8, 8, 24, "H264")) == AviCodec::Unsupported);
}

SH_TEST(raw_rgb_pictures_are_flipped_and_reordered) {
    // 3x2, 24 bits: rows of 9 bytes padded to 12, blue first, bottom row first.
    Bytes dib(24, 0);
    for (std::uint8_t i = 0; i < 9; ++i) {
        dib[i] = static_cast<std::uint8_t>(100 + i);     // the bottom row
        dib[12 + i] = static_cast<std::uint8_t>(10 + i);  // the top row
    }
    Bytes rgb;
    SH_CHECK(unpack_raw_frame(AviCodec::Rgb, format_of(3, 2, 24, "\0\0\0\0"), dib, rgb));
    SH_CHECK(rgb.size() == 18);
    SH_CHECK(rgb[0] == 12 && rgb[1] == 11 && rgb[2] == 10);
    SH_CHECK(rgb[9] == 102 && rgb[10] == 101 && rgb[11] == 100 && rgb[17] == 106);
    // A negative height: the rows already run top to bottom.
    SH_CHECK(unpack_raw_frame(AviCodec::Rgb, format_of(3, -2, 24, "\0\0\0\0"), dib, rgb));
    SH_CHECK(rgb[0] == 102 && rgb[9] == 12);
    // 32 bits: the fourth byte is dropped.
    const Bytes bgra = {1, 2, 3, 255, 4, 5, 6, 255};
    SH_CHECK(unpack_raw_frame(AviCodec::Rgb, format_of(2, 1, 32, "\0\0\0\0"), bgra, rgb));
    SH_CHECK(rgb == Bytes({3, 2, 1, 6, 5, 4}));
    // Too short a chunk, and codecs that are not uncompressed.
    dib.pop_back();
    SH_CHECK(!unpack_raw_frame(AviCodec::Rgb, format_of(3, 2, 24, "\0\0\0\0"), dib, rgb));
    SH_CHECK(!unpack_raw_frame(AviCodec::MotionJpeg, format_of(3, 2, 24, "MJPG"), dib, rgb));
    SH_CHECK(!unpack_raw_frame(AviCodec::Rgb, format_of(0, 2, 24, "\0\0\0\0"), dib, rgb));
}

SH_TEST(raw_yuv_pictures_use_the_limited_range_matrix) {
    Bytes rgb;
    // Black, white and a grey: luma 16 is 0, 235 is 255.
    const Bytes yuy2 = {16, 128, 235, 128};
    SH_CHECK(unpack_raw_frame(AviCodec::Yuy2, format_of(2, 1, 16, "YUY2"), yuy2, rgb));
    SH_CHECK(rgb == Bytes({0, 0, 0, 255, 255, 255}));
    const Bytes uyvy = {128, 16, 128, 235};
    SH_CHECK(unpack_raw_frame(AviCodec::Uyvy, format_of(2, 1, 16, "UYVY"), uyvy, rgb));
    SH_CHECK(rgb == Bytes({0, 0, 0, 255, 255, 255}));
    SH_CHECK(!unpack_raw_frame(AviCodec::Yuy2, format_of(3, 1, 16, "YUY2"), Bytes(6, 0), rgb));  // an odd width

    // 2x2 with one chroma sample: a strong V is red, a strong U is blue; YV12 has the planes the other way round.
    const Bytes planar = {81, 81, 81, 81, 90, 240};  // Y, U = 90, V = 240
    SH_CHECK(unpack_raw_frame(AviCodec::I420, format_of(2, 2, 12, "I420"), planar, rgb));
    SH_CHECK(rgb.size() == 12 && rgb[0] > 240 && rgb[1] < 10 && rgb[2] < 10 && rgb[9] == rgb[0]);
    SH_CHECK(unpack_raw_frame(AviCodec::Nv12, format_of(2, 2, 12, "NV12"), planar, rgb));
    SH_CHECK(rgb[0] > 240 && rgb[2] < 10);
    SH_CHECK(unpack_raw_frame(AviCodec::Yv12, format_of(2, 2, 12, "YV12"), planar, rgb));
    SH_CHECK(rgb[0] < 80 && rgb[2] > 240);
    SH_CHECK(!unpack_raw_frame(AviCodec::I420, format_of(2, 2, 12, "I420"), Bytes(5, 0), rgb));
}

SH_TEST(movie_sound_gain_and_narrowing) {
    SH_CHECK(movie_gain(1.0f, true) == 0.625f);
    SH_CHECK(movie_gain(0.5f, false) == 0.5f);

    std::int16_t s16[3] = {1000, -1000, 32767};
    apply_gain(s16, sizeof(s16), SampleFormat::S16, 1.0f);
    SH_CHECK(s16[0] == 1000 && s16[2] == 32767);
    apply_gain(s16, sizeof(s16), SampleFormat::S16, 0.625f);
    SH_CHECK(s16[0] == 625 && s16[1] == -625 && s16[2] == 20479);
    apply_gain(s16, sizeof(s16), SampleFormat::S16, -3.0f);
    SH_CHECK(s16[0] == 0 && s16[1] == 0 && s16[2] == 0);

    std::uint8_t u8[3] = {128, 228, 28};
    apply_gain(u8, sizeof(u8), SampleFormat::U8, 0.5f);
    SH_CHECK(u8[0] == 128 && u8[1] == 178 && u8[2] == 78);
    float f32[2] = {0.5f, -1.0f};
    apply_gain(f32, sizeof(f32), SampleFormat::F32, 0.5f);
    SH_CHECK(f32[0] == 0.25f && f32[1] == -0.5f);

    std::vector<std::int16_t> narrow;
    const Bytes s24 = {0xAA, 0x34, 0x12, 0xBB, 0x00, 0x80};
    narrow_to_s16(s24, 24, narrow);
    SH_CHECK(narrow.size() == 2 && narrow[0] == 0x1234 && narrow[1] == -32768);
    const Bytes s32 = {0xAA, 0xBB, 0xFE, 0xFF};
    narrow_to_s16(s32, 32, narrow);
    SH_CHECK(narrow.size() == 1 && narrow[0] == -2);
}

SH_TEST(movie_list_reads_the_game_data) {
    std::ifstream in(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata" / "config" / "movie_files.json");
    SH_CHECK(in.good());
    std::stringstream text;
    text << in.rdbuf();
    const sh::Result<MovieList> parsed = MovieList::parse(text.str());
    SH_CHECK(static_cast<bool>(parsed));
    if (!parsed) return;
    const std::vector<Movie>& movies = parsed.value().movies();
    SH_CHECK(movies.size() == 21);
    if (movies.size() != 21) return;
    SH_CHECK(movies[0].file == "XA/C1_20670" && movies[0].name == "C1_20670" && movies[0].sectors == 20670);
    SH_CHECK(movies[2].name == "M1_03500" && movies[20].name == "ZZ_14239" && movies[20].sectors == 14239);
    // The file's name on the disc ends with its length in sectors.
    for (const Movie& movie : movies) SH_CHECK(std::stoul(movie.name.substr(3)) == movie.sectors);
    SH_CHECK(parsed.value().find("XA/M7_01536") == &movies[8]);
    SH_CHECK(parsed.value().find("XA/05_02152") == nullptr);
    SH_CHECK(parsed.value().find("M7_01536") == nullptr);

    SH_CHECK(MovieList::builtin().movies().empty());
    SH_CHECK(!MovieList::parse("[]"));
    SH_CHECK(!MovieList::parse("{\"movies\": [{\"file\": \"XA/A\"}]}"));
    SH_CHECK(!MovieList::parse("{\"movies\": [{\"file\": \"XA/A\", \"sectors\": 0}]}"));
    SH_CHECK(!MovieList::parse("{\"movies\": [{\"file\": \"XA/\", \"sectors\": 5}]}"));
}

SH_TEST(movie_frame_limit_follows_the_game) {
    SH_CHECK(default_frame_limit(345) == 338);
    SH_CHECK(default_frame_limit(7) == 0 && default_frame_limit(0) == 0);
    // With a limit of 338 the last frame shown is number 336.
    SH_CHECK(!past_frame_limit(336, 338));
    SH_CHECK(past_frame_limit(337, 338));
    SH_CHECK(!past_frame_limit(100000, 0));
}

SH_TEST(movie_replacement_order) {
    std::map<std::string, std::string> mod_files;
    std::set<std::string> loose_files;
    std::vector<std::string> asked;
    ReplacementSources sources;
    sources.mod_file = [&](const std::string& path) -> std::optional<std::string> {
        asked.push_back(path);
        const auto found = mod_files.find(path);
        return found == mod_files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    sources.loose_file = [&](const std::string& path) { return loose_files.count(path) != 0; };
    using Kind = MovieReplacement::Kind;

    SH_CHECK(find_replacement("M1_03500", sources).kind == Kind::None);
    SH_CHECK(asked.front() == "FMV/M1_03500.avi" && asked[1] == "FMV/M1_03500.AVI" && asked[2] == "FMV/M1_03500.mp4");

    // Loose files: an .avi in any folder before a container in any folder.
    loose_files = {"M1_03500.avi", "gamedata/fmv/M1_03500.mkv"};
    MovieReplacement r = find_replacement("M1_03500", sources);
    SH_CHECK(r.kind == Kind::Avi && r.path == "M1_03500.avi");
    loose_files = {"../gamedata/fmv/M1_03500.mov", "gamedata/fmv/M1_03500.webm", "gamedata/fmv/M1_03500.mkv"};
    r = find_replacement("M1_03500", sources);
    SH_CHECK(r.kind == Kind::Container && r.path == "gamedata/fmv/M1_03500.mkv");

    // A mod's file before any loose one, whatever the formats; the mod's .avi before its .mp4.
    loose_files = {"gamedata/fmv/M1_03500.avi"};
    mod_files = {{"FMV/M1_03500.mp4", "/mods/a/FMV/M1_03500.mp4"}};
    r = find_replacement("M1_03500", sources);
    SH_CHECK(r.kind == Kind::Container && r.path == "/mods/a/FMV/M1_03500.mp4");
    mod_files["FMV/M1_03500.AVI"] = "/mods/b/FMV/M1_03500.AVI";
    r = find_replacement("M1_03500", sources);
    SH_CHECK(r.kind == Kind::Avi && r.path == "/mods/b/FMV/M1_03500.AVI");

    // Without FFmpeg only .avi files count.
    sources.containers = false;
    mod_files = {{"FMV/M1_03500.mp4", "/mods/a/FMV/M1_03500.mp4"}};
    loose_files = {"gamedata/fmv/M1_03500.mp4"};
    SH_CHECK(find_replacement("M1_03500", sources).kind == Kind::None);
    loose_files.insert("gamedata/fmv/M1_03500.AVI");
    r = find_replacement("M1_03500", sources);
    SH_CHECK(r.kind == Kind::Avi && r.path == "gamedata/fmv/M1_03500.AVI");
    SH_CHECK(find_replacement("M2_01190", sources).kind == Kind::None);
}
