// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "engine/save/save_store.h"

#include "formats/common/json.h"

#include <algorithm>
#include <optional>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <system_error>

namespace sh::save {

namespace fs = std::filesystem;

namespace {

std::string two_digits(int n) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d", n);
    return buf;
}

std::string utc_now() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// "save_0012.sav" -> 12; -1 for any other name (temporary files included).
int number_of_name(const std::string& name) {
    if (name.size() < 13 || !name.starts_with("save_") || !name.ends_with(".sav")) {
        return -1;
    }
    int n = 0;
    for (std::size_t i = 5; i + 4 < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9' || n > 99999999) {
            return -1;
        }
        n = n * 10 + (name[i] - '0');
    }
    return n > 0 ? n : -1;
}

bool is_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

struct Decoded {
    SlotState state = SlotState::Empty;
    SaveFile file;
    SlotSummary summary;
    std::string error;
};

Decoded decode_path(const fs::path& path, FileKind kind) {
    Decoded out;
    if (!is_file(path)) {
        return out;
    }
    out.state = SlotState::Damaged;
    auto bytes = read_whole_file(path);
    if (!bytes) {
        out.error = bytes.error();
        return out;
    }
    auto file = decode(bytes.value());
    if (!file) {
        out.error = file.error();
        return out;
    }
    if (file.value().kind != kind) {
        out.error = "wrong kind of save file";
        return out;
    }
    const Chunk* info = file.value().find(kTagInfo);
    if (info == nullptr || (file.value().find(kTagState) == nullptr && file.value().find(kTagGame) == nullptr)) {
        out.error = "INFO or STAT chunk missing";
        return out;
    }
    auto summary = decode_summary(info->data);
    if (!summary) {
        out.error = summary.error();
        return out;
    }
    out.state = SlotState::Ok;
    out.summary = summary.value();
    out.file = std::move(file.value());
    return out;
}

std::string text_of(const Chunk& chunk) { return std::string(chunk.data.begin(), chunk.data.end()); }

int int_member(const json::Value& doc, const char* key, int fallback) {
    const json::Value* v = doc.find(key);
    return v != nullptr && v->is_integer() ? static_cast<int>(v->integer()) : fallback;
}

std::string string_member(const json::Value& doc, const char* key) {
    const json::Value* v = doc.find(key);
    return v != nullptr && v->is_string() ? v->string() : std::string();
}

// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's days_from_civil).
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

}  // namespace

