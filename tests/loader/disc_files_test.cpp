// The disc's files by name (engine/loader/queue/disc_files): lookups, sizes, the game's searches and
// the fan-disc remap.

#include <array>
#include <cstdint>
#include <vector>

#include "engine/loader/queue/disc_files.h"

#include "framework/sh_test.h"

namespace ld = sh::loader;
namespace ft = sh::convert::filetable;

namespace {

ft::Record rec(const char* path, std::uint32_t sector, std::uint32_t blocks) {
    return ft::record_from(path, sector, blocks).value();
}

ld::DiscFiles sample() {
    ld::DiscFiles t;
    t.assign({rec("1ST/FONT16.TIM", 100, 33), rec("CHARA/HERO.ILM", 200, 8), rec("CHARA/HERO.TIM", 210, 8),
              rec("TIM/MP_0TOWN.TIM", 300, 1), rec("XA/Z1_16180", 400, 16)});
    return t;
}

}  // namespace

SH_TEST(disc_files_find_by_path) {
    const ld::DiscFiles t = sample();
    SH_CHECK(t.count() == 5);
    SH_CHECK(t.find("CHARA/HERO.TIM") == 2);
    SH_CHECK(t.find("chara\\hero.tim") == 2);
    SH_CHECK(t.find("\\CHARA\\HERO.TIM") == 2);
    SH_CHECK(t.find("/XA/Z1_16180") == 4);
    SH_CHECK(t.find("CHARA/NOPE.TIM") == ld::DiscFiles::kNone);
    SH_CHECK(t.path(4) == "XA/Z1_16180");
    SH_CHECK(t.name(1) == "HERO.ILM");
    SH_CHECK(t.directory(1) == "CHARA");
    SH_CHECK(t.path(9).empty());
}

SH_TEST(disc_files_sizes) {
    const ld::DiscFiles t = sample();
    SH_CHECK(t.first_sector(0) == 100);
    SH_CHECK(t.bytes(0) == 33 * 256);
    SH_CHECK(t.sector_bytes(0) == 5 * 2048);
    SH_CHECK(t.sector_bytes(1) == 2048);
    SH_CHECK(t.bytes(-1) == 0);
}

SH_TEST(disc_files_searches) {
    const ld::DiscFiles t = sample();
    const int tim = 0, ilm = 7;
    SH_CHECK(t.next_of_type(tim, 0, 1) == 2);
    SH_CHECK(t.next_of_type(tim, 3, 1) == 0);   // wraps past the end
    SH_CHECK(t.next_of_type(tim, 0, -1) == 3);  // below 0 lands on the last file
    SH_CHECK(t.next_of_type(ilm, 1, 1) == 1);   // one full lap comes back to the start
    SH_CHECK(t.next_of_type(9, 0, 1) == ld::DiscFiles::kNone);
    SH_CHECK(t.next_named("HERO", tim, 0) == 2);
    SH_CHECK(t.next_named("HERO.XYZ", ilm, 0) == 1);
    SH_CHECK(t.next_named("HERO", tim, 3) == ld::DiscFiles::kNone);
    SH_CHECK(t.next_named("hero", tim, 0) == ld::DiscFiles::kNone);  // the table is upper case
}

SH_TEST(disc_files_remap) {
    ld::DiscFiles t = sample();
    std::vector<std::uint8_t> raw;
    for (const ft::Record& r : {rec("CHARA/HERO.TIM", 250, 9), rec("1ST/FONT16.TIM", 100, 33)}) {
        const auto bytes = ft::pack(r);
        raw.insert(raw.end(), bytes.begin(), bytes.end());
    }
    SH_CHECK(ld::DiscFiles::unpack(raw.data()).sector == 250);
    SH_CHECK(t.remap(raw) == 1);
    SH_CHECK(t.first_sector(2) == 250);
    SH_CHECK(t.bytes(2) == 9 * 256);
    SH_CHECK(t.moved_sector(210) == 250);
    SH_CHECK(t.moved_sector(100) == 100);
    SH_CHECK(t.moved_sector(999) == 999);
    SH_CHECK(t.find("CHARA/HERO.TIM") == 2);
}
