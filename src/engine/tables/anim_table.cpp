#include "engine/tables/anim_table.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace sh::tables {

namespace {

constexpr std::string_view kFormat = "sh-anim-table";
constexpr std::int64_t kVersion = 1;
constexpr std::uint32_t kOverlayBase = 0x800C9578;
constexpr std::size_t kRecordSize = 16;
constexpr std::uint8_t kStatusNone = 0xFF;

// Disc-code addresses of Anim_BlendLinear, Anim_PlaybackOnce, Anim_PlaybackLoop.
constexpr std::uint32_t kDiscPlaybackFuncs[3] = {0x80044CA4, 0x800449F0, 0x80044B38};

constexpr std::string_view kHelp =
    "Entry i is the animation entry for packed status i ((anim << 1) | playing). speed is keyframes per second; "
    "speed_q12 is the same number times 4096 as the game stores it: edit one of them and delete the other, or "
    "keep both equal. link 255 and status 255 mean none; start_kf -1 starts from the current keyframe. "
    "A wrong file is reported in the log and the disc values are used instead. <name>@<MAPn_Snn>.json, when "
    "present, replaces <name>.json while that map overlay is loaded (the disc has a different copy there).";

std::string hex(std::uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%X", v);
    return buf;
}

std::string at(const json::Value& v) {
    if (v.line() == 0) {
        return {};
    }
    return " (line " + std::to_string(v.line()) + ", column " + std::to_string(v.column()) + ")";
}

const char* find_func_name(std::span<const ShAnimFunc> funcs, std::int32_t value) {
    for (const ShAnimFunc& f : funcs) {
        if (f.value == value) {
            return f.name;
        }
    }
    return nullptr;
}

const ShAnimFunc* find_func(std::span<const ShAnimFunc> funcs, std::string_view name) {
    for (const ShAnimFunc& f : funcs) {
        if (name == f.name) {
            return &f;
        }
    }
    return nullptr;
}

// Reads the fields of one entry; the first problem is kept in `error`.
class EntryReader {
public:
    EntryReader(const json::Value& entry, std::size_t index) : entry_(entry), index_(index) {}

    const std::string& error() const { return error_; }
    bool ok() const { return error_.empty(); }

    void fail(std::string_view field, const std::string& what, const json::Value& where) {
        if (error_.empty()) {
            error_ = "entry " + std::to_string(index_) + ", \"" + std::string(field) + "\": " + what + at(where);
        }
    }

    const json::Value* get(std::string_view field, bool required) {
        const json::Value* v = entry_.find(field);
        if (!v && required) {
            fail(field, "missing", entry_);
        }
        return v;
    }

    std::int64_t integer(std::string_view field, std::int64_t lo, std::int64_t hi, bool required = true) {
        const json::Value* v = get(field, required);
        if (!v) {
            return 0;
        }
        if (!v->is_integer()) {
            fail(field, std::string("expected an integer, got ") + (v->is_number() ? "a fraction" : json::Value::type_name(v->type())), *v);
            return 0;
        }
        if (v->integer() < lo || v->integer() > hi) {
            fail(field, std::to_string(v->integer()) + " is outside " + std::to_string(lo) + ".." + std::to_string(hi), *v);
            return 0;
        }
        return v->integer();
    }

    bool boolean(std::string_view field) {
        const json::Value* v = get(field, true);
        if (!v) {
            return false;
        }
        if (!v->is_bool()) {
            fail(field, std::string("expected true or false, got ") + json::Value::type_name(v->type()), *v);
            return false;
        }
        return v->boolean();
    }

    std::string string(std::string_view field, bool required) {
        const json::Value* v = get(field, required);
        if (!v) {
            return {};
        }
        if (!v->is_string()) {
            fail(field, std::string("expected a string, got ") + json::Value::type_name(v->type()), *v);
            return {};
        }
        return v->string();
    }

private:
    const json::Value& entry_;
    std::size_t index_;
    std::string error_;
};

}  // namespace

std::string_view anim_kind_name(AnimKind kind) {
    switch (kind) {
        case AnimKind::BlendLinear: return "blend_linear";
        case AnimKind::PlaybackOnce: return "playback_once";
        case AnimKind::PlaybackLoop: return "playback_loop";
        case AnimKind::None: return "none";
    }
    return "none";
}

