// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands over the save's event flags: flags, setflag, getflags, clearflags, setending. Which flags
// decide the ending is data: gamedata/config/console_endings.json.

#include "engine/debug/command_services.h"
#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/debug/commands/remembered_flags.h"
#include "engine/debug/dbg_trace.h"
#include "engine/game_state/game_state.h"
#include "engine/ids/game_numbers.h"
#include "engine/log/sh_log.h"
#include "engine/mods/data_reload.h"
#include "engine/vfs/vfs_data_json.h"
#include "formats/common/json.h"

#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sh::debug {

namespace {

using namespace sh::debug::commands;
namespace gs = sh::game_state;

int flag_count() { return sh::ids::flag_bit_end(); }

RememberedFlags& remembered() {
    static RememberedFlags flags;
    return flags;
}

bool flag_is_set(int flag) { return gs::event_flag(flag); }

// Named event flags by lower-case name, for typing and completion; read again when the data is reloaded.
const std::map<std::string, int>& flags_by_name() {
    static std::map<std::string, int> names;
    static std::uint32_t seen = sh::mods::kNeverRead;
    if (sh::mods::data_stale(seen)) names.clear();
    if (names.empty()) {
        for (int flag = 0; flag < flag_count(); ++flag) {
            const char* name = DbgTrace_EventFlagName(flag);
            if (name != nullptr && name[0] != '\0') names.emplace(lower(name), flag);
        }
    }
    return names;
}

void complete_flags(std::string_view, std::vector<Candidate>& out) {
    static std::vector<Candidate> cache;
    if (cache.empty()) {
        for (const auto& [name, flag] : flags_by_name()) cache.push_back({name, "#" + std::to_string(flag)});
    }
    out.insert(out.end(), cache.begin(), cache.end());
}

// ---- the ending's flags (gamedata/config/console_endings.json) ----

struct EndingData {
    struct Ending {
        std::string name;
        bool cybil_saved = false;
        bool good_path = false;
    };
    struct Shown {
        std::string label;
        std::string flag;
    };
    std::string cybil_saved_flag;
    std::string good_path_flag;
    std::vector<Ending> endings;
    std::vector<Shown> shown;
};

std::string text_of(const sh::json::Value& object, std::string_view key) {
    const sh::json::Value* value = object.find(key);
    return value != nullptr && value->is_string() ? value->string() : std::string();
}

bool truth_of(const sh::json::Value& object, std::string_view key) {
    const sh::json::Value* value = object.find(key);
    return value != nullptr && value->is_bool() && value->boolean();
}

EndingData read_ending_data() {
    EndingData data;
    const std::optional<std::string> text = ShVfs_ReadDataJsonText("config/console_endings.json");
    const Result<sh::json::Value> document = text ? sh::json::parse(*text) : Result<sh::json::Value>::fail("missing");
    if (!document || !document.value().is_object()) {
        SH_ERR("[CONSOLE] gamedata/config/console_endings.json: %s; the ending commands know no flags",
               document ? "not an object" : document.error().c_str());
        return data;
    }
    const sh::json::Value& root = document.value();
    data.cybil_saved_flag = text_of(root, "cybil_saved_flag");
    data.good_path_flag = text_of(root, "good_path_flag");
    if (const sh::json::Value* endings = root.find("endings"); endings != nullptr && endings->is_array()) {
        for (const sh::json::Value& entry : endings->array()) {
            data.endings.push_back({text_of(entry, "name"), truth_of(entry, "cybil_saved"), truth_of(entry, "good_path")});
        }
    }
    if (const sh::json::Value* shown = root.find("shown"); shown != nullptr && shown->is_array()) {
        for (const sh::json::Value& entry : shown->array()) {
            data.shown.push_back({text_of(entry, "label"), text_of(entry, "flag")});
        }
    }
    return data;
}

const EndingData& ending_data() {
    static EndingData data;
    static std::uint32_t seen = sh::mods::kNeverRead;
    if (sh::mods::data_stale(seen)) data = read_ending_data();
    return data;
}

// ---- commands ----

Result<void> run_flags(Context& ctx, const Args& args) {
    const std::string filter = upper(args.joined(0));
    if (!filter.empty() && std::isdigit(static_cast<unsigned char>(filter[0])) != 0) {
        const int flag = whole(filter);
        if (flag >= flag_count()) {
            say(ctx, "flags: %d is past the last flag (%d)", flag, flag_count() - 1);
            return done();
        }
        const char* name = DbgTrace_EventFlagName(flag);
        say(ctx, "flag %d %s%s%s", flag, flag_is_set(flag) ? "SET" : "clear", name != nullptr ? " " : "",
            name != nullptr ? name : "");
        return done();
    }

    int set = 0, shown = 0;
    LineWrapper list(ctx, 120, 2, "  ");
    for (int flag = 0; flag < flag_count(); ++flag) {
        if (!flag_is_set(flag)) continue;
        ++set;
        const char* name = DbgTrace_EventFlagName(flag);
        if (!filter.empty() && (name == nullptr || !contains_word(name, filter))) continue;
        const std::string item = std::to_string(flag) + (name != nullptr ? std::string(":") + name : std::string());
        list.add(item.substr(0, 63), item.size());
        ++shown;
    }
    list.flush();
    say(ctx, "%d event flag(s) set of %d%s%s, %d shown ('flags <n>' for one flag)", set, flag_count(),
        filter.empty() ? "" : ", filter ", filter.c_str(), shown);
    return done();
}

Result<void> run_setflag(Context& ctx, const Args& args) {
    // A name of gamedata/flags (with or without the base prefix) or a save bit.
    std::string typed = lower(args.str(0));
    if (typed.rfind("silenthill:", 0) == 0) typed.erase(0, 11);
    int flag = -1;
    if (!typed.empty() && std::isdigit(static_cast<unsigned char>(typed[0])) != 0) {
        flag = whole(typed);
    } else if (const auto known = flags_by_name().find(typed); known != flags_by_name().end()) {
        flag = known->second;
    }
    if (flag < 0) {
        ctx.out.error("unknown flag '" + args.str(0) + "'");
        return Result<void>::fail("unknown flag");
    }
    if (flag == 0 || flag >= flag_count()) {
        say(ctx, "usage: setflag <name> 0|1  (a flag of gamedata/flags; a save bit works too)");
        return done();
    }
    const bool value = args.boolean(1, true);
    gs::set_event_flag(flag, value);
    remembered().remember(flag, value);
    const char* name = sh::ids::flag_name(flag);
    say(ctx, "flag %s = %d", name != nullptr ? name : std::to_string(flag).c_str(), value ? 1 : 0);
    return done();
}

Result<void> run_getflags(Context& ctx, const Args&) {
    const EndingData& data = ending_data();
    say(ctx, "Ending = %s + %s. set BEFORE ending:", data.cybil_saved_flag.c_str(), data.good_path_flag.c_str());
    for (const EndingData::Shown& row : data.shown) {
        const int flag = sh::ids::flag_number(row.flag.c_str());
        say(ctx, " %-14s %-38s = %d", row.label.c_str(), row.flag.c_str(), flag > 0 && gs::event_flag(flag) ? 1 : 0);
    }
    say(ctx, "setending bad|bad+|good|good+  | setflag <name> 0|1");
    return done();
}

// The flags must be set before the final boss falls (the ending reads them there); story progress is not
// touched.
Result<void> run_setending(Context& ctx, const Args& args) {
    const EndingData& data = ending_data();
    const EndingData::Ending* ending = nullptr;
    for (const EndingData::Ending& known : data.endings) {
        if (same_word(known.name, args.str(0))) ending = &known;
    }
    const int cybil_flag = sh::ids::flag_number(data.cybil_saved_flag.c_str());
    const int good_flag = sh::ids::flag_number(data.good_path_flag.c_str());
    if (ending == nullptr || cybil_flag <= 0 || good_flag <= 0) {
        say(ctx, "setending: the ending flags are not in this gamedata");
        return done();
    }
    gs::set_event_flag(cybil_flag, ending->cybil_saved);
    gs::set_event_flag(good_flag, ending->good_path);
    remembered().remember(cybil_flag, ending->cybil_saved);
    remembered().remember(good_flag, ending->good_path);
    say(ctx, "ending '%s': %s=%d %s=%d", upper(args.str(0)).c_str(), data.cybil_saved_flag.c_str(),
        ending->cybil_saved ? 1 : 0, data.good_path_flag.c_str(), ending->good_path ? 1 : 0);
    say(ctx, "persists across 'map' warp; set before the ending");
    return done();
}

}  // namespace

