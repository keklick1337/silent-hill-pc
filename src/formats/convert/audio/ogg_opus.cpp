#include "formats/convert/audio/ogg_opus.h"

#include "formats/convert/audio/resample.h"

#include <opus.h>
#include <opus_multistream.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <deque>
#include <string_view>

namespace sh::convert::audio {
namespace {

constexpr std::size_t kPageHeader = 27;
constexpr std::uint8_t kContinued = 0x01;
constexpr std::uint8_t kFirstPage = 0x02;
constexpr std::uint8_t kLastPage = 0x04;
constexpr std::uint64_t kNoGranule = ~std::uint64_t{0};
// The longest Opus packet decodes to 120 ms.
constexpr int kMaxPacketFrames = 5760;
constexpr std::size_t kMaxFrames = std::size_t{kOpusRate} * 60 * 30;
constexpr int kFrameSize = 960;  // 20 ms per packet when encoding
constexpr std::size_t kPageBody = 4096;

// The Ogg checksum: CRC-32 with the polynomial 0x04C11DB7, not reflected, starting at 0.
const std::array<std::uint32_t, 256>& crc_table() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t r = i << 24;
            for (int bit = 0; bit < 8; ++bit) {
                r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : r << 1;
            }
            t[i] = r;
        }
        return t;
    }();
    return table;
}

std::uint32_t ogg_crc(std::span<const std::uint8_t> bytes) {
    const auto& table = crc_table();
    std::uint32_t crc = 0;
    for (const std::uint8_t b : bytes) {
        crc = (crc << 8) ^ table[((crc >> 24) ^ b) & 0xFF];
    }
    return crc;
}

std::uint32_t le16(const std::uint8_t* p) { return std::uint32_t{p[0]} | std::uint32_t{p[1]} << 8; }
std::uint32_t le32(const std::uint8_t* p) { return le16(p) | le16(p + 2) << 16; }
std::uint64_t le64(const std::uint8_t* p) { return std::uint64_t{le32(p)} | std::uint64_t{le32(p + 4)} << 32; }

void put16(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}
void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    put16(out, v & 0xFFFF);
    put16(out, v >> 16);
}
void put64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    put32(out, static_cast<std::uint32_t>(v));
    put32(out, static_cast<std::uint32_t>(v >> 32));
}
void put_text(std::vector<std::uint8_t>& out, std::string_view text) {
    out.insert(out.end(), text.begin(), text.end());
}

bool starts_with(std::span<const std::uint8_t> bytes, std::string_view magic) {
    return bytes.size() >= magic.size() && std::memcmp(bytes.data(), magic.data(), magic.size()) == 0;
}

std::optional<std::uint64_t> number(std::string_view text) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

bool same_key(std::string_view comment, std::string_view key) {
    if (comment.size() <= key.size() || comment[key.size()] != '=') {
        return false;
    }
    for (std::size_t i = 0; i < key.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(comment[i])) != key[i]) {
            return false;
        }
    }
    return true;
}

struct Packet {
    std::vector<std::uint8_t> data;
    std::uint64_t granule = kNoGranule;  // the granule of the page it ended on
    bool last_page = false;
};

// Writes pages of one logical stream.
class PageWriter {
public:
    PageWriter(std::vector<std::uint8_t>& out, std::uint32_t serial) : out_(out), serial_(serial) {}

    void packet(std::span<const std::uint8_t> data, std::uint64_t granule) {
        std::size_t at = 0;
        for (;;) {
            if (lacing_.size() == 255) {
                emit(false, true);
            }
            const std::size_t chunk = std::min<std::size_t>(data.size() - at, 255);
            lacing_.push_back(static_cast<std::uint8_t>(chunk));
            body_.insert(body_.end(), data.begin() + static_cast<std::ptrdiff_t>(at),
                         data.begin() + static_cast<std::ptrdiff_t>(at + chunk));
            at += chunk;
            // A segment shorter than 255 bytes (zero included) ends the packet.
            if (chunk < 255) {
                break;
            }
        }
        granule_ = granule;
    }

    std::size_t body_size() const { return body_.size(); }

