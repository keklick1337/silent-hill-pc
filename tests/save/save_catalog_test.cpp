// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// SaveCatalog: the save count the title menu shows, capped at 11, re-read once per refresh interval.
#include "framework/sh_test.h"
#include "engine/save/save_catalog.h"

#include <vector>

namespace {

using sh::save::SaveCatalog;

SH_TEST(save_catalog_counts_and_caps) {
    std::vector<std::int32_t> saves;
    int reads = 0;
    SaveCatalog catalog(
        [&](std::int32_t* out, int capacity) {
            ++reads;
            int n = 0;
            for (std::int32_t s : saves) {
                if (n < capacity) {
                    out[n] = s;
                }
                ++n;
            }
            return n;  // the real lister returns the total, not the capped count
        },
        3);
    catalog.poll();
    SH_CHECK(catalog.count() == 0 && !catalog.any() && catalog.newest() == 0 && reads == 1);

    saves = {9, 8};
    catalog.poll();  // not due yet
    SH_CHECK(catalog.count() == 0 && reads == 1);
    catalog.poll();
    catalog.poll();
    SH_CHECK(catalog.count() == 2 && catalog.newest() == 9 && reads == 2);

    saves.clear();
    for (int i = 20; i > 0; --i) {
        saves.push_back(i);
    }
    catalog.invalidate();
    catalog.poll();
    SH_CHECK(catalog.count() == 11 && catalog.newest() == 20);
}

}  // namespace