std::int64_t parse_utc(const std::string& text) {
    int y = 0;
    unsigned mo = 0, d = 0, h = 0, mi = 0, s = 0;
    char z = 0;
    if (std::sscanf(text.c_str(), "%4d-%2u-%2uT%2u:%2u:%2u%c", &y, &mo, &d, &h, &mi, &s, &z) != 7 || z != 'Z' ||
        mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) {
        return 0;
    }
    return days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

SaveStore::SaveStore(fs::path root) : root_(std::move(root)) {}

Result<std::string> SaveStore::state_of(const SlotRecord& record, Warnings& warnings) const {
    if (!record.state.empty()) {
        return Result<std::string>::ok(record.state);
    }
    if (record.legacy_game.empty()) {
        return Result<std::string>::fail("no game state");
    }
    auto game = from_legacy(record.legacy_game, record.legacy_names, record.legacy_options, names_, warnings);
    if (!game) {
        return Result<std::string>::fail(game.error());
    }
    return Result<std::string>::ok(to_json(game.value()));
}

SlotDescription SaveStore::describe_state(const SaveGame& game) const {
    SlotDescription d;
    if (game.map.named()) {
        d.map_name = game.map.name;
        d.map_index = names_.map_number ? names_.map_number(game.map.name.c_str()) : -1;
    } else {
        d.map_index = static_cast<int>(game.map.number);
        if (names_.map_name) {
            if (const char* name = names_.map_name(d.map_index)) {
                d.map_name = name;
            }
        }
    }
    d.location_id = game.location.named() ? location_number(game.location.name)
                                          : static_cast<int>(game.location.number);
    if (const char* name = location_display_name(d.location_id)) {
        d.location_name = name;
    }
    d.difficulty = game.difficulty;
    d.difficulty_name = game.difficulty < 0 ? "Easy" : game.difficulty == 0 ? "Normal" : "Hard";
    d.play_time_seconds = game.play_time / 4096.0;
    d.next_fear = game.next_fear;
    d.save_count = game.save_count;
    return d;
}

SlotRead SaveStore::read_file(const fs::path& path, FileKind kind) const {
    Decoded d = decode_path(path, kind);
    SlotRead out;
    out.state = d.state;
    out.error = d.error;
    if (d.state != SlotState::Ok) {
        return out;
    }
    out.record.summary = d.summary;
    if (const Chunk* remains = d.file.find(kTagRemains)) {
        out.record.remains = text_of(*remains);
    }
    if (const Chunk* meta = d.file.find(kTagMeta)) {
        out.record.mods = mods_of_meta(text_of(*meta));
        if (auto doc = json::parse(text_of(*meta)); doc && doc.value().is_object()) {
            const std::string at = string_member(doc.value(), "saved_at");
            if (parse_utc(at) > 0) out.record.saved_at = at;
        }
    }
    if (const Chunk* state = d.file.find(kTagState)) {
        out.record.state = text_of(*state);
        if (auto game = from_json(out.record.state); !game) {
            out.state = SlotState::Damaged;
            out.error = game.error();
        }
        return out;
    }
    // Format 1: the game's records, turned into a state here; the file itself stays as it is until it is saved over.
    out.record.legacy_game = d.file.find(kTagGame)->data;
    if (const Chunk* names = d.file.find(kTagNames)) {
        out.record.legacy_names = text_of(*names);
    }
    if (const Chunk* options = d.file.find(kTagOptions)) {
        out.record.legacy_options = options->data;
    }
    if (auto state = state_of(out.record, out.warnings)) {
        out.record.state = std::move(state.value());
    } else {
        out.state = SlotState::Damaged;
        out.error = state.error();
    }
    return out;
}

Result<void> SaveStore::write_file(const fs::path& path, FileKind kind, const SlotRecord& record, int number,
                                   int quick_index) const {
    Warnings warnings;
    auto state = state_of(record, warnings);
    if (!state) {
        return Result<void>::fail(state.error());
    }
    auto game = from_json(state.value());
    if (!game) {
        return Result<void>::fail(game.error());
    }
    SlotDescription description = describe_state(game.value());
    description.mods = record.mods;
    description.saved_at = record.saved_at;
    const std::string meta = describe(description, number, quick_index);
    // Always the current format (a state read from an older one is written anew).
    const std::string text = game.value().old_format ? to_json(game.value()) : state.value();
    SaveFile file;
    file.kind = kind;
    file.put(kTagInfo, encode_summary(record.summary));
    file.put(kTagState, std::vector<std::uint8_t>(text.begin(), text.end()));
    file.put(kTagMeta, std::vector<std::uint8_t>(meta.begin(), meta.end()));
    if (!record.remains.empty()) {
        file.put(kTagRemains, std::vector<std::uint8_t>(record.remains.begin(), record.remains.end()));
    }
    return write_file_atomic(path, encode(file));
}

fs::path SaveStore::save_path(int number) const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "save_%04d.sav", number);
    return root_ / buf;
}

fs::path SaveStore::quick_path(int index) const { return root_ / "quick" / ("quick_" + two_digits(index + 1) + ".sav"); }