    void flush(bool last) {
        if (!lacing_.empty() || last) {
            emit(last, false);
        }
    }

private:
    void emit(bool last, bool next_continues) {
        const std::size_t start = out_.size();
        put_text(out_, "OggS");
        out_.push_back(0);
        out_.push_back(static_cast<std::uint8_t>((continued_ ? kContinued : 0) | (first_ ? kFirstPage : 0) |
                                                 (last ? kLastPage : 0)));
        put64(out_, granule_);
        put32(out_, serial_);
        put32(out_, sequence_++);
        put32(out_, 0);
        out_.push_back(static_cast<std::uint8_t>(lacing_.size()));
        out_.insert(out_.end(), lacing_.begin(), lacing_.end());
        out_.insert(out_.end(), body_.begin(), body_.end());
        const std::uint32_t crc = ogg_crc(std::span<const std::uint8_t>(out_).subspan(start));
        for (int i = 0; i < 4; ++i) {
            out_[start + 22 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(crc >> (8 * i));
        }
        lacing_.clear();
        body_.clear();
        granule_ = kNoGranule;
        first_ = false;
        continued_ = next_continues;
    }

    std::vector<std::uint8_t>& out_;
    std::uint32_t serial_;
    std::uint32_t sequence_ = 0;
    std::vector<std::uint8_t> lacing_, body_;
    std::uint64_t granule_ = kNoGranule;
    bool first_ = true;
    bool continued_ = false;
};

}  // namespace

bool is_ogg(std::span<const std::uint8_t> bytes) { return starts_with(bytes, "OggS"); }

struct OggOpusReader::State {
    std::span<const std::uint8_t> bytes;
    std::size_t next_page = 0;
    std::size_t page_number = 0;
    std::optional<std::uint32_t> serial;
    std::vector<std::uint8_t> partial;  // a packet continued on the next page
    std::deque<Packet> packets;
    bool data_end = false;

    int channels = 0;
    std::uint32_t input_rate = 0;
    std::uint32_t pre_skip = 0;
    std::optional<WavLoop> loop;
    OpusMSDecoder* decoder = nullptr;

    std::vector<std::int16_t> pcm;  // decoded frames not handed out yet
    std::size_t pcm_at = 0;         // in samples
    std::uint64_t decoded = 0;      // frames decoded so far, the pre-skip included

    ~State() {
        if (decoder) {
            opus_multistream_decoder_destroy(decoder);
        }
    }

    // Reads the next page of the stream into `packets`; false at the end of the data.
    Result<bool> read_page() {
        using R = Result<bool>;
        for (;;) {
            if (data_end || next_page >= bytes.size()) {
                data_end = true;
                return R::ok(false);
            }
            const std::span<const std::uint8_t> rest = bytes.subspan(next_page);
            const std::string where = "Ogg page " + std::to_string(page_number);
            if (rest.size() < kPageHeader || !starts_with(rest, "OggS") || rest[4] != 0) {
                return R::fail(where + ": not an Ogg page");
            }
            const std::size_t segments = rest[26];
            if (rest.size() < kPageHeader + segments) {
                return R::fail(where + ": cut short");
            }
            std::size_t body = 0;
            for (std::size_t i = 0; i < segments; ++i) {
                body += rest[kPageHeader + i];
            }
            const std::size_t size = kPageHeader + segments + body;
            if (rest.size() < size) {
                return R::fail(where + ": cut short");
            }
            std::vector<std::uint8_t> page(rest.begin(), rest.begin() + static_cast<std::ptrdiff_t>(size));
            const std::uint32_t stored = le32(page.data() + 22);
            std::fill(page.begin() + 22, page.begin() + 26, std::uint8_t{0});
            if (ogg_crc(page) != stored) {
                return R::fail(where + ": bad checksum");
            }
            next_page += size;
            ++page_number;

            const std::uint8_t flags = rest[5];
            const std::uint32_t page_serial = le32(rest.data() + 14);
            if (!serial) {
                if (!(flags & kFirstPage)) {
                    return R::fail(where + ": the stream does not start here");
                }
                serial = page_serial;
            } else if (page_serial != *serial) {
                continue;  // another stream of the file (a video, a second song): not ours
            }
            if (!(flags & kContinued) && !partial.empty()) {
                return R::fail(where + ": a packet of the previous page is not continued");
            }
            const std::uint64_t granule = le64(rest.data() + 6);
            const bool last = (flags & kLastPage) != 0;
            const std::uint8_t* data = rest.data() + kPageHeader + segments;
            for (std::size_t i = 0; i < segments; ++i) {
                const std::uint8_t length = rest[kPageHeader + i];
                partial.insert(partial.end(), data, data + length);
                data += length;
                if (length < 255) {
                    packets.push_back(Packet{std::move(partial), granule, last});
                    partial.clear();
                }
            }
            if (last) {
                data_end = true;
            }
            return R::ok(true);
        }
    }

