#pragma once

// Data of the composite event helpers (event_composites.h): the common pickups
// (gamedata/config/pickups.json) and the paper map pages a page reveals (config/paper_map_reveals.json). The files
// hold names; the host turns them into the numbers its code takes.

#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::gameflow {

struct PickupEntry {
    std::string item;     // item name
    std::string message;  // name of the shared prompt message
    bool ammo = false;
};

class PickupTable {
public:
    static Result<PickupTable> parse(std::string_view json);
    const std::vector<PickupEntry>& entries() const { return entries_; }

private:
    std::vector<PickupEntry> entries_;
};

struct RevealEntry {
    std::string page;                  // paper map name
    std::vector<std::string> revealed;  // pages marked along with it
};

class PaperMapReveals {
public:
    static Result<PaperMapReveals> parse(std::string_view json);
    const std::vector<RevealEntry>& entries() const { return entries_; }

private:
    std::vector<RevealEntry> entries_;
};

}  // namespace sh::gameflow