Result<AnimTable> anim_table_from_overlay(const ShAnimTableInfo& info, std::span<const std::uint8_t> overlay,
                                          std::uint32_t va, std::span<const ShAnimFunc> funcs) {
    using R = Result<AnimTable>;
    if (va < kOverlayBase) {
        return R::fail("address " + hex(va) + " is below the overlay base " + hex(kOverlayBase));
    }
    const std::size_t offset = va - kOverlayBase;
    if (offset + info.fileCount * kRecordSize > overlay.size()) {
        return R::fail("the overlay is " + std::to_string(overlay.size()) + " bytes, the table ends at " +
                       std::to_string(offset + info.fileCount * kRecordSize));
    }
    auto u32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(overlay[at]) | (static_cast<std::uint32_t>(overlay[at + 1]) << 8) |
               (static_cast<std::uint32_t>(overlay[at + 2]) << 16) | (static_cast<std::uint32_t>(overlay[at + 3]) << 24);
    };
    AnimTable table;
    table.symbol = info.symbol;
    for (std::size_t i = 0; i < info.fileCount; ++i) {
        const std::size_t rec = offset + i * kRecordSize;
        const std::string where = std::string(info.symbol) + "[" + std::to_string(i) + "] at " +
                                  hex(va + static_cast<std::uint32_t>(i * kRecordSize)) + ": ";
        AnimEntry e;
        const std::uint32_t func = u32(rec);
        if (func == 0) {
            e.kind = AnimKind::None;
        } else {
            std::size_t k = 0;
            while (k < 3 && kDiscPlaybackFuncs[k] != func) {
                ++k;
            }
            if (k == 3) {
                return R::fail(where + "playback function " + hex(func) + " is not one of the three");
            }
            e.kind = static_cast<AnimKind>(k);
        }
        e.status = overlay[rec + 4];
        const auto variable = static_cast<std::int8_t>(overlay[rec + 5]);
        if (variable != 0 && variable != 1) {
            return R::fail(where + "variable duration flag " + std::to_string(variable) + " is not a flag");
        }
        e.variable_duration = variable != 0;
        e.link = overlay[rec + 6];
        const auto duration = static_cast<std::int32_t>(u32(rec + 8));
        if (e.variable_duration) {
            const char* name = find_func_name(funcs, duration);
            if (!name) {
                return R::fail(where + "no duration function has the value " + hex(static_cast<std::uint32_t>(duration)));
            }
            e.duration_func = name;
        } else {
            e.speed_q12 = duration;
        }
        e.start_kf = static_cast<std::int16_t>(overlay[rec + 12] | (overlay[rec + 13] << 8));
        e.end_kf = static_cast<std::int16_t>(overlay[rec + 14] | (overlay[rec + 15] << 8));
        table.entries.push_back(std::move(e));
    }
    return R::ok(std::move(table));
}

std::string overlay_of(const ShAnimSource& source) {
    std::string_view file = source.file;
    if (const auto slash = file.rfind('/'); slash != std::string_view::npos) {
        file.remove_prefix(slash + 1);
    }
    if (const auto dot = file.find('.'); dot != std::string_view::npos) {
        file = file.substr(0, dot);
    }
    return std::string(file);
}