    Result<std::optional<Packet>> next_packet() {
        using R = Result<std::optional<Packet>>;
        while (packets.empty()) {
            auto more = read_page();
            if (!more) {
                return R::fail(more.error());
            }
            if (!more.value()) {
                return R::ok(std::nullopt);
            }
        }
        Packet p = std::move(packets.front());
        packets.pop_front();
        return R::ok(std::move(p));
    }

    Result<void> parse_head(const std::vector<std::uint8_t>& head) {
        using R = Result<void>;
        if (!starts_with(head, "OpusHead") || head.size() < 19) {
            return R::fail("no OpusHead: not an Opus stream");
        }
        if (head[8] >= 16) {
            return R::fail("OpusHead version " + std::to_string(head[8]) + " is not supported");
        }
        channels = head[9];
        pre_skip = le16(head.data() + 10);
        input_rate = le32(head.data() + 12);
        const auto gain = static_cast<std::int16_t>(le16(head.data() + 16));
        const int family = head[18];
        if (channels < 1 || channels > 2) {
            return R::fail(std::to_string(channels) + " channels: only mono and stereo are played");
        }
        int streams = 1;
        int coupled = channels == 2 ? 1 : 0;
        std::array<unsigned char, 2> mapping{0, 1};
        if (family != 0) {
            if (head.size() < 21 + static_cast<std::size_t>(channels)) {
                return R::fail("OpusHead: channel mapping cut short");
            }
            streams = head[19];
            coupled = head[20];
            for (int c = 0; c < channels; ++c) {
                mapping[static_cast<std::size_t>(c)] = head[21 + static_cast<std::size_t>(c)];
            }
        }
        int error = OPUS_OK;
        decoder = opus_multistream_decoder_create(static_cast<opus_int32>(kOpusRate), channels, streams, coupled,
                                                  mapping.data(), &error);
        if (error != OPUS_OK || decoder == nullptr) {
            decoder = nullptr;
            return R::fail(std::string("OpusHead: ") + opus_strerror(error));
        }
        if (gain != 0) {
            opus_multistream_decoder_ctl(decoder, OPUS_SET_GAIN(gain));
        }
        return R::ok();
    }

    void parse_tags(const std::vector<std::uint8_t>& tags) {
        // Broken tags lose the loop only: the sound itself is still good.
        std::size_t at = 8;
        auto read_length = [&](std::uint32_t& out) {
            if (at + 4 > tags.size()) return false;
            out = le32(tags.data() + at);
            at += 4;
            return true;
        };
        std::uint32_t vendor = 0, count = 0;
        if (!read_length(vendor) || vendor > tags.size() - at) return;
        at += vendor;
        if (!read_length(count)) return;
        std::optional<std::uint64_t> start, length, end;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t size = 0;
            if (!read_length(size) || size > tags.size() - at) return;
            const std::string_view comment(reinterpret_cast<const char*>(tags.data() + at), size);
            at += size;
            if (same_key(comment, "LOOPSTART")) start = number(comment.substr(10));
            if (same_key(comment, "LOOPLENGTH")) length = number(comment.substr(11));
            if (same_key(comment, "LOOPEND")) end = number(comment.substr(8));
        }
        if (start && length && *length > 0) {
            end = *start + *length - 1;
        }
        if (start && end && *end >= *start && *end <= 0xFFFFFFFFu) {
            loop = WavLoop{static_cast<std::uint32_t>(*start), static_cast<std::uint32_t>(*end)};
        }
    }
};

OggOpusReader::OggOpusReader(std::unique_ptr<State> state) : state_(std::move(state)) {}
OggOpusReader::~OggOpusReader() = default;

Result<std::unique_ptr<OggOpusReader>> OggOpusReader::open(std::span<const std::uint8_t> bytes) {
    using R = Result<std::unique_ptr<OggOpusReader>>;
    if (!is_ogg(bytes)) {
        return R::fail("not an Ogg file");
    }
    auto state = std::make_unique<State>();
    state->bytes = bytes;
    auto head = state->next_packet();
    if (!head) {
        return R::fail(head.error());
    }
    if (!head.value()) {
        return R::fail("no OpusHead: empty stream");
    }
    if (auto parsed = state->parse_head(head.value()->data); !parsed) {
        return R::fail(parsed.error());
    }
    auto tags = state->next_packet();
    if (!tags) {
        return R::fail(tags.error());
    }
    if (!tags.value() || !starts_with(tags.value()->data, "OpusTags")) {
        return R::fail("no OpusTags after the OpusHead");
    }
    state->parse_tags(tags.value()->data);
    return R::ok(std::unique_ptr<OggOpusReader>(new OggOpusReader(std::move(state))));
}

