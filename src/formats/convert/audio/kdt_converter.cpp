// KDT1 sequences (Konami's MIDI-like music format, SND/*.KDT) to sh-kdt JSON (lossless) and Standard MIDI.
//
// Layout, as the game's sequencer reads it:
//   "KDT1", u32 size, u32 ticks per quarter, u32 track count, u16 track size per track, then the tracks back to back.
// A track starts with a delta time (MIDI variable-length number) and then has events, each followed by a delta time
// unless the event says otherwise:
//   0x00-0x7F key, vel   note: velocity & 0x7F (0 = note off), vel bit 7 = no delta time follows
//   0xCA                 note off of the last key; a delta follows        0xCB: the same, no delta
//   0xC6 c               MIDI channel c & 15         0xC7 c: tempo (c & 0x7F) * 2 + 2 (the sequencer's unit)
//   0xC8 c               pitch bend c & 0x7F         0xC9 c: program change c & 0x7F
//   0xFF c               end of track                other 0x80 | n, c: control change n = c & 0x7F
// In every event with a value byte c, c bit 7 = no delta time follows. Control 0x63 = 0x14 marks the loop start.
#include "formats/convert/audio/audio_converter.h"

#include "formats/common/crc32.h"
#include "formats/common/json.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace sh::convert::audio {
namespace {

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::string to_hex(std::span<const std::uint8_t> bytes) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (const std::uint8_t b : bytes) {
        s.push_back(digits[b >> 4]);
        s.push_back(digits[b & 15]);
    }
    return s;
}