std::string write_anim_table(const AnimTable& table, const ShAnimTableInfo& info, std::string_view overlay) {
    json::Writer w(true);
    w.begin_object();
    w.member("format", kFormat);
    w.member("version", kVersion);
    w.member("symbol", table.symbol);
    const ShAnimSource* source = info.sourceCount > 0 ? &info.sources[0] : nullptr;
    if (!overlay.empty()) {
        w.member("overlay", overlay);
        source = nullptr;
        for (std::size_t i = 0; i < info.sourceCount; ++i) {
            if (overlay_of(info.sources[i]) == overlay) {
                source = &info.sources[i];
            }
        }
    }
    if (source) {
        w.key("source").inline_next().begin_object();
        w.member("file", source->file);
        w.member("offset", hex(source->va - kOverlayBase));
        w.member("va", hex(source->va));
        w.member("stride", 16);
        w.end_object();
    }
    if (overlay.empty() && info.sourceCount > 1) {
        w.key("copies").inline_next().begin_array();
        for (std::size_t i = 1; i < info.sourceCount; ++i) {
            w.value(std::string(info.sources[i].file) + "@" + hex(info.sources[i].va));
        }
        w.end_array();
    }
    w.member("help", kHelp);
    w.key("entries").begin_array();
    for (std::size_t i = 0; i < table.entries.size(); ++i) {
        const AnimEntry& e = table.entries[i];
        w.inline_next().begin_object();
        w.member("index", static_cast<std::int64_t>(i));
        w.member("kind", anim_kind_name(e.kind));
        w.member("status", e.status);
        w.member("variable_duration", e.variable_duration);
        w.member("link", e.link);
        if (e.variable_duration) {
            w.member("duration_func", e.duration_func);
        } else {
            w.key("speed").value_shortest(static_cast<double>(e.speed_q12) / 4096.0);
            w.member("speed_q12", e.speed_q12);
        }
        w.member("start_kf", e.start_kf);
        w.member("end_kf", e.end_kf);
        w.end_object();
    }
    w.end_array();
    w.end_object();
    std::string text = w.take();
    text += '\n';
    return text;
}

