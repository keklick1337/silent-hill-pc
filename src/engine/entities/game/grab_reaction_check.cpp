// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/entities/game/grab_reaction_check.h"

#include <algorithm>
#include <span>

#include "engine/game_state/game_state_ui.h"

namespace sh::entities {

namespace {

namespace gs = sh::game_state;

// An animation and its phase, as the reaction table names them: the animation's number doubled, plus one for the
// playback phase (even is the blend into it).
constexpr int blend(int animation) { return animation << 1; }
constexpr int playback(int animation) { return (animation << 1) | 1; }

// The player's base animations the grab states play.
constexpr int kTorsoHit = 105;
constexpr int kTorsoGrabFront = 115;
constexpr int kTorsoGrabBack = 117;
constexpr int kLegGrabBack = 118;
constexpr int kTorsoReleaseFront = 120;
constexpr int kTorsoReleaseBack = 122;
constexpr int kLegReleaseBack = 123;
constexpr int kNeckGrab = 125;
constexpr int kPinFrontStart = 127;
constexpr int kPinFront = 128;
constexpr int kPinFrontRelease = 129;
constexpr int kPinFrontRecover = 130;
constexpr int kPinBackRecover = 131;

// Every link of a grab's chain of states looks its animation up by exact match, so each of them needs a row.
// Taken from the states themselves: the grab state's animation, then those of the states it hands over to.
constexpr int kTorsoFront[] = {blend(kTorsoGrabFront), blend(kTorsoReleaseFront)};
constexpr int kTorsoBack[] = {blend(kTorsoGrabBack), blend(kTorsoReleaseBack)};
// A leg grab from the front plays the playback phase of the torso's rear pair.
constexpr int kLegsFront[] = {playback(kTorsoGrabBack), playback(kTorsoReleaseBack)};
constexpr int kLegsBack[] = {blend(kLegGrabBack), blend(kLegReleaseBack)};
// The neck grab has one animation for both sides and no release row on the one map that has it: its release
// state finds none, stays on the grab's looping row and ends at that row's last keyframe - the original's own way
// out, so only the grab is asked for.
constexpr int kNeck[] = {playback(kNeckGrab)};
// The pin has four links a side. Its first state has no struggle timer, only the keyframe gate, and the last
// links gate the return to normal: a hole anywhere holds the player.
constexpr int kPinFromFront[] = {playback(kPinFrontStart), blend(kPinFront), playback(kPinFrontRelease),
                                 playback(kPinFrontRecover)};
constexpr int kPinFromBack[] = {playback(kPinFront), blend(kPinFrontRelease), blend(kPinFrontRecover),
                                blend(kPinBackRecover)};

struct Grab {
    int attack;
    std::span<const int> front;
    std::span<const int> back;
};

constexpr Grab kGrabs[] = {
    {45, kTorsoFront, kTorsoBack},
    {56, kTorsoFront, kTorsoBack},
    {49, kLegsFront, kLegsBack},
    {66, kNeck, kNeck},
    {54, kPinFromFront, kPinFromBack},
};

// Whether the map's reaction table has a row for this animation and phase. The table ends with an all-zero row;
// the search stops there, where the game's own fixed-length scan would go on into whatever follows the table.
bool map_has_reaction(int status) {
    gs::MapHarryAnimRow row;
    for (int index = 0; gs::map_harry_anim_row(index, &row); ++index) {
        if (row.map_status == 0 && row.harry_status == 0) {
            return false;
        }
        if (row.harry_status == status) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool grab_reaction_playable(int attack, bool frontal) {
    for (const Grab& grab : kGrabs) {
        if (grab.attack == attack) {
            const std::span<const int> chain = frontal ? grab.front : grab.back;
            return std::all_of(chain.begin(), chain.end(), map_has_reaction);
        }
    }
    return true;
}

bool hit_reaction_playable(bool frontal) {
    return map_has_reaction(frontal ? blend(kTorsoHit) : playback(kTorsoHit));
}

}  // namespace sh::entities
