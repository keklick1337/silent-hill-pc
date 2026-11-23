#include "formats/convert/archive/archive_converters.h"

#include "formats/common/crc32.h"
#include "formats/common/json.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace sh::convert::archive {
namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

std::string file_name(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

std::string without_extension(std::string_view path) {
    const std::string p(path);
    const std::size_t dot = p.rfind('.');
    return dot == std::string::npos ? p : p.substr(0, dot);
}

std::string to_hex(std::span<const std::uint8_t> bytes) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(bytes.size() * 2);
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

std::string crc_text(std::span<const std::uint8_t> raw) {
    char crc[16];
    std::snprintf(crc, sizeof crc, "%08X", crc32(raw.data(), raw.size()));
    return crc;
}

std::string_view as_text(const std::vector<std::uint8_t>& b) {
    return std::string_view(reinterpret_cast<const char*>(b.data()), b.size());
}

// ---- CMP ----

class CmpConverter final : public sh::convert::Converter {
public:
    std::string_view name() const override { return "cmp"; }
    bool lossless() const override { return true; }

    bool accepts(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        const std::string u = upper(game_path);
        return u.size() > 4 && u.ends_with(".CMP") && raw.size() >= 5;
    }

    Result<std::vector<Output>> decode(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        using Out = Result<std::vector<Output>>;
        std::size_t consumed = 0;
        auto data = lzss_decode(raw, &consumed);
        if (!data) {
            return Out::fail(std::string(game_path) + ": " + data.error());
        }
        const std::vector<std::uint8_t> again = lzss_encode(data.value());
        if (again.size() != consumed || !std::equal(again.begin(), again.end(), raw.begin())) {
            return Out::fail(std::string(game_path) + ": the LZSS encoder does not rebuild this stream");
        }
        const bool tim = data.value().size() >= 8 && data.value()[0] == 0x10 && data.value()[1] == 0 &&
                         data.value()[2] == 0 && data.value()[3] == 0;
        const std::string base = "unpacked/" + without_extension(game_path);
        const std::string unpacked = base + (tim ? ".tim" : ".bin");
        json::Writer w(true);
        w.begin_object();
        w.member("format", "sh-cmp/1");
        w.key("source").inline_next().begin_object();
        w.member("path", std::string(game_path));
        w.member("size", raw.size());
        w.member("crc32", crc_text(raw));
        w.end_object();
        w.member("codec", "LZSS (Okumura 1989: N 4096, F 18, threshold 2, zero window), u32 unpacked size first");
        w.member("unpacked", file_name(unpacked));
        w.member("unpacked_size", data.value().size());
        w.member("unpacked_crc32", crc_text(data.value()));
        w.member("content", tim ? "tim" : "unknown");
        w.member("stream_size", consumed);
        w.member("tail", to_hex(raw.subspan(consumed)));
        w.end_object();
        const std::string json = w.take() + "\n";
        std::vector<Output> out;
        out.push_back({unpacked, std::move(data.value())});
        out.push_back({base + ".cmp.json", std::vector<std::uint8_t>(json.begin(), json.end())});
        return Out::ok(std::move(out));
    }

    Result<std::vector<std::uint8_t>> encode(std::span<const Output> outputs) const override {
        using Out = Result<std::vector<std::uint8_t>>;
        const Output* meta = nullptr;
        for (const Output& o : outputs) {
            if (o.relative_path.ends_with(".cmp.json")) {
                meta = &o;
            }
        }
        if (!meta) {
            return Out::fail("cmp: no .cmp.json among the outputs");
        }
        auto doc = json::parse(as_text(meta->bytes));
        if (!doc) {
            return Out::fail("cmp: " + doc.error());
        }
        const json::Value* unpacked = doc.value().find("unpacked");
        const json::Value* tail = doc.value().find("tail");
        std::vector<std::uint8_t> tail_bytes;
        if (!unpacked || !unpacked->is_string() || !tail || !tail->is_string() || !from_hex(tail->string(), tail_bytes)) {
            return Out::fail("cmp: .cmp.json needs \"unpacked\" and a hex \"tail\"");
        }
        for (const Output& o : outputs) {
            if (file_name(o.relative_path) == unpacked->string()) {
                std::vector<std::uint8_t> packed = lzss_encode(o.bytes);
                packed.insert(packed.end(), tail_bytes.begin(), tail_bytes.end());
                return Out::ok(std::move(packed));
            }
        }
        return Out::fail("cmp: the unpacked file " + unpacked->string() + " is not among the outputs");
    }
};

// ---- DAT (demos) ----

enum class FieldType : std::uint8_t { U8, S8, U16, S16, U32, Hex };

struct Field {
    const char* name;
    std::size_t offset;
    std::size_t size;  // bytes; 1, 2 or 4 for numbers
    FieldType type;
};

// The disc's demo state, laid out as the original's s_DemoState with its s_OptionsConfig spelled out; the save
// record (s_Savegame) stays hex.
constexpr Field kDemoFields[] = {
    {"controller_config", 0x00, 0x1C, FieldType::Hex},
    {"screen_position_x", 0x1C, 1, FieldType::S8},
    {"screen_position_y", 0x1D, 1, FieldType::S8},
    {"sound_type", 0x1E, 1, FieldType::U8},
    {"volume_bgm", 0x1F, 1, FieldType::U8},
    {"volume_se", 0x20, 1, FieldType::U8},
    {"vibration_enabled", 0x21, 1, FieldType::U8},
    {"brightness", 0x22, 1, FieldType::U8},
    {"extra_weapon_ctrl", 0x23, 1, FieldType::U8},
    {"extra_blood_color", 0x24, 1, FieldType::U8},
    {"auto_load", 0x25, 1, FieldType::S8},
    {"unused_26", 0x26, 1, FieldType::S8},
    {"extra_options_enabled", 0x27, 1, FieldType::U8},
    {"extra_view_ctrl", 0x28, 1, FieldType::S8},
    {"extra_view_mode", 0x29, 1, FieldType::S8},
    {"extra_retreat_turn", 0x2A, 1, FieldType::S8},
    {"extra_walk_run_ctrl", 0x2B, 1, FieldType::S8},
    {"extra_auto_aiming", 0x2C, 1, FieldType::S8},
    {"extra_bullet_adjust", 0x2D, 1, FieldType::S8},
    {"seen_game_over_tips", 0x2E, 2, FieldType::U16},
    {"pad_30", 0x30, 4, FieldType::U32},  // unknown purpose
    {"pal_language_id", 0x34, 1, FieldType::S8},
    {"pad_35", 0x35, 3, FieldType::Hex},
    {"pad_38", 0x38, 200, FieldType::Hex},
    {"savegame", 0x100, 0x27C, FieldType::Hex},
    {"pad_37c", 0x37C, 1148, FieldType::Hex},
    {"frame_count", 0x7F8, 4, FieldType::U32},
    {"rand_seed", 0x7FC, 2, FieldType::U16},
    {"pad_7fe", 0x7FE, 2, FieldType::Hex},
};
constexpr std::size_t kDemoStateBytes = 2048;
constexpr std::size_t kPlayFrameBytes = 16;

std::uint32_t read_le(std::span<const std::uint8_t> b, std::size_t at, std::size_t n) {
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < n; ++i) {
        v |= static_cast<std::uint32_t>(b[at + i]) << (8 * i);
    }
    return v;
}
void write_le(std::vector<std::uint8_t>& b, std::size_t at, std::size_t n, std::uint32_t v) {
    for (std::size_t i = 0; i < n; ++i) {
        b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

std::int64_t field_value(std::span<const std::uint8_t> b, const Field& f) {
    const std::uint32_t u = read_le(b, f.offset, f.size);
    switch (f.type) {
        case FieldType::S8: return static_cast<std::int8_t>(u);
        case FieldType::S16: return static_cast<std::int16_t>(u);
        default: return u;
    }
}

bool all_zero(std::span<const std::uint8_t> b) {
    return std::all_of(b.begin(), b.end(), [](std::uint8_t v) { return v == 0; });
}

class DemoConverter final : public sh::convert::Converter {
public:
    std::string_view name() const override { return "demo"; }
    bool lossless() const override { return true; }

    bool accepts(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        const std::string n = upper(file_name(game_path));
        if (!n.ends_with(".DAT")) {
            return false;
        }
        if (n.starts_with("DEMO")) {
            return raw.size() == kDemoStateBytes;
        }
        return n.starts_with("PLAY") && !raw.empty() && raw.size() % kPlayFrameBytes == 0;
    }

    Result<std::vector<Output>> decode(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        const bool state = upper(file_name(game_path)).starts_with("DEMO");
        json::Writer w(true);
        w.begin_object();
        w.member("format", "sh-demo/1");
        w.key("source").inline_next().begin_object();
        w.member("path", std::string(game_path));
        w.member("size", raw.size());
        w.member("crc32", crc_text(raw));
        w.end_object();
        if (state) {
            w.member("kind", "demo_state");
            w.member("help", "s_DemoState (include/bodyprog/demo.h): the options and the savegame a demo starts from");
            w.key("fields").begin_object();
            for (const Field& f : kDemoFields) {
                const auto bytes = raw.subspan(f.offset, f.size);
                if (f.type == FieldType::Hex) {
                    if (all_zero(bytes) && f.size > 8) {
                        w.key(f.name).inline_next().begin_object().member("zeros", f.size).end_object();
                    } else {
                        w.member(f.name, to_hex(bytes));
                    }
                } else {
                    w.member(f.name, field_value(raw, f));
                }
            }
            w.end_object();
        } else {
            w.member("kind", "demo_input");
            w.member("help",
                     "s_DemoPlaybackFrame (include/bodyprog/demo.h), one per frame: [pad status, pad type byte "
                     "(terminal type << 4 | bytes / 2), buttons (active low), right x, right y, left x, left y, "
                     "expected game state, present interval, two spare bytes (often a repeated pair), rand seed]");
            w.key("frames").begin_array();
            for (std::size_t i = 0; i + kPlayFrameBytes <= raw.size(); i += kPlayFrameBytes) {
                w.inline_next().begin_array();
                w.value(raw[i]).value(raw[i + 1]).value(read_le(raw, i + 2, 2));
                for (std::size_t k = 4; k < 8; ++k) {
                    w.value(raw[i + k]);
                }
                w.value(static_cast<int>(static_cast<std::int8_t>(raw[i + 8])));
                w.value(raw[i + 9]);
                w.value(static_cast<int>(static_cast<std::int8_t>(raw[i + 10])));
                w.value(static_cast<int>(static_cast<std::int8_t>(raw[i + 11])));
                w.value(read_le(raw, i + 12, 4));
                w.end_array();
            }
            w.end_array();
        }
        w.end_object();
        const std::string json = w.take() + "\n";
        std::vector<Output> out;
        out.push_back({"demo/" + without_extension(game_path) + ".dat.json", std::vector<std::uint8_t>(json.begin(), json.end())});
        return Result<std::vector<Output>>::ok(std::move(out));
    }

    Result<std::vector<std::uint8_t>> encode(std::span<const Output> outputs) const override {
        using Out = Result<std::vector<std::uint8_t>>;
        const Output* doc_out = nullptr;
        for (const Output& o : outputs) {
            if (o.relative_path.ends_with(".dat.json")) {
                doc_out = &o;
            }
        }
        if (!doc_out) {
            return Out::fail("demo: no .dat.json among the outputs");
        }
        auto doc = json::parse(as_text(doc_out->bytes));
        if (!doc) {
            return Out::fail("demo: " + doc.error());
        }
        const json::Value* kind = doc.value().find("kind");
        if (!kind || !kind->is_string()) {
            return Out::fail("demo: no kind");
        }
        if (kind->string() == "demo_state") {
            const json::Value* fields = doc.value().find("fields");
            if (!fields || !fields->is_object()) {
                return Out::fail("demo: no fields");
            }
            std::vector<std::uint8_t> b(kDemoStateBytes, 0);
            for (const Field& f : kDemoFields) {
                const json::Value* v = fields->find(f.name);
                if (!v) {
                    return Out::fail(std::string("demo: field ") + f.name + " is missing");
                }
                if (f.type == FieldType::Hex) {
                    std::vector<std::uint8_t> bytes;
                    if (v->is_object()) {
                        const json::Value* z = v->find("zeros");
                        if (!z || !z->is_integer() || static_cast<std::size_t>(z->integer()) != f.size) {
                            return Out::fail(std::string("demo: field ") + f.name + " zeros count");
                        }
                        bytes.assign(f.size, 0);
                    } else if (!v->is_string() || !from_hex(v->string(), bytes) || bytes.size() != f.size) {
                        return Out::fail(std::string("demo: field ") + f.name + " must be " + std::to_string(f.size) +
                                         " bytes of hex");
                    }
                    std::copy(bytes.begin(), bytes.end(), b.begin() + static_cast<std::ptrdiff_t>(f.offset));
                } else {
                    if (!v->is_integer()) {
                        return Out::fail(std::string("demo: field ") + f.name + " must be an integer");
                    }
                    write_le(b, f.offset, f.size, static_cast<std::uint32_t>(v->integer()));
                }
            }
            return Out::ok(std::move(b));
        }
        if (kind->string() == "demo_input") {
            const json::Value* frames = doc.value().find("frames");
            if (!frames || !frames->is_array()) {
                return Out::fail("demo: no frames");
            }
            std::vector<std::uint8_t> b;
            for (const json::Value& f : frames->array()) {
                if (!f.is_array() || f.array().size() != 12 ||
                    !std::all_of(f.array().begin(), f.array().end(), [](const json::Value& x) { return x.is_integer(); })) {
                    return Out::fail("demo: a frame is 12 integers");
                }
                const auto& a = f.array();
                std::vector<std::uint8_t> frame(kPlayFrameBytes, 0);
                frame[0] = static_cast<std::uint8_t>(a[0].integer());
                frame[1] = static_cast<std::uint8_t>(a[1].integer());
                write_le(frame, 2, 2, static_cast<std::uint32_t>(a[2].integer()));
                for (std::size_t k = 0; k < 4; ++k) {
                    frame[4 + k] = static_cast<std::uint8_t>(a[3 + k].integer());
                }
                for (std::size_t k = 0; k < 4; ++k) {
                    frame[8 + k] = static_cast<std::uint8_t>(a[7 + k].integer());
                }
                write_le(frame, 12, 4, static_cast<std::uint32_t>(a[11].integer()));
                b.insert(b.end(), frame.begin(), frame.end());
            }
            return Out::ok(std::move(b));
        }
        return Out::fail("demo: unknown kind " + kind->string());
    }
};

}  // namespace

std::unique_ptr<sh::convert::Converter> make_cmp_converter() { return std::make_unique<CmpConverter>(); }
std::unique_ptr<sh::convert::Converter> make_demo_converter() { return std::make_unique<DemoConverter>(); }

}  // namespace sh::convert::archive