void apply_remembered_flags() {
    for (const RememberedFlags::Entry& entry : remembered().entries()) gs::set_event_flag(entry.flag, entry.value);
}

namespace commands {

void register_flag_commands(Registry& registry) {
    add(registry, "flags", "debug", "event flags that are set",
        {arg("filter", ArgType::String, true, "part of a flag name, or a flag number", {}, complete_flags)}, run_flags);

    add(registry, "setflag", "game", "set an event flag",
        {arg("flag", ArgType::String, false, "flag number or name", {}, complete_flags),
         arg("value", ArgType::Bool, true, "default on")},
        run_setflag, {}, "The flag is remembered and re-applied after a 'map' warp. 'clearflags' forgets those.");

    add(registry, "getflags", "game", "show the ending flags", {}, run_getflags);

    // Only the remembered list is dropped; flags already written to the save stay (load a save for that).
    add(registry, "clearflags", "game", "forget the flags remembered by setflag and setending", {},
        [](Context& ctx, const Args&) {
            remembered().forget_all();
            say(ctx, "cleared pending flags (live flags unchanged; load a save to reset)");
            return done();
        });

    add(registry, "setending", "game", "set the flags of an ending (before the final boss)",
        {arg("ending", ArgType::Enum, false, "ending", {"bad", "bad+", "good", "good+"})}, run_setending);
}

}  // namespace commands
}  // namespace sh::debug