bool from_hex(std::string_view s, std::vector<std::uint8_t>& out) {
    if (s.size() % 2 != 0) {
        return false;
    }
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < s.size(); i += 2) {
        const int hi = nibble(s[i]), lo = nibble(s[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
    }
    return true;
}

void put_varint(std::vector<std::uint8_t>& out, std::uint32_t v) {
    std::uint8_t tmp[5];
    int n = 0;
    tmp[n++] = static_cast<std::uint8_t>(v & 0x7F);
    while (v >>= 7) {
        tmp[n++] = static_cast<std::uint8_t>(0x80 | (v & 0x7F));
    }
    while (n) {
        out.push_back(tmp[--n]);
    }
}

// One decoded event or delta time. `kind` names it in the JSON; a, b are the raw bytes after the event byte.
struct Event {
    std::string kind;  // wait, note, off, off_nodelta, channel, tempo, bend, program, cc, end, wait_raw
    std::uint32_t value = 0;     // wait: ticks
    std::uint8_t a = 0, b = 0;   // note: key, vel; cc: control, value; others: a = value byte
    std::vector<std::uint8_t> raw;  // wait_raw: the exact varint bytes
};

struct Track {
    std::vector<Event> events;
    std::vector<std::uint8_t> tail;  // bytes after the end event, or what could not be read
};

struct Kdt {
    std::uint32_t size = 0, ticks_per_quarter = 0;
    std::vector<std::uint16_t> track_sizes;
    std::vector<Track> tracks;
    std::vector<std::uint8_t> trailing;  // after the last track
};

// Reads a delta time; non-canonical encodings (a leading 0x80) are kept as raw bytes.
bool read_wait(std::span<const std::uint8_t> t, std::size_t& pos, Track& track) {
    const std::size_t start = pos;
    std::uint32_t value = 0;
    std::uint8_t c;
    int bytes = 0;
    do {
        if (pos >= t.size() || ++bytes > 5) {
            pos = start;
            return false;
        }
        c = t[pos++];
        value = (value << 7) | (c & 0x7Fu);
    } while (c & 0x80);
    std::vector<std::uint8_t> canonical;
    put_varint(canonical, value);
    Event e;
    if (std::equal(canonical.begin(), canonical.end(), t.begin() + static_cast<std::ptrdiff_t>(start),
                   t.begin() + static_cast<std::ptrdiff_t>(pos)) &&
        canonical.size() == pos - start) {
        e.kind = "wait";
        e.value = value;
    } else {
        e.kind = "wait_raw";
        e.raw.assign(t.begin() + static_cast<std::ptrdiff_t>(start), t.begin() + static_cast<std::ptrdiff_t>(pos));
    }
    track.events.push_back(std::move(e));
    return true;
}

Track parse_track(std::span<const std::uint8_t> t) {
    Track track;
    std::size_t pos = 0;
    bool ok = read_wait(t, pos, track);
    while (ok && pos < t.size()) {
        const std::uint8_t c = t[pos++];
        Event e;
        bool delta = true;
        if (!(c & 0x80)) {
            if (pos >= t.size()) {
                --pos;
                break;
            }
            e.kind = "note";
            e.a = c;
            e.b = t[pos++];
            delta = !(e.b & 0x80);
        } else if (c == 0xCA || c == 0xCB) {
            e.kind = c == 0xCA ? "off" : "off_nodelta";
            delta = c == 0xCA;
        } else {
            if (pos >= t.size()) {
                --pos;
                break;
            }
            const std::uint8_t v = t[pos++];
            switch (c) {
                case 0xC6: e.kind = "channel"; break;
                case 0xC7: e.kind = "tempo"; break;
                case 0xC8: e.kind = "bend"; break;
                case 0xC9: e.kind = "program"; break;
                case 0xFF: e.kind = "end"; break;
                default: e.kind = "cc"; break;
            }
            if (e.kind == "cc") {
                e.a = static_cast<std::uint8_t>(c & 0x7F);
                e.b = v;
            } else {
                e.a = v;
            }
            delta = !(v & 0x80);
            if (c == 0xFF) {
                track.events.push_back(std::move(e));
                break;
            }
        }
        track.events.push_back(std::move(e));
        if (delta) {
            ok = read_wait(t, pos, track);
        }
    }
    track.tail.assign(t.begin() + static_cast<std::ptrdiff_t>(pos), t.end());
    return track;
}

std::vector<std::uint8_t> write_track(const Track& track) {
    std::vector<std::uint8_t> out;
    for (const Event& e : track.events) {
        if (e.kind == "wait") {
            put_varint(out, e.value);
        } else if (e.kind == "wait_raw") {
            out.insert(out.end(), e.raw.begin(), e.raw.end());
        } else if (e.kind == "note") {
            out.push_back(e.a);
            out.push_back(e.b);
        } else if (e.kind == "off") {
            out.push_back(0xCA);
        } else if (e.kind == "off_nodelta") {
            out.push_back(0xCB);
        } else if (e.kind == "cc") {
            out.push_back(static_cast<std::uint8_t>(0x80 | e.a));
            out.push_back(e.b);
        } else {
            const std::uint8_t c = e.kind == "channel" ? 0xC6 : e.kind == "tempo" ? 0xC7 : e.kind == "bend" ? 0xC8
                                   : e.kind == "program" ? 0xC9 : 0xFF;
            out.push_back(c);
            out.push_back(e.a);
        }
    }
    out.insert(out.end(), track.tail.begin(), track.tail.end());
    return out;
}

bool looks_like_kdt(std::span<const std::uint8_t> raw) {
    if (raw.size() < 16 || raw[0] != 'K' || raw[1] != 'D' || raw[2] != 'T' || raw[3] != '1') {
        return false;
    }
    const std::uint32_t tracks = le32(raw.data() + 12);
    if (tracks == 0 || tracks > 32 || 16 + tracks * 2 > raw.size()) {
        return false;
    }
    std::size_t total = 16 + tracks * 2;
    for (std::uint32_t i = 0; i < tracks; ++i) {
        total += static_cast<std::size_t>(raw[16 + i * 2] | (raw[17 + i * 2] << 8));
    }
    return total <= raw.size();
}

Kdt parse_kdt(std::span<const std::uint8_t> raw) {
    Kdt k;
    k.size = le32(raw.data() + 4);
    k.ticks_per_quarter = le32(raw.data() + 8);
    const std::uint32_t tracks = le32(raw.data() + 12);
    std::size_t pos = 16 + tracks * 2;
    for (std::uint32_t i = 0; i < tracks; ++i) {
        const auto n = static_cast<std::uint16_t>(raw[16 + i * 2] | (raw[17 + i * 2] << 8));
        k.track_sizes.push_back(n);
        k.tracks.push_back(parse_track(raw.subspan(pos, n)));
        pos += n;
    }
    k.trailing.assign(raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.end());
    return k;
}

std::vector<std::uint8_t> write_kdt(const Kdt& k) {
    std::vector<std::uint8_t> out = {'K', 'D', 'T', '1'};
    const auto put32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    };
    put32(k.size);
    put32(k.ticks_per_quarter);
    put32(static_cast<std::uint32_t>(k.tracks.size()));
    std::vector<std::vector<std::uint8_t>> bodies;
    for (const Track& t : k.tracks) {
        bodies.push_back(write_track(t));
    }
    for (const auto& b : bodies) {
        out.push_back(static_cast<std::uint8_t>(b.size()));
        out.push_back(static_cast<std::uint8_t>(b.size() >> 8));
    }
    for (const auto& b : bodies) {
        out.insert(out.end(), b.begin(), b.end());
    }
    out.insert(out.end(), k.trailing.begin(), k.trailing.end());
    return out;
}