Result<AnimTable> read_anim_table(std::string_view text, const ShAnimTableInfo& info, std::span<const ShAnimFunc> funcs) {
    using R = Result<AnimTable>;
    auto parsed = json::parse(text);
    if (!parsed) {
        return R::fail("not valid JSON: " + parsed.error());
    }
    const json::Value& root = parsed.value();
    if (!root.is_object()) {
        return R::fail("the file must be a JSON object" + at(root));
    }
    const json::Value* format = root.find("format");
    if (!format || !format->is_string() || format->string() != kFormat) {
        return R::fail("\"format\" must be \"" + std::string(kFormat) + "\"" + (format ? at(*format) : std::string()));
    }
    const json::Value* version = root.find("version");
    if (!version || !version->is_integer() || version->integer() != kVersion) {
        return R::fail("\"version\" must be " + std::to_string(kVersion) + (version ? at(*version) : std::string()));
    }
    AnimTable table;
    table.symbol = info.symbol;
    if (const json::Value* symbol = root.find("symbol")) {
        // Imports before the table had a name wrote the address name of its first disc copy instead.
        char address_name[16] = {};
        if (info.sourceCount > 0) {
            std::snprintf(address_name, sizeof(address_name), "D_%08X", static_cast<unsigned>(info.sources[0].va));
        }
        if (!symbol->is_string() || (symbol->string() != info.symbol && symbol->string() != address_name)) {
            return R::fail("\"symbol\" must be \"" + std::string(info.symbol) + "\": the file belongs to another table" +
                           at(*symbol));
        }
    }
    const json::Value* entries = root.find("entries");
    if (!entries || !entries->is_array()) {
        return R::fail("\"entries\" must be an array" + (entries ? at(*entries) : std::string()));
    }
    if (entries->array().size() != info.fileCount) {
        return R::fail("\"entries\" has " + std::to_string(entries->array().size()) + " entries, the game table has " +
                       std::to_string(info.fileCount) + at(*entries));
    }
    const std::int64_t last_status = static_cast<std::int64_t>(info.linkLimit) - 1;

    for (std::size_t i = 0; i < entries->array().size(); ++i) {
        const json::Value& v = entries->array()[i];
        EntryReader r(v, i);
        if (!v.is_object()) {
            r.fail("entries", std::string("entry must be an object, got ") + json::Value::type_name(v.type()), v);
            return R::fail(r.error());
        }
        AnimEntry e;
        if (const json::Value* index = r.get("index", false)) {
            if (!index->is_integer() || index->integer() != static_cast<std::int64_t>(i)) {
                r.fail("index", "does not match the position " + std::to_string(i) +
                                    " in the array; entries are in status order and cannot be moved",
                       *index);
            }
        }
        const std::string kind = r.string("kind", true);
        if (r.ok()) {
            bool known = false;
            for (std::uint8_t k = 0; k < 4; ++k) {
                if (kind == anim_kind_name(static_cast<AnimKind>(k))) {
                    e.kind = static_cast<AnimKind>(k);
                    known = true;
                }
            }
            if (!known) {
                r.fail("kind", "\"" + kind + "\" is not one of blend_linear, playback_once, playback_loop, none",
                       *v.find("kind"));
            }
        }
        e.status = static_cast<std::uint8_t>(r.integer("status", 0, 255));
        e.variable_duration = r.boolean("variable_duration");
        const std::int64_t link = r.integer("link", 0, 255);
        if (r.ok() && link != kStatusNone && link > last_status) {
            r.fail("link", std::to_string(link) + " is past the last entry (" + std::to_string(last_status) +
                               "); use 255 for no link",
                   *v.find("link"));
        }
        e.link = static_cast<std::uint8_t>(link);
        if (r.ok() && e.variable_duration) {
            e.duration_func = r.string("duration_func", true);
            if (r.ok() && !find_func(funcs, e.duration_func)) {
                std::string known;
                for (const ShAnimFunc& f : funcs) {
                    known += (known.empty() ? "" : ", ") + std::string(f.name);
                }
                r.fail("duration_func", "\"" + e.duration_func + "\" is not a function the game knows (" +
                                            (known.empty() ? std::string("none") : known) + ")",
                       *v.find("duration_func"));
            }
            for (const char* field : {"speed", "speed_q12"}) {
                if (const json::Value* s = v.find(field); s && r.ok()) {
                    r.fail(field, "a variable-duration entry takes its speed from duration_func; remove this field", *s);
                }
            }
        } else if (r.ok()) {
            const json::Value* speed = v.find("speed");
            const json::Value* q12 = v.find("speed_q12");
            if (!speed && !q12) {
                r.fail("speed", "missing (give speed or speed_q12)", v);
            }
            std::int64_t from_speed = 0;
            if (speed && r.ok()) {
                const double s = speed->number();
                if (!speed->is_number() || !std::isfinite(s)) {
                    r.fail("speed", std::string("expected a number, got ") + json::Value::type_name(speed->type()), *speed);
                } else {
                    // Truncated toward zero, as the game's Q12() macro does.
                    const double scaled = std::trunc(s * 4096.0);
                    if (scaled < std::numeric_limits<std::int32_t>::min() || scaled > std::numeric_limits<std::int32_t>::max()) {
                        r.fail("speed", "is too large", *speed);
                    } else {
                        from_speed = static_cast<std::int64_t>(scaled);
                    }
                }
            }
            if (q12) {
                e.speed_q12 = static_cast<std::int32_t>(r.integer("speed_q12", std::numeric_limits<std::int32_t>::min(),
                                                                  std::numeric_limits<std::int32_t>::max()));
                if (speed && r.ok() && from_speed != e.speed_q12) {
                    r.fail("speed", "is " + json::format_number(speed->number()) + " (speed_q12 " +
                                        std::to_string(from_speed) + ") but \"speed_q12\" is " +
                                        std::to_string(e.speed_q12) + "; edit one of them and delete the other",
                           *speed);
                }
            } else {
                e.speed_q12 = static_cast<std::int32_t>(from_speed);
            }
        }
        e.start_kf = static_cast<std::int16_t>(r.integer("start_kf", -32768, 32767));
        e.end_kf = static_cast<std::int16_t>(r.integer("end_kf", -32768, 32767));
        if (!r.ok()) {
            return R::fail(r.error());
        }
        table.entries.push_back(std::move(e));
    }
    return R::ok(std::move(table));
}

std::vector<ShAnimEntry> to_game_entries(const AnimTable& table, const ShAnimTableInfo& info,
                                         std::span<const ShAnimFunc> funcs) {
    std::vector<ShAnimEntry> out(info.count, ShAnimEntry{});
    for (std::size_t i = 0; i < table.entries.size() && i < out.size(); ++i) {
        const AnimEntry& e = table.entries[i];
        ShAnimEntry& o = out[i];
        o.kind = static_cast<std::uint8_t>(e.kind);
        o.status = e.status;
        o.hasVariableDuration = e.variable_duration ? 1 : 0;
        o.linkStatus = e.link;
        o.durationQ12 = e.speed_q12;
        if (e.variable_duration) {
            const ShAnimFunc* f = find_func(funcs, e.duration_func);
            o.durationQ12 = f ? f->value : 0;
        }
        o.startKeyframeIdx = e.start_kf;
        o.endKeyframeIdx = e.end_kf;
    }
    return out;
}

