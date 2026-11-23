// SPDX-License-Identifier: GPL-3.0-or-later
// sh::effects::SfxMix, the pan and volume of a placed looping sound (engine/effects/sfx_mix.h): a recorded run
// step by step, a long seeded run against its recorded digest, and the rules in words.

#include "engine/effects/sfx_mix.h"

#include <cstdint>
#include <string>

#include "framework/golden.h"
#include "framework/sh_test.h"

using namespace sh::effects;

namespace {

constexpr const char* kTable = "effects/sfx_mix.txt";
constexpr std::int32_t kRange = 32 * 4096;

SfxMixMode mode_of(const std::string& name) {
    return name == "direct" ? SfxMixMode::Direct : name == "smoothed" ? SfxMixMode::Smoothed : SfxMixMode::Centered;
}

}  // namespace

SH_TEST(sfx_mix_run_as_recorded) {
    const auto rows = sh::test::golden_rows(kTable, "mix");
    SH_CHECK(rows.size() == 2500);
    SfxMix m;
    int differences = 0;
    for (const auto& r : rows) {
        const SfxMixOut o = m.update(static_cast<std::int32_t>(r.num(0)), static_cast<std::int32_t>(r.num(1)),
                                     static_cast<std::int32_t>(r.num(2)), static_cast<std::int32_t>(r.num(3)),
                                     mode_of(r.str(4)), static_cast<std::int32_t>(r.num(5)));
        if ((o.pan != r.num(6) || o.volume != r.num(7)) && ++differences <= 3) {
            sh::test::golden_mismatch(kTable, r, std::to_string(o.pan) + " " + std::to_string(o.volume));
        }
    }
    SH_CHECK(differences == 0);
}

SH_TEST(sfx_mix_seeded_run_digest) {
    const auto rows = sh::test::golden_rows(kTable, "digest");
    SH_CHECK(rows.size() == 1);
    for (const auto& r : rows) {
        sh::test::Sequence q(static_cast<std::uint64_t>(r.num(0)));
        sh::test::Digest d;
        SfxMix m;
        for (std::int64_t i = 0; i < r.num(1); ++i) {
            const std::int32_t balance = q.below(255) - 127;
            const std::int32_t base = q.one_in(4) ? 0 : q.below(300);
            const std::int32_t distance = q.below(200000);
            const std::int32_t mode = q.below(3);
            const std::int32_t dt = q.below(300);
            const SfxMixOut o = m.update(balance, base, distance, kRange, static_cast<SfxMixMode>(mode), dt);
            d.add(o.pan);
            d.add(o.volume);
        }
        SH_CHECK(sh::test::golden_digest_is(kTable, r, 2, d));
    }
}

// The direct mode passes the balance through; the centred mode puts the sound in the middle.
SH_TEST(sfx_mix_direct_and_centred_pan) {
    SfxMix m;
    SH_CHECK(m.update(-100, 128, 0, kRange, SfxMixMode::Direct, 68).pan == -100);
    SH_CHECK(m.update(77, 128, 0, kRange, SfxMixMode::Direct, 68).pan == 77);
    SH_CHECK(m.update(77, 128, 0, kRange, SfxMixMode::Centered, 68).pan == 0);
}

// The smoothed mode aims at half the balance and moves there by the slew rate times the frame time, never past it.
SH_TEST(sfx_mix_smoothed_pan_slews_to_half_the_balance) {
    SfxMix m;
    const std::int32_t dt = 4096 / 16;  // a sixteenth of a second: 0xC0 / 16 = 12 per step
    SH_CHECK(m.update(100, 128, 0, kRange, SfxMixMode::Smoothed, dt).pan == 12);
    SH_CHECK(m.update(100, 128, 0, kRange, SfxMixMode::Smoothed, dt).pan == 24);
    for (int i = 0; i < 2; ++i) m.update(100, 128, 0, kRange, SfxMixMode::Smoothed, dt);
    SH_CHECK(m.update(100, 128, 0, kRange, SfxMixMode::Smoothed, dt).pan == 50);  // 60 would overshoot the target
    SH_CHECK(m.update(100, 128, 0, kRange, SfxMixMode::Smoothed, dt).pan == 50);
    SH_CHECK(m.update(-100, 128, 0, kRange, SfxMixMode::Smoothed, dt).pan == 38);
    m.reset();
    SH_CHECK(m.pan() == 0);
}

// The volume term is the base scaled by distance over range, less the base and one: it climbs from below minus the
// base at the source to the cap at the range and stays there. A missing base volume is the default one, the
// smoothed and centred modes use half the base, and a base above the cap gives 0 while the term is under it.
SH_TEST(sfx_mix_volume_term_climbs_with_distance) {
    SfxMix m;
    const auto volume = [&](std::int32_t base, std::int32_t distance, SfxMixMode mode) {
        return m.update(0, base, distance, kRange, mode, 0).volume;
    };
    SH_CHECK(volume(128, 0, SfxMixMode::Direct) == -129);
    SH_CHECK(volume(128, kRange / 2, SfxMixMode::Direct) == 64 - 129);
    SH_CHECK(volume(128, kRange, SfxMixMode::Direct) == 0xFF);
    SH_CHECK(volume(128, kRange * 2, SfxMixMode::Direct) == 0xFF);
    SH_CHECK(volume(0, kRange / 2, SfxMixMode::Direct) == 64 - 129);
    SH_CHECK(volume(128, kRange / 2, SfxMixMode::Centered) == 32 - 65);
    SH_CHECK(volume(128, kRange / 2, SfxMixMode::Smoothed) == 32 - 65);
    SH_CHECK(volume(300, 0, SfxMixMode::Direct) == 0);
    SH_CHECK(volume(300, kRange / 2, SfxMixMode::Direct) == 150 - 301);
}
