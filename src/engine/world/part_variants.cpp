// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/part_variants.h"

#include <string>

#include "formats/common/json.h"

namespace sh::world {

namespace {

using json::Value;

std::string where(const Value& v) {
    return "line " + std::to_string(v.line()) + ": ";
}

bool parts(const Value* v, std::vector<std::uint8_t>& out, std::string& error) {
    out.clear();
    if (v == nullptr) {
        return true;
    }
    if (!v->is_array()) {
        error = where(*v) + "a part list is an array of model indices";
        return false;
    }
    for (const Value& p : v->array()) {
        if (!p.is_integer() || p.integer() < 0 || p.integer() > 127) {
            error = where(p) + "a model index is an integer 0..127";
            return false;
        }
        out.push_back(static_cast<std::uint8_t>(p.integer()));
    }
    return true;
}

bool small(const Value& parent, const char* key, std::uint32_t lo, std::uint32_t hi, std::uint32_t& out,
           std::string& error) {
    const Value* v = parent.find(key);
    if (v == nullptr) {
        return true;
    }
    if (!v->is_integer() || v->integer() < lo || v->integer() > hi) {
        error = where(*v) + "\"" + key + "\" is an integer " + std::to_string(lo) + ".." + std::to_string(hi);
        return false;
    }
    out = static_cast<std::uint32_t>(v->integer());
    return true;
}

}  // namespace

Result<PartVariants> PartVariants::parse(std::string_view text) {
    Result<Value> doc = json::parse(text);
    if (!doc) {
        return Result<PartVariants>::fail(doc.error());
    }
    const Value& root = doc.value();
    const Value* fmt = root.find("format");
    if (!root.is_object() || fmt == nullptr || !fmt->is_string() || fmt->string() != "sh-part-variants") {
        return Result<PartVariants>::fail("not a sh-part-variants file");
    }
    const Value* slots = root.find("slots");
    if (slots == nullptr || !slots->is_array()) {
        return Result<PartVariants>::fail("\"slots\" is an array");
    }
    PartVariants pv;
    std::string error;
    for (const Value& s : slots->array()) {
        if (!s.is_object()) {
            return Result<PartVariants>::fail(where(s) + "a slot is an object");
        }
        Slot slot;
        if (!small(s, "shift", 0, 31, slot.shift, error) || !small(s, "bits", 1, 16, slot.bits, error) ||
            !parts(s.find("hide"), slot.hide, error)) {
            return Result<PartVariants>::fail(error);
        }
        if (const Value* vs = s.find("variants"); vs != nullptr) {
            if (!vs->is_object()) {
                return Result<PartVariants>::fail(where(*vs) + "\"variants\" maps a slot value to its parts");
            }
            for (const auto& [key, v] : vs->members()) {
                Variant var;
                unsigned long value = 0;
                bool digits = !key.empty() && key.size() <= 6;
                for (char c : key) {
                    digits = digits && c >= '0' && c <= '9';
                    value = value * 10 + static_cast<unsigned long>(c - '0');
                }
                if (!digits || value == 0 || value >= (1ul << slot.bits)) {
                    return Result<PartVariants>::fail(where(v) + "variant key \"" + key +
                                                      "\" is a slot value 1.." +
                                                      std::to_string((1ul << slot.bits) - 1));
                }
                if (!v.is_object()) {
                    return Result<PartVariants>::fail(where(v) + "a variant is an object");
                }
                var.value = static_cast<std::uint32_t>(value);
                if (!parts(v.find("hide"), var.hide, error) || !parts(v.find("show"), var.show, error)) {
                    return Result<PartVariants>::fail(error);
                }
                slot.variants.push_back(std::move(var));
            }
        }
        pv.slots.push_back(std::move(slot));
    }
    return Result<PartVariants>::ok(std::move(pv));
}

void PartVariants::toggles(std::uint32_t selector, std::vector<PartToggle>& out) const {
    for (const Slot& slot : slots) {
        const std::uint32_t value = (selector >> slot.shift) & ((1u << slot.bits) - 1u);
        if (value == 0) {
            continue;
        }
        for (std::uint8_t p : slot.hide) {
            out.push_back({p, false});
        }
        for (const Variant& v : slot.variants) {
            if (v.value != value) {
                continue;
            }
            for (std::uint8_t p : v.hide) {
                out.push_back({p, false});
            }
            for (std::uint8_t p : v.show) {
                out.push_back({p, true});
            }
            break;
        }
    }
}

}  // namespace sh::world
