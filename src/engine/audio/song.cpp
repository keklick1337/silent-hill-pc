#include "engine/audio/song.h"

#include "formats/common/json.h"
#include "formats/convert/audio/audio_converter.h"

#include <string>

namespace sh::audio {
namespace {

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::uint32_t varint_bytes(std::uint32_t v) {
    std::uint32_t n = 1;
    while (v >>= 7) {
        ++n;
    }
    return n;
}

bool byte_at(const std::vector<json::Value>& a, std::size_t i, std::uint8_t& out) {
    if (i >= a.size() || !a[i].is_integer() || a[i].integer() < 0 || a[i].integer() > 255) {
        return false;
    }
    out = static_cast<std::uint8_t>(a[i].integer());
    return true;
}

}  // namespace

Result<Song> load_song(std::string_view json_text) {
    using R = Result<Song>;
    auto doc = json::parse(json_text);
    if (!doc) {
        return R::fail("song: " + doc.error());
    }
    const json::Value& d = doc.value();
    const json::Value* size = d.find("size_field");
    const json::Value* tpq = d.find("ticks_per_quarter");
    const json::Value* tracks = d.find("tracks");
    if (!size || !size->is_integer() || !tpq || !tpq->is_integer() || !tracks || !tracks->is_array() ||
        tracks->array().empty() || tracks->array().size() > 32) {
        return R::fail("song: needs size_field, ticks_per_quarter and 1 to 32 tracks");
    }
    Song song;
    song.ticks_per_quarter = static_cast<std::uint32_t>(tpq->integer());
    const auto size_field = static_cast<std::uint64_t>(size->integer());
    // Byte position of each step in the original file: the sequencer reads nothing at or past the size field.
    std::uint64_t at = 16 + tracks->array().size() * 2;
    for (const json::Value& tv : tracks->array()) {
        const json::Value* events = tv.find("events");
        if (!events || !events->is_array()) {
            return R::fail("song: a track needs events");
        }
        std::vector<SongStep> steps;
        bool cut = false;
        for (const json::Value& ev : events->array()) {
            if (!ev.is_array() || ev.array().empty() || !ev.array()[0].is_string()) {
                return R::fail("song: an event is [kind, ...]");
            }
            const auto& a = ev.array();
            const std::string& kind = a[0].string();
            SongStep s;
            std::uint32_t bytes = 2;
            bool ok = true;
            if (kind == "wait") {
                ok = a.size() == 2 && a[1].is_integer() && a[1].integer() >= 0;
                s.kind = SongStep::Kind::Wait;
                s.ticks = ok ? static_cast<std::uint32_t>(a[1].integer()) : 0;
                bytes = varint_bytes(s.ticks);
            } else if (kind == "wait_raw") {
                ok = a.size() == 2 && a[1].is_string() && a[1].string().size() % 2 == 0;
                s.kind = SongStep::Kind::Wait;
                if (ok) {
                    const std::string& hex = a[1].string();
                    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
                        const int c = hex_digit(hex[i]) * 16 + hex_digit(hex[i + 1]);
                        s.ticks = (s.ticks << 7) | (static_cast<std::uint32_t>(c) & 0x7F);
                    }
                    bytes = static_cast<std::uint32_t>(hex.size() / 2);
                }
            } else if (kind == "note") {
                s.kind = SongStep::Kind::Note;
                ok = byte_at(a, 1, s.a) && byte_at(a, 2, s.b);
            } else if (kind == "cc") {
                s.kind = SongStep::Kind::Control;
                ok = byte_at(a, 1, s.a) && byte_at(a, 2, s.b);
            } else if (kind == "off" || kind == "off_nodelta") {
                s.kind = kind == "off" ? SongStep::Kind::ReleaseLast : SongStep::Kind::ReleaseLastNoDelta;
                bytes = 1;
            } else {
                ok = byte_at(a, 1, s.a);
                if (kind == "channel") s.kind = SongStep::Kind::Channel;
                else if (kind == "tempo") s.kind = SongStep::Kind::Tempo;
                else if (kind == "bend") s.kind = SongStep::Kind::Bend;
                else if (kind == "program") s.kind = SongStep::Kind::Program;
                else if (kind == "end") s.kind = SongStep::Kind::End;
                else ok = false;
            }
            if (!ok) {
                return R::fail("song: bad event \"" + kind + "\"");
            }
            if (!cut && at + bytes > size_field) {
                cut = true;
            }
            if (!cut) {
                steps.push_back(s);
            }
            at += bytes;
        }
        if (const json::Value* tail = tv.find("tail"); tail && tail->is_string()) {
            at += tail->string().size() / 2;
        }
        song.tracks.push_back(std::move(steps));
    }
    return R::ok(std::move(song));
}

Result<Song> parse_kdt(std::span<const std::uint8_t> raw) {
    auto outputs = convert::audio::make_kdt_converter()->decode(raw, "SND/SONG.KDT");
    if (!outputs) {
        return Result<Song>::fail(outputs.error());
    }
    for (const convert::Output& o : outputs.value()) {
        if (o.relative_path.ends_with(".kdt.json")) {
            return load_song(std::string_view(reinterpret_cast<const char*>(o.bytes.data()), o.bytes.size()));
        }
    }
    return Result<Song>::fail("song: the converter wrote no JSON");
}

}  // namespace sh::audio