int OggOpusReader::channels() const { return state_->channels; }
std::uint32_t OggOpusReader::input_rate() const { return state_->input_rate; }
std::optional<WavLoop> OggOpusReader::loop() const { return state_->loop; }

Result<std::size_t> OggOpusReader::read(std::span<std::int16_t> out) {
    using R = Result<std::size_t>;
    State& s = *state_;
    const std::size_t width = static_cast<std::size_t>(s.channels);
    const std::size_t want = out.size() / width * width;
    std::size_t written = 0;
    while (written < want) {
        if (s.pcm_at < s.pcm.size()) {
            const std::size_t n = std::min(want - written, s.pcm.size() - s.pcm_at);
            std::copy_n(s.pcm.begin() + static_cast<std::ptrdiff_t>(s.pcm_at), n,
                        out.begin() + static_cast<std::ptrdiff_t>(written));
            s.pcm_at += n;
            written += n;
            continue;
        }
        auto packet = s.next_packet();
        if (!packet) {
            return R::fail(packet.error());
        }
        if (!packet.value()) {
            break;
        }
        const Packet& p = *packet.value();
        s.pcm.resize(static_cast<std::size_t>(kMaxPacketFrames) * width);
        const int frames = opus_multistream_decode(s.decoder, p.data.data(), static_cast<opus_int32>(p.data.size()),
                                                   s.pcm.data(), kMaxPacketFrames, 0);
        if (frames < 0) {
            return R::fail("Opus packet: " + std::string(opus_strerror(frames)));
        }
        const std::uint64_t begin = s.decoded;
        s.decoded += static_cast<std::uint64_t>(frames);
        // Keep [pre-skip, end): the end is the granule of the last page, which marks the padding after the sound.
        std::uint64_t end = s.decoded;
        if (p.last_page && p.granule != kNoGranule) {
            end = std::min(end, p.granule);
        }
        const std::uint64_t from = std::max<std::uint64_t>(begin, s.pre_skip);
        if (end <= from) {
            s.pcm.clear();
        } else {
            s.pcm.erase(s.pcm.begin() + static_cast<std::ptrdiff_t>((end - begin) * width), s.pcm.end());
            s.pcm.erase(s.pcm.begin(), s.pcm.begin() + static_cast<std::ptrdiff_t>((from - begin) * width));
        }
        s.pcm_at = 0;
    }
    return R::ok(written / width);
}

Result<Wav> read_ogg_opus(std::span<const std::uint8_t> bytes) {
    using R = Result<Wav>;
    auto opened = OggOpusReader::open(bytes);
    if (!opened) {
        return R::fail(opened.error());
    }
    OggOpusReader& reader = *opened.value();
    Wav wav;
    wav.channels = static_cast<std::uint16_t>(reader.channels());
    wav.sample_rate = kOpusRate;
    std::vector<std::int16_t> block(static_cast<std::size_t>(kMaxPacketFrames) * wav.channels);
    for (;;) {
        auto got = reader.read(block);
        if (!got) {
            return R::fail(got.error());
        }
        if (got.value() == 0) {
            break;
        }
        wav.samples.insert(wav.samples.end(), block.begin(),
                           block.begin() + static_cast<std::ptrdiff_t>(got.value() * wav.channels));
        if (wav.frames() > kMaxFrames) {
            return R::fail("longer than 30 minutes");
        }
    }
    if (const auto loop = reader.loop(); loop && loop->start < wav.frames()) {
        wav.sampler = WavSampler{};
        wav.sampler->loop =
            WavLoop{loop->start, std::min<std::uint32_t>(loop->end, static_cast<std::uint32_t>(wav.frames() - 1))};
    }
    return R::ok(std::move(wav));
}

