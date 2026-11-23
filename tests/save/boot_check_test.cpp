// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// BootCheck: the wait count of the card check and of the options load, the outcome from the list, the options
// of the newest save applied once.
#include "framework/sh_test.h"
#include "engine/save/boot_check.h"

#include <vector>

namespace {

using sh::save::BootCheck;
using sh::save::SaveCatalog;

SH_TEST(boot_check_waits_then_reports_the_list) {
    std::vector<std::int32_t> saves;
    SaveCatalog catalog([&](std::int32_t* out, int capacity) {
        int n = 0;
        for (std::int32_t s : saves) {
            if (n < capacity) out[n] = s;
            ++n;
        }
        return n;
    });
    BootCheck check(catalog, [](std::int32_t) { return true; }, 2, 0);
    SH_CHECK(check.check() == BootCheck::Outcome::Pending);
    SH_CHECK(check.check() == BootCheck::Outcome::Pending);
    SH_CHECK(check.check() == BootCheck::Outcome::NoSave);
    saves = {7, 3};
    SH_CHECK(check.check() == BootCheck::Outcome::HasSave);  // read afresh, not from the earlier count
}

SH_TEST(boot_check_load_applies_options_of_the_newest) {
    std::vector<std::int32_t> saves = {12, 5};
    SaveCatalog catalog([&](std::int32_t* out, int capacity) {
        int n = 0;
        for (std::int32_t s : saves) {
            if (n < capacity) out[n] = s;
            ++n;
        }
        return n;
    });
    std::int32_t applied_to = 0;
    int applies = 0;
    BootCheck check(catalog, [&](std::int32_t n) { applied_to = n; ++applies; return n == 12; }, 0, 3);
    SH_CHECK(check.check() == BootCheck::Outcome::HasSave);
    check.begin_load();
    SH_CHECK(!check.poll_load() && !check.poll_load() && !check.poll_load());
    SH_CHECK(applies == 0);
    SH_CHECK(check.poll_load() && check.options_applied() && applied_to == 12 && applies == 1);

    BootCheck refused(catalog, [](std::int32_t) { return false; }, 0, 0);
    refused.begin_load();
    SH_CHECK(refused.poll_load() && !refused.options_applied());
}

}  // namespace