// Standard MIDI file, format 1, one MIDI track per KDT track, the KDT's ticks per quarter as division.
// Tempo: the sequencer runs at (c & 0x7F) * 2 + 2 in the unit that the SMF path computes as bpm * 100 / 115
// (smf_mid metaevent 0x51), so the MIDI tempo is that value * 1.15 beats per minute.
std::vector<std::uint8_t> to_midi(const Kdt& k) {
    std::vector<std::uint8_t> out = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1};
    out.push_back(static_cast<std::uint8_t>(k.tracks.size() >> 8));
    out.push_back(static_cast<std::uint8_t>(k.tracks.size()));
    const std::uint32_t division = std::min<std::uint32_t>(k.ticks_per_quarter ? k.ticks_per_quarter : 480, 0x7FFF);
    out.push_back(static_cast<std::uint8_t>(division >> 8));
    out.push_back(static_cast<std::uint8_t>(division));
    for (std::size_t ti = 0; ti < k.tracks.size(); ++ti) {
        std::vector<std::uint8_t> body;
        std::uint32_t pending = 0;
        int channel = static_cast<int>(ti & 15);
        int last_key = 0;
        const auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
            put_varint(body, pending);
            pending = 0;
            body.insert(body.end(), bytes);
        };
        for (const Event& e : k.tracks[ti].events) {
            const auto ch = static_cast<std::uint8_t>(channel);
            if (e.kind == "wait") {
                pending += e.value;
            } else if (e.kind == "wait_raw") {
                std::uint32_t v = 0;
                for (const std::uint8_t c : e.raw) {
                    v = (v << 7) | (c & 0x7Fu);
                }
                pending += v;
            } else if (e.kind == "note") {
                last_key = e.a;
                const std::uint8_t vel = e.b & 0x7F;
                emit({static_cast<std::uint8_t>((vel ? 0x90 : 0x80) | ch), e.a, vel ? vel : std::uint8_t{0}});
            } else if (e.kind == "off" || e.kind == "off_nodelta") {
                emit({static_cast<std::uint8_t>(0x80 | ch), static_cast<std::uint8_t>(last_key), 0});
            } else if (e.kind == "channel") {
                channel = e.a & 15;
            } else if (e.kind == "tempo") {
                const double bpm = ((e.a & 0x7F) * 2 + 2) * 1.15;
                const auto usq = static_cast<std::uint32_t>(60000000.0 / bpm + 0.5);
                emit({0xFF, 0x51, 3, static_cast<std::uint8_t>(usq >> 16), static_cast<std::uint8_t>(usq >> 8),
                      static_cast<std::uint8_t>(usq)});
            } else if (e.kind == "bend") {
                // The game's pitch_bend gets only the 7-bit MSB.
                emit({static_cast<std::uint8_t>(0xE0 | ch), 0, static_cast<std::uint8_t>(e.a & 0x7F)});
            } else if (e.kind == "program") {
                emit({static_cast<std::uint8_t>(0xC0 | ch), static_cast<std::uint8_t>(e.a & 0x7F)});
            } else if (e.kind == "cc") {
                emit({static_cast<std::uint8_t>(0xB0 | ch), e.a, static_cast<std::uint8_t>(e.b & 0x7F)});
            } else if (e.kind == "end") {
                break;
            }
        }
        emit({0xFF, 0x2F, 0});
        out.insert(out.end(), {'M', 'T', 'r', 'k'});
        const auto n = static_cast<std::uint32_t>(body.size());
        out.insert(out.end(), {static_cast<std::uint8_t>(n >> 24), static_cast<std::uint8_t>(n >> 16),
                               static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n)});
        out.insert(out.end(), body.begin(), body.end());
    }
    return out;
}