Result<std::vector<std::uint8_t>> write_ogg_opus(const Wav& wav, const OpusEncoding& encoding) {
    using R = Result<std::vector<std::uint8_t>>;
    const int channels = wav.channels;
    if (channels != 1 && channels != 2) {
        return R::fail("opus: only mono or stereo is written");
    }
    if (wav.sample_rate == 0) {
        return R::fail("opus: no sample rate");
    }
    std::vector<std::int16_t> resampled;
    std::span<const std::int16_t> pcm(wav.samples);
    if (wav.sample_rate != kOpusRate) {
        resampled = resample_pcm(wav.samples, channels, wav.sample_rate, kOpusRate);
        pcm = resampled;
    }
    const std::uint64_t frames = pcm.size() / static_cast<std::size_t>(channels);

    int error = OPUS_OK;
    OpusEncoder* encoder =
        opus_encoder_create(static_cast<opus_int32>(kOpusRate), channels, OPUS_APPLICATION_AUDIO, &error);
    if (error != OPUS_OK || encoder == nullptr) {
        return R::fail(std::string("opus: ") + opus_strerror(error));
    }
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(encoding.bitrate));
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(10));
    opus_int32 lookahead = 0;
    opus_encoder_ctl(encoder, OPUS_GET_LOOKAHEAD(&lookahead));
    const std::uint64_t pre_skip = static_cast<std::uint64_t>(lookahead);

    std::vector<std::uint8_t> out;
    PageWriter pages(out, encoding.serial);

    std::vector<std::uint8_t> head;
    put_text(head, "OpusHead");
    head.push_back(1);
    head.push_back(static_cast<std::uint8_t>(channels));
    put16(head, static_cast<std::uint32_t>(pre_skip));
    put32(head, wav.sample_rate);
    put16(head, 0);
    head.push_back(0);
    pages.packet(head, 0);
    pages.flush(false);

    std::vector<std::string> comments;
    if (wav.sampler && wav.sampler->loop) {
        const std::uint64_t start = resample_position(wav.sampler->loop->start, wav.sample_rate, kOpusRate);
        const std::uint64_t end =
            resample_position(std::uint64_t{wav.sampler->loop->end} + 1, wav.sample_rate, kOpusRate);
        if (end > start) {
            comments.push_back("LOOPSTART=" + std::to_string(start));
            comments.push_back("LOOPLENGTH=" + std::to_string(end - start));
        }
    }
    comments.insert(comments.end(), encoding.comments.begin(), encoding.comments.end());
    std::vector<std::uint8_t> tags;
    put_text(tags, "OpusTags");
    const std::string_view vendor = opus_get_version_string();
    put32(tags, static_cast<std::uint32_t>(vendor.size()));
    put_text(tags, vendor);
    put32(tags, static_cast<std::uint32_t>(comments.size()));
    for (const std::string& c : comments) {
        put32(tags, static_cast<std::uint32_t>(c.size()));
        put_text(tags, c);
    }
    pages.packet(tags, 0);
    pages.flush(false);

    // The decoder's output lags by the pre-skip, so the input runs on in silence until it is all out; the last
    // page's granule cuts that padding again.
    const std::uint64_t total = pre_skip + frames;
    std::vector<std::int16_t> frame(static_cast<std::size_t>(kFrameSize) * static_cast<std::size_t>(channels));
    std::vector<std::uint8_t> packet(4000);
    for (std::uint64_t at = 0; at < total; at += kFrameSize) {
        std::fill(frame.begin(), frame.end(), std::int16_t{0});
        if (at < frames) {
            const std::uint64_t n = std::min<std::uint64_t>(kFrameSize, frames - at);
            std::copy_n(pcm.begin() + static_cast<std::ptrdiff_t>(at * static_cast<std::uint64_t>(channels)),
                        n * static_cast<std::uint64_t>(channels), frame.begin());
        }
        const opus_int32 size =
            opus_encode(encoder, frame.data(), kFrameSize, packet.data(), static_cast<opus_int32>(packet.size()));
        if (size < 0) {
            opus_encoder_destroy(encoder);
            return R::fail(std::string("opus: ") + opus_strerror(size));
        }
        const bool last = at + kFrameSize >= total;
        pages.packet(std::span<const std::uint8_t>(packet.data(), static_cast<std::size_t>(size)),
                     last ? total : at + kFrameSize);
        if (last) {
            pages.flush(true);
        } else if (pages.body_size() >= kPageBody) {
            pages.flush(false);
        }
    }
    if (total == 0) {
        pages.flush(true);
    }
    opus_encoder_destroy(encoder);
    return R::ok(std::move(out));
}

Result<Wav> read_sound(std::span<const std::uint8_t> bytes) {
    return is_ogg(bytes) ? read_ogg_opus(bytes) : read_wav(bytes);
}

}  // namespace sh::convert::audio