std::vector<std::size_t> diff_anim_tables(const AnimTable& a, const AnimTable& b) {
    std::vector<std::size_t> out;
    const std::size_t n = std::max(a.entries.size(), b.entries.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (i >= a.entries.size() || i >= b.entries.size() || !(a.entries[i] == b.entries[i])) {
            out.push_back(i);
        }
    }
    return out;
}

std::vector<std::string> lint_anim_table(const AnimTable& table, const AnimTable& defaults) {
    std::vector<std::string> notes;
    for (const std::size_t i : diff_anim_tables(table, defaults)) {
        if (i >= table.entries.size()) {
            continue;
        }
        const AnimEntry& e = table.entries[i];
        const std::string where = "entry " + std::to_string(i) + ": ";
        if (i >= 2 && e.status != i) {
            notes.push_back(where + "status " + std::to_string(e.status) + " is not the entry index");
        }
        if (e.kind == AnimKind::BlendLinear && e.link != kStatusNone && e.link != e.status + 1) {
            notes.push_back(where + "a blend entry usually links to status + 1, this one links to " + std::to_string(e.link));
        }
        if (e.kind == AnimKind::None) {
            notes.push_back(where + "kind none has no playback function; the game must never play this status");
        }
    }
    return notes;
}

Result<std::vector<std::string>> compare_with_overlay(std::span<const ShAnimEntry> entries,
                                                      std::span<const std::uint8_t> overlay, std::uint32_t va) {
    using R = Result<std::vector<std::string>>;
    if (va < kOverlayBase) {
        return R::fail("address " + hex(va) + " is below the overlay base " + hex(kOverlayBase));
    }
    const std::size_t offset = va - kOverlayBase;
    if (offset + entries.size() * kRecordSize > overlay.size()) {
        return R::fail("the overlay is " + std::to_string(overlay.size()) + " bytes, the table ends at " +
                       std::to_string(offset + entries.size() * kRecordSize));
    }
    auto u16 = [&](std::size_t at) {
        return static_cast<std::uint16_t>(overlay[at] | (overlay[at + 1] << 8));
    };
    auto u32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(overlay[at]) | (static_cast<std::uint32_t>(overlay[at + 1]) << 8) |
               (static_cast<std::uint32_t>(overlay[at + 2]) << 16) | (static_cast<std::uint32_t>(overlay[at + 3]) << 24);
    };
    std::vector<std::string> diffs;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const std::size_t rec = offset + i * kRecordSize;
        const ShAnimEntry& e = entries[i];
        const std::uint32_t func = u32(rec);
        const std::uint32_t want_func = e.kind < 3 ? kDiscPlaybackFuncs[e.kind] : 0;
        std::string what;
        auto field = [&](const char* name, std::int64_t game, std::int64_t raw) {
            if (game != raw) {
                what += std::string(what.empty() ? "" : ", ") + name + " " + std::to_string(game) + " (disc " +
                        std::to_string(raw) + ")";
            }
        };
        if (func != want_func) {
            what += "kind " + std::string(anim_kind_name(static_cast<AnimKind>(e.kind < 4 ? e.kind : 3))) + " (disc " +
                    hex(func) + ")";
        }
        field("status", e.status, overlay[rec + 4]);
        field("variable_duration", e.hasVariableDuration, static_cast<std::int8_t>(overlay[rec + 5]));
        field("link", e.linkStatus, overlay[rec + 6]);
        field("speed_q12", e.durationQ12, static_cast<std::int32_t>(u32(rec + 8)));
        field("start_kf", e.startKeyframeIdx, static_cast<std::int16_t>(u16(rec + 12)));
        field("end_kf", e.endKeyframeIdx, static_cast<std::int16_t>(u16(rec + 14)));
        if (!what.empty()) {
            diffs.push_back("entry " + std::to_string(i) + ": " + what);
        }
    }
    return R::ok(std::move(diffs));
}

}  // namespace sh::tables