std::string upper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

std::string kdt_json(const Kdt& k, std::string_view game_path, std::span<const std::uint8_t> raw, const std::string& midi) {
    json::Writer w(true);
    w.begin_object();
    w.member("format", "sh-kdt/1");
    w.key("source").inline_next().begin_object();
    w.member("path", std::string(game_path));
    w.member("size", raw.size());
    char crc[16];
    std::snprintf(crc, sizeof crc, "%08X", crc32(raw.data(), raw.size()));
    w.member("crc32", crc);
    w.end_object();
    w.member("midi", midi);
    w.member("help",
             "Events in file order. [\"wait\", ticks] delta time; [\"note\", key, vel] (vel & 127 = velocity, 0 = off; "
             "vel & 128 = no delta follows); [\"off\"] / [\"off_nodelta\"] last key off; [\"channel\" | \"tempo\" | "
             "\"bend\" | \"program\" | \"end\", c] (value c & 127, c & 128 = no delta follows; tempo unit: "
             "(c & 127) * 2 + 2 = bpm / 1.15); [\"cc\", control, c] (control 99 = 20: loop start, 30: loop end); "
             "[\"wait_raw\", hex] a delta time stored in a non-canonical way.");
    w.member("size_field", k.size);
    w.member("ticks_per_quarter", k.ticks_per_quarter);
    w.key("tracks").begin_array();
    for (const Track& t : k.tracks) {
        w.begin_object();
        w.key("events").begin_array();
        for (const Event& e : t.events) {
            w.inline_next().begin_array();
            w.value(e.kind);
            if (e.kind == "wait") {
                w.value(e.value);
            } else if (e.kind == "wait_raw") {
                w.value(to_hex(e.raw));
            } else if (e.kind == "note" || e.kind == "cc") {
                w.value(e.a).value(e.b);
            } else if (e.kind != "off" && e.kind != "off_nodelta") {
                w.value(e.a);
            }
            w.end_array();
        }
        w.end_array();
        w.member("tail", to_hex(t.tail));
        w.end_object();
    }
    w.end_array();
    w.member("trailing", to_hex(k.trailing));
    w.end_object();
    return w.take() + "\n";
}

