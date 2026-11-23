// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/chara/behavior_state.h"

#include "engine/chara/chara_library.h"
#include "formats/common/json.h"

#include <algorithm>
#include <cstring>

namespace sh::chara {

std::size_t field_kind_size(FieldKind kind) {
    switch (kind) {
        case FieldKind::S8:
        case FieldKind::U8: return 1;
        case FieldKind::S16:
        case FieldKind::U16: return 2;
        case FieldKind::S32:
        case FieldKind::U32: return 4;
    }
    return 0;
}

bool field_kind_parse(std::string_view name, FieldKind* out) {
    if (name == "s8") *out = FieldKind::S8;
    else if (name == "u8") *out = FieldKind::U8;
    else if (name == "s16") *out = FieldKind::S16;
    else if (name == "u16") *out = FieldKind::U16;
    else if (name == "s32") *out = FieldKind::S32;
    else if (name == "u32") *out = FieldKind::U32;
    else return false;
    return true;
}

long long field_read(FieldKind kind, const std::uint8_t* p) {
    switch (kind) {
        case FieldKind::S8: { std::int8_t v;  std::memcpy(&v, p, 1); return v; }
        case FieldKind::U8: { std::uint8_t v; std::memcpy(&v, p, 1); return v; }
        case FieldKind::S16: { std::int16_t v;  std::memcpy(&v, p, 2); return v; }
        case FieldKind::U16: { std::uint16_t v; std::memcpy(&v, p, 2); return v; }
        case FieldKind::S32: { std::int32_t v;  std::memcpy(&v, p, 4); return v; }
        case FieldKind::U32: { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
    }
    return 0;
}

void field_write(FieldKind kind, std::uint8_t* p, long long value) {
    switch (kind) {
        case FieldKind::S8:
        case FieldKind::U8: { const auto x = static_cast<std::uint8_t>(value);  std::memcpy(p, &x, 1); break; }
        case FieldKind::S16:
        case FieldKind::U16: { const auto x = static_cast<std::uint16_t>(value); std::memcpy(p, &x, 2); break; }
        case FieldKind::S32:
        case FieldKind::U32: { const auto x = static_cast<std::uint32_t>(value); std::memcpy(p, &x, 4); break; }
    }
}

const BehaviorField* BehaviorLayout::find(std::string_view name) const {
    for (const BehaviorField& f : fields_) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}

bool BehaviorLayout::parse(std::string_view json_text, std::string_view origin,
                           std::vector<std::string>& warnings, BehaviorLayout* out) {
    const auto parsed = json::parse(json_text);
    if (!parsed) {
        warnings.push_back(std::string(origin) + ": " + parsed.error());
        return false;
    }
    const json::Value& doc = parsed.value();
    const json::Value* fields = doc.find("fields");
    if (fields == nullptr || !fields->is_array()) {
        warnings.push_back(std::string(origin) + ": \"fields\" must be an array");
        return false;
    }

    BehaviorLayout layout;
    std::size_t next = 0;  // where a field without "at" goes (before alignment)
    std::size_t size = 0;
    for (const json::Value& entry : fields->array()) {
        const auto where = [&](const std::string& what) {
            return std::string(origin) + " field " + std::to_string(layout.fields_.size() + 1) + ": " + what;
        };
        if (!entry.is_object()) {
            warnings.push_back(where("must be an object"));
            continue;
        }
        const json::Value* name = entry.find("name");
        const json::Value* kind = entry.find("kind");
        if (name == nullptr || !name->is_string() || name->string().empty()) {
            warnings.push_back(where("\"name\" must be a non-empty string"));
            continue;
        }
        if (kind == nullptr || !kind->is_string()) {
            warnings.push_back(where("\"kind\" must be a string (s8, u8, s16, u16, s32, u32)"));
            continue;
        }
        BehaviorField field;
        field.name = name->string();
        if (!field_kind_parse(kind->string(), &field.kind)) {
            warnings.push_back(where("unknown kind \"" + kind->string() + "\""));
            continue;
        }
        if (layout.find(field.name) != nullptr) {
            warnings.push_back(where("duplicate field \"" + field.name + "\""));
            continue;
        }
        const std::size_t bytes = field_kind_size(field.kind);
        std::size_t offset = 0;
        if (const json::Value* at = entry.find("at"); at != nullptr) {
            if (!at->is_integer() || at->integer() < 0 ||
                static_cast<std::size_t>(at->integer()) + bytes > kMaxSize) {
                warnings.push_back(where("\"at\" must be an integer offset inside the block"));
                continue;
            }
            offset = static_cast<std::size_t>(at->integer());
        } else {
            // Natural alignment after the previous field.
            offset = (next + bytes - 1) / bytes * bytes;
            if (offset + bytes > kMaxSize) {
                warnings.push_back(where("the block is full"));
                continue;
            }
        }
        field.offset = static_cast<std::uint16_t>(offset);
        next = offset + bytes;
        size = std::max(size, next);
        layout.fields_.push_back(std::move(field));
    }
    if (layout.fields_.empty()) {
        warnings.push_back(std::string(origin) + ": no fields");
        return false;
    }
    layout.size_ = size;

    *out = std::move(layout);
    return true;
}

BehaviorVars& BehaviorVars::operator=(const BehaviorVars& other) {
    if (this != &other) {
        layout_ = other.layout_;
        if (other.owned()) {
            own_ = other.own_;
            bytes_ = own_;
        } else {
            own_.clear();
            bytes_ = other.bytes_;
        }
    }
    return *this;
}

BehaviorVars& BehaviorVars::operator=(BehaviorVars&& other) noexcept {
    if (this != &other) {
        const bool owned = other.owned();
        layout_ = other.layout_;
        own_ = std::move(other.own_);
        bytes_ = owned ? std::span<std::uint8_t>(own_) : other.bytes_;
        other.clear();
    }
    return *this;
}

void BehaviorVars::own(const BehaviorLayout* layout) {
    layout_ = layout;
    own_.assign(layout != nullptr ? layout->size() : 0, 0);
    bytes_ = own_;
}

void BehaviorVars::bind(const BehaviorLayout* layout, std::span<std::uint8_t> storage) {
    layout_ = layout;
    own_.clear();
    bytes_ = storage;
}

void BehaviorVars::clear() {
    layout_ = nullptr;
    own_.clear();
    bytes_ = {};
}

const BehaviorField* BehaviorVars::field(std::string_view name) const {
    const BehaviorField* f = layout_ != nullptr ? layout_->find(name) : nullptr;
    if (f == nullptr || f->offset + field_kind_size(f->kind) > bytes_.size()) {
        return nullptr;
    }
    return f;
}

bool BehaviorVars::has(std::string_view name) const {
    return field(name) != nullptr;
}

std::optional<long long> BehaviorVars::get(std::string_view name) const {
    const BehaviorField* f = field(name);
    if (f == nullptr) {
        return std::nullopt;
    }
    return field_read(f->kind, bytes_.data() + f->offset);
}

bool BehaviorVars::set(std::string_view name, long long value) {
    const BehaviorField* f = field(name);
    if (f == nullptr) {
        return false;
    }
    field_write(f->kind, bytes_.data() + f->offset, value);
    return true;
}

void BehaviorLayouts::clear() {
    by_type_.clear();
    by_name_.clear();
    loaded_ = false;
}

void BehaviorLayouts::load(const CharaLibrary& library, std::vector<std::string>& warnings) {
    clear();
    loaded_ = true;
    std::map<int, std::shared_ptr<const BehaviorLayout>> own;
    for (const CharacterDef& def : library.characters()) {
        std::string source;
        const Result<std::string> text = library.data_file(def.number, "state.json", &source, warnings);
        if (!text) {
            if (!text.error().starts_with("no file")) {
                warnings.push_back(text.error());
            }
            continue;
        }
        auto layout = std::make_shared<BehaviorLayout>();
        if (BehaviorLayout::parse(text.value(), source, warnings, layout.get())) {
            own[def.number] = std::move(layout);
        }
    }
    for (const CharacterDef& def : library.characters()) {
        auto it = own.find(def.number);
        if (it == own.end() && !def.state_layout_folder().empty()) {
            const int shared = library.find_id(def.state_layout_folder());
            if (shared >= 0) {
                it = own.find(shared);
            }
        }
        if (it != own.end()) {
            by_type_[def.number] = it->second;
        }
    }
    for (const auto& [id, layout] : own) {
        for (const BehaviorField& f : layout->fields()) {
            const auto [at, fresh] = by_name_.emplace(f.name, layout.get());
            if (!fresh) {
                const BehaviorField* first = at->second->find(f.name);
                if (first->offset != f.offset || first->kind != f.kind) {
                    warnings.push_back("state.json of type " + std::to_string(id) + ": \"" + f.name +
                                       "\" is another type's variable at a different place; the first one wins");
                }
            }
        }
    }
}

const BehaviorLayout* BehaviorLayouts::of(int id) const {
    const auto it = by_type_.find(id);
    return it != by_type_.end() ? it->second.get() : nullptr;
}

const BehaviorLayout* BehaviorLayouts::naming(std::string_view name) const {
    const auto it = by_name_.find(name);
    return it != by_name_.end() ? it->second : nullptr;
}

}  // namespace sh::chara
