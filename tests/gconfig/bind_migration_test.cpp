// SPDX-License-Identifier: GPL-3.0-or-later
// Binds whose built-in value changed (config_keys.h kBindMigrations): an old file holding the old value moves to
// the new one, a value the player chose stays.

#include "engine/config/config_keys.h"
#include "framework/sh_test.h"

#include <string_view>

namespace {

const sh::config::SchemeBind* scheme_bind(std::string_view key) {
    for (const sh::config::SchemeBind& b : sh::config::kSchemeBinds) {
        if (b.key == key) return &b;
    }
    return nullptr;
}

}  // namespace

SH_TEST(bind_migration_quick_turn_becomes_both_shoulders) {
    const sh::config::SchemeBind* turn = scheme_bind("pad_quick_turn");
    SH_CHECK(turn != nullptr);
    SH_CHECK(turn->classic == "leftshoulder+rightshoulder");
    SH_CHECK(turn->altcam == "leftshoulder");
    // The side steps it is made of are the classic scheme's L1 and R1.
    SH_CHECK(scheme_bind("pad_l1")->classic == "leftshoulder" && scheme_bind("pad_r1")->classic == "rightshoulder");

    bool found = false;
    for (const sh::config::BindMigration& m : sh::config::kBindMigrations) {
        // A migration ends on the present built-in value, under a version the game stamps.
        const sh::config::SchemeBind* b = scheme_bind(m.key);
        SH_CHECK(b != nullptr && b->slot == m.slot);
        SH_CHECK((m.altcam ? b->altcam : b->classic) == m.now);
        SH_CHECK(m.version <= PC_CONFIG_VERSION);
        SH_CHECK(m.now.size() < sizeof(sh::config::BindSlot));
        if (m.key != "pad_quick_turn") continue;
        found = true;
        SH_CHECK(!m.altcam && m.version == 12);
        SH_CHECK(sh::config::bind_migrates(m, 11, "leftshoulder"));
        SH_CHECK(sh::config::bind_migrates(m, 0, "leftshoulder"));    // a file with no version
        SH_CHECK(!sh::config::bind_migrates(m, 12, "leftshoulder"));  // chosen after the change
        SH_CHECK(!sh::config::bind_migrates(m, 11, "y"));             // the player's own
        SH_CHECK(!sh::config::bind_migrates(m, 11, "NONE"));
    }
    SH_CHECK(found);
}
