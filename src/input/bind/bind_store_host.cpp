// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game's bind table (bind_table.h): its values are config.cfg's bind fields and the input actions' slots, and
// a change is written to the file as its own line, so a hand edit made while the game runs is not overwritten.

#include "input/bind/bind_table.h"

#include "engine/config/pc_config.h"
#include "input/camera_style.h"
#include "input/pc_actions.h"

#include <cstdio>
#include <string>
#include <vector>

namespace sh::input {
namespace {

// Bind names live in fixed fields of the configuration.
constexpr std::size_t kFieldSize = PC_BIND_SLOT_LEN;

int scheme_index(BindScheme s) { return s == BindScheme::AltCamera ? 1 : 0; }

class ConfigBindStore final : public BindStore {
public:
    std::optional<std::string> value(std::string_view key, BindScheme scheme) const override {
        const char* field = PcConfig_BindField(std::string(key).c_str(), scheme_index(scheme), nullptr);
        return field != nullptr ? std::optional<std::string>(field) : std::nullopt;
    }

    std::string built_in(std::string_view key, BindScheme scheme) const override {
        const char* def = PcConfig_BindDefault(std::string(key).c_str(), scheme_index(scheme));
        return def != nullptr ? def : "";
    }

    bool per_scheme(std::string_view key) const override {
        int own_copy = 0;
        PcConfig_BindField(std::string(key).c_str(), 0, &own_copy);
        return own_copy != 0;
    }

    bool assign(std::string_view key, BindScheme scheme, std::string_view value) override {
        char* field = PcConfig_BindField(std::string(key).c_str(), scheme_index(scheme), nullptr);
        if (field == nullptr) {
            return false;
        }
        std::snprintf(field, kFieldSize, "%.*s", static_cast<int>(value.size()), value.data());
        return true;
    }

    std::optional<BindLine> action_line(std::string_view key, BindScheme scheme) const override {
        char name[64];
        char value[64];
        if (PcActions_ConfigLine(std::string(key).c_str(), scheme_index(scheme), name, sizeof(name), value,
                                 sizeof(value)) == 0) {
            return std::nullopt;
        }
        return BindLine{name, value};
    }

    bool save_action(std::string_view key, BindScheme scheme) override {
        return PcActions_Save(std::string(key).c_str(), scheme_index(scheme)) != 0;
    }

    void save_line(const BindLine& line) override { PcConfig_SaveKeyValue(line.key.c_str(), line.value.c_str()); }

    void save_lines(std::span<const BindLine> lines) override {
        std::vector<const char*> keys;
        std::vector<const char*> values;
        for (const BindLine& l : lines) {
            keys.push_back(l.key.c_str());
            values.push_back(l.value.c_str());
        }
        PcConfig_SaveKeyValues(keys.data(), values.data(), static_cast<int>(lines.size()));
    }

    void actions_assigned() override { PcActions_Project(); }

    void changed() override {
        ++g_PcBindsGen;
        camera_styles().reapply_binds();
    }
};

}  // namespace

BindTable& bind_table() {
    static ConfigBindStore store;
    static BindTable table(store);
    return table;
}

}  // namespace sh::input
