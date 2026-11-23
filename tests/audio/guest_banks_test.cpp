// Sound banks of a character spawned off its maps (engine/audio/guest_banks.h).

#include "engine/audio/guest_banks.h"

#include "framework/sh_test.h"

#include <set>
#include <string>
#include <tuple>

namespace {

namespace au = sh::audio;

au::GuestSound sound(std::uint16_t id, int program, int note, std::vector<std::string> banks) {
    au::GuestSound s;
    s.id = id;
    s.program = program;
    s.note = note;
    s.name = "s" + std::to_string(id);
    s.banks = std::move(banks);
    return s;
}

}  // namespace

SH_TEST(guest_banks_nothing_when_the_map_owns_the_sounds) {
    const std::vector<au::GuestSound> sounds = {sound(1, 3, 49, {"MEP000", "MEP101"}), sound(2, 3, 51, {})};
    auto any = [](const std::string&, int, int) { return true; };
    const au::GuestPlan p = au::plan_guest_banks("MEP000", sounds, {"MEP000", "MAP702"}, any);
    SH_CHECK(p.loads.empty());
    SH_CHECK(p.unplayable.empty());
}

SH_TEST(guest_banks_loads_the_bank_that_serves_most) {
    // Registry banks first, the character's own banks for sounds the registry has none for.
    const std::vector<au::GuestSound> sounds = {sound(1, 3, 48, {"MAP200"}), sound(2, 3, 49, {"MAP200", "MEP100"}),
                                                sound(3, 2, 57, {})};
    std::set<std::tuple<std::string, int, int>> tones = {
        {"MAP200", 3, 48}, {"MAP200", 3, 49}, {"MEP100", 3, 49}, {"MAP202", 2, 57}};
    auto has = [&](const std::string& b, int p, int n) { return tones.count({b, p, n}) != 0; };
    const au::GuestPlan p = au::plan_guest_banks("MEP000", sounds, {"MAP202", "MAP200"}, has);
    SH_CHECK(p.loads.size() == std::size_t{2});
    SH_CHECK(p.loads[0].bank == std::string("MAP200"));
    SH_CHECK(p.loads[0].ids.size() == std::size_t{2});
    SH_CHECK(p.loads[1].bank == std::string("MAP202"));
    SH_CHECK(p.unplayable.empty());
}

SH_TEST(guest_banks_only_missing_sounds_and_unplayable) {
    // Sound 1 is the map's (its bank is in the slot); sound 2 no bank has a tone for.
    const std::vector<au::GuestSound> sounds = {sound(1, 3, 48, {"MEP100"}), sound(2, 3, 60, {"MAP200"})};
    auto none = [](const std::string&, int, int) { return false; };
    const au::GuestPlan p = au::plan_guest_banks("MEP100", sounds, {"MAP200"}, none);
    SH_CHECK(p.loads.empty());
    SH_CHECK(p.unplayable.size() == std::size_t{1});
    SH_CHECK(p.unplayable[0] == std::string("s2"));
}

SH_TEST(guest_banks_slots_and_redirects) {
    au::GuestBanks g;
    bool fresh = false;
    SH_CHECK(g.slot_of("MAP200", &fresh) == au::GuestBanks::kFirstSlot);
    SH_CHECK(fresh);
    SH_CHECK(g.slot_of("MAP200", &fresh) == au::GuestBanks::kFirstSlot);
    SH_CHECK(!fresh);
    SH_CHECK(g.slot_of("MEP100", &fresh) == au::GuestBanks::kFirstSlot + 1);
    g.redirect(1300, 4);
    SH_CHECK(g.slot_for(1300, 2) == 4);
    SH_CHECK(g.slot_for(1301, 2) == 2);
    SH_CHECK(g.reset().size() == std::size_t{2});
    SH_CHECK(g.slot_for(1300, 2) == 2);
    SH_CHECK(g.empty());
}
