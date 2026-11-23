// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/world/part_variants.h"

#include <vector>

using namespace sh::world;

namespace {

const char* kTwoSlots = R"({
  "format": "sh-part-variants",
  "slots": [
    { "shift": 0, "bits": 4, "hide": [19, 20, 21, 22],
      "variants": { "1": { "show": [19] }, "5": { "show": [] } } },
    { "shift": 4, "bits": 4,
      "variants": { "1": { "hide": [18, 19], "show": [18] }, "2": { "hide": [18], "show": [18, 19] } } }
  ]
})";

}  // namespace

SH_TEST(part_variants_slot_order) {
    auto pv = PartVariants::parse(kTwoSlots);
    SH_CHECK(static_cast<bool>(pv));
    std::vector<PartToggle> t;
    pv.value().toggles(0x21, t);  // slot 0 = 1, slot 1 = 2
    const std::vector<std::pair<int, bool>> want = {{19, false}, {20, false}, {21, false}, {22, false},
                                                    {19, true},  {18, false}, {18, true},  {19, true}};
    SH_CHECK(t.size() == want.size());
    for (std::size_t i = 0; i < want.size() && i < t.size(); ++i) {
        SH_CHECK(static_cast<int>(t[i].part) == want[i].first);
        SH_CHECK(t[i].visible == want[i].second);
    }
}

SH_TEST(part_variants_zero_and_unlisted_values) {
    auto pv = PartVariants::parse(kTwoSlots);
    SH_CHECK(static_cast<bool>(pv));
    std::vector<PartToggle> t;
    pv.value().toggles(0x00, t);
    SH_CHECK(t.empty());
    pv.value().toggles(0x07, t);  // no variant 7: the slot's hide list only
    SH_CHECK(t.size() == static_cast<std::size_t>(4));
    t.clear();
    pv.value().toggles(0x30, t);  // slot 1 = 3 without a variant and without a hide list
    SH_CHECK(t.empty());
    t.clear();
    pv.value().toggles(0x100, t);  // bits above both slots
    SH_CHECK(t.empty());
}

SH_TEST(part_variants_rejects_bad_files) {
    SH_CHECK(!PartVariants::parse("{}"));
    SH_CHECK(!PartVariants::parse(R"({"format":"sh-part-variants"})"));
    SH_CHECK(!PartVariants::parse(R"({"format":"sh-part-variants","slots":[{"bits":2,"variants":{"4":{}}}]})"));
    SH_CHECK(!PartVariants::parse(R"({"format":"sh-part-variants","slots":[{"variants":{"x":{}}}]})"));
    SH_CHECK(!PartVariants::parse(R"({"format":"sh-part-variants","slots":[{"hide":[300]}]})"));
    SH_CHECK(static_cast<bool>(PartVariants::parse(R"({"format":"sh-part-variants","slots":[]})")));
}
