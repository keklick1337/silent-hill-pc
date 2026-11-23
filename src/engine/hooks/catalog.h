// The events the engine and the game emit on the hook bus: names, whether a
// handler may cancel them, and their fields. The single list the console trace, the docs and the tests read.
//
// Units are the script API's own: positions Q12 world units (4096 = 1 m, Y grows downwards), angles Q12
// (4096 = full turn), health and damage Q12 (409600 = Harry's 100). Things are named, never numbered:
// items, characters, flags and maps by their readable ids.
#pragma once

#include <span>
#include <string_view>

namespace sh::hooks {

struct FieldInfo {
    std::string_view key;
    std::string_view type;  // "string", "int", "bool"
    bool modifiable = false;
    std::string_view about;
};

struct EventInfo {
    std::string_view name;
    bool cancellable = false;
    std::span<const FieldInfo> fields;
    std::string_view about;
};

std::span<const EventInfo> catalog();
const EventInfo* find_event(std::string_view name);

}  // namespace sh::hooks