std::vector<int> SaveStore::numbers() const {
    std::vector<int> out;
    std::error_code ec;
    fs::directory_iterator it(root_, ec);
    if (ec) {
        return out;
    }
    for (const fs::directory_entry& entry : it) {
        const int n = number_of_name(entry.path().filename().string());
        if (n > 0 && entry.is_regular_file(ec)) {
            out.push_back(n);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

int SaveStore::next_number() const {
    const std::vector<int> n = numbers();
    return n.empty() ? 1 : n.back() + 1;
}

std::vector<SaveEntry> SaveStore::list() const {
    std::vector<SaveEntry> out;
    for (int number : numbers()) {
        Decoded d = decode_path(save_path(number), FileKind::Slot);
        if (d.state == SlotState::Empty) {
            continue;
        }
        SaveEntry e;
        e.number = number;
        e.state = d.state;
        e.error = d.error;
        e.summary = d.summary;
        e.location_id = d.summary.location;
        e.play_time_seconds = d.summary.play_time / 4096.0;
        if (const Chunk* meta = d.file.find(kTagMeta)) {
            auto doc = json::parse(std::string_view(reinterpret_cast<const char*>(meta->data.data()), meta->data.size()));
            if (doc && doc.value().is_object()) {
                e.map_index = int_member(doc.value(), "map_index", -1);
                e.location_id = int_member(doc.value(), "location_id", e.location_id);
                if (const json::Value* v = doc.value().find("difficulty_id"); v != nullptr && v->is_integer()) {
                    e.difficulty = static_cast<int>(v->integer());
                    e.has_difficulty = true;
                }
                e.location_name = string_member(doc.value(), "location");
                e.difficulty_name = string_member(doc.value(), "difficulty");
                e.saved_at = parse_utc(string_member(doc.value(), "saved_at"));
                if (const json::Value* t = doc.value().find("play_time_seconds"); t != nullptr && t->is_number()) {
                    e.play_time_seconds = t->number();
                }
                e.mods = mods_of_meta(std::string_view(reinterpret_cast<const char*>(meta->data.data()),
                                                       meta->data.size()));
            }
        }
        out.push_back(std::move(e));
    }
    std::stable_sort(out.begin(), out.end(), [](const SaveEntry& a, const SaveEntry& b) {
        if (a.summary.sequence != b.summary.sequence) {
            return a.summary.sequence > b.summary.sequence;
        }
        return a.number > b.number;
    });
    return out;
}

std::int32_t SaveStore::newest_sequence() const {
    std::int32_t best = 0;
    for (const SaveEntry& e : list()) {
        if (e.state == SlotState::Ok) {
            best = std::max(best, e.summary.sequence);
        }
    }
    return best;
}

bool SaveStore::contains(const SlotRecord& record) const {
    Warnings warnings;
    const auto state = state_of(record, warnings);
    if (!state) {
        return false;
    }
    for (int number : numbers()) {
        const SlotRead r = read(number);
        if (r.state == SlotState::Ok && r.record.summary == record.summary && r.record.state == state.value()) {
            return true;
        }
    }
    return false;
}

SlotRead SaveStore::read(int number) const {
    if (number <= 0) {
        return SlotRead{};
    }
    return read_file(save_path(number), FileKind::Slot);
}

Result<int> SaveStore::write(int number, const SlotRecord& record) {
    if (number <= 0) {
        number = next_number();
    }
    if (auto r = write_file(save_path(number), FileKind::Slot, record, number, -1); !r) {
        return Result<int>::fail(r.error());
    }
    return Result<int>::ok(number);
}

Result<void> SaveStore::remove(int number) {
    const fs::path path = save_path(number);
    std::error_code ec;
    if (!fs::remove(path, ec) || ec) {
        return Result<void>::fail("cannot remove " + path.string() + (ec ? ": " + ec.message() : ": not there"));
    }
    return Result<void>::ok();
}

SlotRead SaveStore::read_quick(int index) const {
    if (index < 0 || index >= kQuickSlots) {
        return SlotRead{};
    }
    return read_file(quick_path(index), FileKind::Quick);
}

Result<void> SaveStore::write_quick(int index, const SlotRecord& record) {
    if (index < 0 || index >= kQuickSlots) {
        return Result<void>::fail("quick save index out of range");
    }
    return write_file(quick_path(index), FileKind::Quick, record, 0, index);
}

SlotRead SaveStore::read_at(const fs::path& path) const { return read_file(path, FileKind::Quick); }

Result<void> SaveStore::write_at(const fs::path& path, const SlotRecord& record) const {
    return write_file(path, FileKind::Quick, record, 0, -1);
}

fs::path SaveStore::legacy_card_dir(int card) const { return root_ / ("card" + std::to_string(card + 1)); }

fs::path SaveStore::legacy_group_path(int card, int file) const {
    return legacy_card_dir(card) / ("file_" + two_digits(file + 1) + ".sav");
}

fs::path SaveStore::legacy_slot_path(int card, int file, int slot) const {
    return legacy_card_dir(card) / ("slot_" + two_digits(file + 1) + "_" + two_digits(slot + 1) + ".sav");
}

SlotRead SaveStore::read_legacy_slot(int card, int file, int slot) const {
    SlotRead out = read_file(legacy_slot_path(card, file, slot), FileKind::Slot);
    if (out.state != SlotState::Ok || out.record.legacy_game.empty() || !out.record.legacy_options.empty()) {
        return out;
    }
    // The old layout kept the options in the group file: the state is converted again with them.
    if (auto bytes = read_whole_file(legacy_group_path(card, file))) {
        if (auto group = decode(bytes.value()); group && group.value().kind == FileKind::Group) {
            if (const Chunk* options = group.value().find(kTagOptions)) {
                out.record.legacy_options = options->data;
                out.record.state.clear();
                out.warnings.clear();
                if (auto state = state_of(out.record, out.warnings)) {
                    out.record.state = std::move(state.value());
                }
            }
        }
    }
    return out;
}

std::optional<std::vector<SavedMod>> mods_of_meta(std::string_view meta_json) {
    auto doc = json::parse(meta_json);
    if (!doc || !doc.value().is_object()) {
        return std::nullopt;
    }
    // "sh-save-meta/N": the list means something from format 3 on.
    const std::string format = string_member(doc.value(), "format");
    const std::size_t slash = format.rfind('/');
    if (!format.starts_with("sh-save-meta/") || slash == std::string::npos ||
        std::atoi(format.c_str() + slash + 1) < 3) {
        return std::nullopt;
    }
    const json::Value* list = doc.value().find("mods");
    if (list == nullptr || !list->is_array()) {
        return std::nullopt;
    }
    std::vector<SavedMod> out;
    for (const json::Value& m : list->array()) {
        if (!m.is_object()) {
            continue;
        }
        SavedMod mod;
        mod.id = string_member(m, "id");
        if (mod.id.empty()) {
            continue;
        }
        mod.name = string_member(m, "name");
        mod.version = string_member(m, "version");
        const json::Value* gameplay = m.find("gameplay");
        mod.gameplay = gameplay != nullptr && gameplay->is_bool() && gameplay->boolean();
        out.push_back(std::move(mod));
    }
    return out;
}

std::string describe(const SlotDescription& d, int number, int quick_index) {
    json::Writer w(true);
    w.begin_object();
    w.member("format", "sh-save-meta/3");
    if (quick_index >= 0) {
        w.member("quick", quick_index + 1);
    } else {
        w.member("save", number);
    }
    w.member("saved_at", d.saved_at.empty() ? utc_now() : d.saved_at);
    if (!d.map_name.empty()) {
        w.member("map", d.map_name);
    }
    if (d.map_index >= 0) {
        w.member("map_index", d.map_index);
    }
    if (d.location_id >= 0) {
        w.member("location_id", d.location_id);
    }
    if (!d.location_name.empty()) {
        w.member("location", d.location_name);
    }
    // -1 is a difficulty of the game (Easy): the name says whether there is one.
    if (!d.difficulty_name.empty()) {
        w.member("difficulty_id", d.difficulty);
        w.member("difficulty", d.difficulty_name);
    }
    w.key("play_time_seconds").value_shortest(d.play_time_seconds);
    w.member("save_count", d.save_count);
    w.member("next_fear", d.next_fear);
    if (d.mods) {
        w.key("mods").begin_array();
        for (const SavedMod& mod : *d.mods) {
            w.inline_next().begin_object();
            w.member("id", mod.id);
            w.member("name", mod.name);
            w.member("version", mod.version);
            w.member("gameplay", mod.gameplay);
            w.end_object();
        }
        w.end_array();
    }
    w.end_object();
    return w.take();
}

}  // namespace sh::save

namespace sh::save {

namespace {

// The running record a save gives, for comparing an old file with its conversion.
std::optional<game::SaveRecord> live_of(const SlotRead& read, const NameTable& names) {
    if (read.state != SlotState::Ok) return std::nullopt;
    auto game = from_json(read.record.state);
    if (!game) return std::nullopt;
    Warnings warnings;
    auto live = to_live_state(game.value(), names, warnings);
    if (!live) return std::nullopt;
    return std::move(live.value());
}

}  // namespace

int SaveStore::convert_old_saves(std::vector<std::string>& lines) {
    std::vector<std::pair<fs::path, int>> files;  // path, number (> 0) or -1 - quick index
    for (const int n : numbers()) files.emplace_back(save_path(n), n);
    for (int q = 0; q < kQuickSlots; ++q) {
        if (is_file(quick_path(q))) files.emplace_back(quick_path(q), -1 - q);
    }
    const fs::path backup_dir = root_ / "old_format_backup";
    int converted = 0;
    for (const auto& [path, id] : files) {
        auto bytes = read_whole_file(path);
        if (!bytes) continue;
        auto file = decode(bytes.value());
        if (!file) continue;  // a damaged file stays as it is (the list shows it)
        bool old = file.value().version < kFormatVersion;
        if (const Chunk* state = file.value().find(kTagState); state != nullptr && !old) {
            auto game = from_json(text_of(*state));
            old = game && game.value().old_format;
        }
        if (!old) continue;
        const SlotRead before = id > 0 ? read(id) : read_quick(-1 - id);
        if (before.state != SlotState::Ok) {
            lines.push_back(path.filename().string() + ": not converted (" + before.error + ")");
            continue;
        }
        std::error_code ec;
        fs::create_directories(backup_dir, ec);
        fs::path copy = backup_dir / path.filename();
        if (id < 0) copy = backup_dir / ("quick_" + path.filename().string());
        if (!is_file(copy)) {
            fs::copy_file(path, copy, ec);
            if (ec) {
                lines.push_back(path.filename().string() + ": not converted, no backup copy (" + ec.message() + ")");
                continue;
            }
        }
        bool written = false;
        if (id > 0) {
            written = static_cast<bool>(write(id, before.record));
        } else {
            written = static_cast<bool>(write_quick(-1 - id, before.record));
        }
        const SlotRead after = id > 0 ? read(id) : read_quick(-1 - id);
        const auto a = live_of(before, names_), b = live_of(after, names_);
        if (!written || !a || !b || !(*a == *b)) {
            write_file_atomic(path, bytes.value());
            lines.push_back(path.filename().string() + ": not converted, the new file did not give the same state; "
                            "the old one is kept");
            continue;
        }
        ++converted;
        lines.push_back(path.filename().string() + ": converted from save format " +
                        std::to_string(file.value().version) + " (deprecated, to be removed in a future version); "
                        "the old file is in " + copy.string());
    }
    return converted;
}

}  // namespace sh::save
