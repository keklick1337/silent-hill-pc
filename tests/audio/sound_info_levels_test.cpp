// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/audio/sound_registry.h"

SH_TEST(sound_info_levels_and_pitch) {
    sh::audio::SoundInfo info;
    info.volume = 1.0f;
    info.pan = 0.0f;
    info.pitch = 0.0f;

    int l = 0, r = 0;
    info.play_levels(100, 90, &l, &r);
    SH_CHECK(l == 100 && r == 90);  // unity, centred

    // Full pan: the far side is clamped away, the near side untouched by pan.
    info.pan = -1.0f;
    info.play_levels(100, 90, &l, &r);
    SH_CHECK(l == 100 && r == 0);
    info.pan = 1.0f;
    info.play_levels(100, 90, &l, &r);
    SH_CHECK(l == 0 && r == 90);

    // Volume is a straight gain; over-unity pan clamps at 1.0 on the far side.
    info.volume = 0.5f;
    info.pan = 0.25f;
    info.play_levels(100, 100, &l, &r);
    SH_CHECK(l == static_cast<int>(100 * 0.5f * 0.75f));
    SH_CHECK(r == static_cast<int>(100 * 0.5f * 1.0f));

    // The volume path keeps its own multiply order but the same factors here.
    info.volume_levels(100, 100, &l, &r);
    SH_CHECK(l == static_cast<int>(100 * 0.5f * 0.75f));
    SH_CHECK(r == static_cast<int>(100 * 0.5f * 1.0f));

    // Pitch: registry semitones plus live cents.
    info.pitch = 2.0f;
    SH_CHECK(info.pitch_semitones(0) == 2.0f);
    SH_CHECK(info.pitch_semitones(-50) == 1.5f);
    SH_CHECK(info.pitch_semitones(1200) == 14.0f);
}