Result<Kdt> kdt_from_json(std::string_view text) {
    using Out = Result<Kdt>;
    auto doc = json::parse(text);
    if (!doc) {
        return Out::fail("sh-kdt: " + doc.error());
    }
    const json::Value& d = doc.value();
    const json::Value* size = d.find("size_field");
    const json::Value* tpq = d.find("ticks_per_quarter");
    const json::Value* tracks = d.find("tracks");
    const json::Value* trailing = d.find("trailing");
    if (!size || !size->is_integer() || !tpq || !tpq->is_integer() || !tracks || !tracks->is_array() || !trailing ||
        !trailing->is_string()) {
        return Out::fail("sh-kdt: needs size_field, ticks_per_quarter, tracks and trailing");
    }
    Kdt k;
    k.size = static_cast<std::uint32_t>(size->integer());
    k.ticks_per_quarter = static_cast<std::uint32_t>(tpq->integer());
    if (!from_hex(trailing->string(), k.trailing)) {
        return Out::fail("sh-kdt: trailing is not hex");
    }
    const auto byte = [](const json::Value& v, std::uint8_t& out) {
        if (!v.is_integer() || v.integer() < 0 || v.integer() > 255) {
            return false;
        }
        out = static_cast<std::uint8_t>(v.integer());
        return true;
    };
    for (const json::Value& tv : tracks->array()) {
        Track t;
        const json::Value* events = tv.find("events");
        const json::Value* tail = tv.find("tail");
        if (!events || !events->is_array() || !tail || !tail->is_string() || !from_hex(tail->string(), t.tail)) {
            return Out::fail("sh-kdt: a track needs events and a hex tail");
        }
        for (const json::Value& ev : events->array()) {
            if (!ev.is_array() || ev.array().empty() || !ev.array()[0].is_string()) {
                return Out::fail("sh-kdt: an event is [kind, ...]");
            }
            const auto& a = ev.array();
            Event e;
            e.kind = a[0].string();
            bool ok = true;
            if (e.kind == "wait") {
                ok = a.size() == 2 && a[1].is_integer() && a[1].integer() >= 0 && a[1].integer() <= 0x0FFFFFFF;
                if (ok) {
                    e.value = static_cast<std::uint32_t>(a[1].integer());
                }
            } else if (e.kind == "wait_raw") {
                ok = a.size() == 2 && a[1].is_string() && from_hex(a[1].string(), e.raw);
            } else if (e.kind == "note" || e.kind == "cc") {
                ok = a.size() == 3 && byte(a[1], e.a) && byte(a[2], e.b) && (e.kind == "cc" ? e.a < 0x80 : e.a < 0x80);
            } else if (e.kind == "off" || e.kind == "off_nodelta") {
                ok = a.size() == 1;
            } else if (e.kind == "channel" || e.kind == "tempo" || e.kind == "bend" || e.kind == "program" ||
                       e.kind == "end") {
                ok = a.size() == 2 && byte(a[1], e.a);
            } else {
                ok = false;
            }
            if (!ok) {
                return Out::fail("sh-kdt: bad event \"" + e.kind + "\"");
            }
            t.events.push_back(std::move(e));
        }
        k.tracks.push_back(std::move(t));
    }
    if (k.tracks.empty() || k.tracks.size() > 32) {
        return Out::fail("sh-kdt: 1 to 32 tracks");
    }
    return Out::ok(std::move(k));
}

class KdtConverter final : public Converter {
public:
    std::string_view name() const override { return "kdt"; }
    bool lossless() const override { return true; }
    bool accepts(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        return upper(game_path).ends_with(".KDT") && looks_like_kdt(raw);
    }
    Result<std::vector<Output>> decode(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        using Out = Result<std::vector<Output>>;
        const Kdt k = parse_kdt(raw);
        if (write_kdt(k) != std::vector<std::uint8_t>(raw.begin(), raw.end())) {
            return Out::fail(std::string(game_path) + ": KDT does not rebuild from its events");
        }
        std::string stem(game_path);
        stem = stem.substr(0, stem.rfind('.'));
        const std::size_t slash = stem.find_last_of('/');
        const std::string midi_name = (slash == std::string::npos ? stem : stem.substr(slash + 1)) + ".mid";
        const std::string json = kdt_json(k, game_path, raw, midi_name);
        std::vector<Output> out;
        out.push_back({"audio/seq/" + stem + ".kdt.json", std::vector<std::uint8_t>(json.begin(), json.end())});
        out.push_back({"audio/seq/" + stem + ".mid", to_midi(k)});
        return Out::ok(std::move(out));
    }
    Result<std::vector<std::uint8_t>> encode(std::span<const Output> outputs) const override {
        using Out = Result<std::vector<std::uint8_t>>;
        for (const Output& o : outputs) {
            if (o.relative_path.ends_with(".kdt.json")) {
                auto k = kdt_from_json(std::string_view(reinterpret_cast<const char*>(o.bytes.data()), o.bytes.size()));
                if (!k) {
                    return Out::fail(o.relative_path + ": " + k.error());
                }
                return Out::ok(write_kdt(k.value()));
            }
        }
        return Out::fail("kdt: no .kdt.json among the outputs");
    }
};

}  // namespace

std::unique_ptr<Converter> make_kdt_converter() { return std::make_unique<KdtConverter>(); }

}  // namespace sh::convert::audio
