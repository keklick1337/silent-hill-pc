// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The behaviour variables of a character type (`vars`): the
// named fields a gamedata/chara/<Name>/behavior.lua reads and writes, as engine data. The layout comes from
// gamedata/chara/<Name>/state.json — readable names with a C kind and, optionally, the byte offset in the
// type's variable block ("at"); a mod patches the file like any JSON data of a character.
//
// Storage semantics are those of a C assignment: a set stores the low bits of the value
// in the field's kind, a get reads them back sign- or zero-extended.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The checked kind of `Record::member` (sized_field_kind).
#define SH_FIELD_KIND(Record, member, kind) \
    ::sh::chara::sized_field_kind<sizeof(std::declval<Record&>().member)>(::sh::chara::FieldKind::kind)

namespace sh::chara {

class CharaLibrary;

enum class FieldKind : std::uint8_t { S8, U8, S16, U16, S32, U32 };

// The byte size of a kind.
std::size_t field_kind_size(FieldKind kind);

// The kind of a record field, checked against the field's size when a field table is compiled: a field whose type
// changes in its record without its table entry following fails to build instead of reading or writing part of it.
template <std::size_t Size>
consteval FieldKind sized_field_kind(FieldKind kind) {
    const std::size_t size = (kind == FieldKind::S8 || kind == FieldKind::U8)    ? 1
                             : (kind == FieldKind::S16 || kind == FieldKind::U16) ? 2
                                                                                  : 4;
    return size == Size ? kind : throw "the field kind does not match the size of the field";
}

// Parses "s8".."u32"; returns false for anything else.
bool field_kind_parse(std::string_view name, FieldKind* out);

// Reads / writes one value of `kind` at `p` (a C assignment: the low bits; reads sign- or zero-extend).
long long field_read(FieldKind kind, const std::uint8_t* p);
void field_write(FieldKind kind, std::uint8_t* p, long long value);

// A named field of a variable block: `offset` is the byte offset in the block.
struct BehaviorField {
    std::string name;
    FieldKind kind = FieldKind::S32;
    std::uint16_t offset = 0;
};

// The layout of one character type's variable block, from state.json:
// { "fields": [ { "name": "romper_flags", "kind": "s32", "at": 0 }, ... ] }.
// "at" places the field (two names may share bytes: a flag word and its low half); a field without it follows
// the previous one at natural alignment. Every field needs a unique name and a known kind; duplicates, unknown
// kinds and bad offsets are reported through `warnings` and dropped.
class BehaviorLayout {
public:
    BehaviorLayout() = default;

    // The largest block a layout may describe.
    static constexpr std::size_t kMaxSize = 4096;

    // Parses the file text. Returns false when the text is not the expected shape; field-level problems are
    // warnings and skip the field.
    static bool parse(std::string_view json_text, std::string_view origin, std::vector<std::string>& warnings,
                      BehaviorLayout* out);

    const std::vector<BehaviorField>& fields() const { return fields_; }
    std::size_t size() const { return size_; }
    const BehaviorField* find(std::string_view name) const;

private:
    std::vector<BehaviorField> fields_;
    std::size_t size_ = 0;
};

// The variables of one character: a layout over a byte block. The block is the engine's own (own()) or storage
// another owner keeps (bind(): the game's character record holds the bytes). Reads of a name
// the layout lacks answer nothing; writes are refused.
class BehaviorVars {
public:
    BehaviorVars() = default;
    // A copy of an engine-owned block owns a copy of the bytes; a copy of a bound one views the same storage.
    BehaviorVars(const BehaviorVars& other) { *this = other; }
    BehaviorVars& operator=(const BehaviorVars& other);
    BehaviorVars(BehaviorVars&& other) noexcept { *this = std::move(other); }
    BehaviorVars& operator=(BehaviorVars&& other) noexcept;

    // Engine-owned block of the layout's size, zeroed (a fresh character).
    void own(const BehaviorLayout* layout);
    // A block kept elsewhere; fields past `storage` are not served.
    void bind(const BehaviorLayout* layout, std::span<std::uint8_t> storage);
    void clear();

    const BehaviorLayout* layout() const { return layout_; }
    bool has(std::string_view name) const;
    std::optional<long long> get(std::string_view name) const;
    bool set(std::string_view name, long long value);

    std::span<const std::uint8_t> bytes() const { return bytes_; }
    bool owned() const { return !own_.empty() && bytes_.data() == own_.data(); }

private:
    const BehaviorField* field(std::string_view name) const;

    const BehaviorLayout* layout_ = nullptr;
    std::span<std::uint8_t> bytes_;
    std::vector<std::uint8_t> own_;
};

// The layouts of every character type, read once from the library (state.json of the type's folder, base and
// mods merged). A type without its own file uses the file of its character.json "state_layout" folder, else of
// its "behavior" folder (the behaviour whose Lua it runs, as Grey Child runs the Stalker's). All layouts describe
// the same variable block of a character, so a name is also looked up across types (`naming`): a map event may set
// a boss's variables before the slot becomes that boss.
class BehaviorLayouts {
public:
    // Every layout of `library`, parse problems into `warnings`.
    void load(const CharaLibrary& library, std::vector<std::string>& warnings);
    void clear();
    bool loaded() const { return loaded_; }

    // The layout of type `id`, nullptr when it has none.
    const BehaviorLayout* of(int id) const;
    // The first layout (by type number) that names `name`, nullptr when none does.
    const BehaviorLayout* naming(std::string_view name) const;

private:
    std::map<int, std::shared_ptr<const BehaviorLayout>> by_type_;
    std::map<std::string, const BehaviorLayout*, std::less<>> by_name_;
    bool loaded_ = false;
};

}  // namespace sh::chara
