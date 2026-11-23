/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/* The story characters of a map (gamedata/maps/<map>/charas.json): which
 * animation table a character's animation status indexes, and how loud the sounds its animations cue are.
 * The tables differ from map to map (Cybil's, Dahlia's or Kaufmann's statuses mean different tables on maps that
 * carry different casts), so the executable keeps one set for the loaded map. */

#include <array>
#include <map>
#include <cstdint>
#include <vector>

namespace sh::maps {

/* Character number -> animation table index, with a table for every character not listed. */
class CharaTableLookup {
public:
    static constexpr int kNone = -1;

    CharaTableLookup() { clear(); }

    void clear() {
        by_chara_.clear();
        fallback_ = kNone;
    }
    /* The character's run-time number (any number a mod's character takes). */
    void set(int chara, int table) {
        if (table == kNone) {
            by_chara_.erase(chara);
        } else {
            by_chara_[chara] = table;
        }
    }
    void set_fallback(int table) { fallback_ = table; }
    int fallback() const { return fallback_; }

    /* kNone when the character has no table on this map. */
    int find(int chara) const {
        const auto it = by_chara_.find(chara);
        return it != by_chara_.end() ? it->second : fallback_;
    }

private:
    std::map<int, int> by_chara_;
    int fallback_ = kNone;
};

/* How an animation-cued sound is played: volume 0..255 and the sound flags (distance attenuation off, ...). */
struct CueSound {
    std::int32_t volume = 128;
    std::int32_t flags = 0;
};

/* Sound number -> how it plays; every other sound plays at the default. */
class CueSoundRules {
public:
    void clear() {
        rules_.clear();
        default_ = CueSound{};
    }
    void set(std::int32_t sound, CueSound how) {
        for (Rule& r : rules_) {
            if (r.sound == sound) {
                r.how = how;
                return;
            }
        }
        rules_.push_back(Rule{sound, how});
    }
    void set_default(CueSound how) { default_ = how; }
    CueSound find(std::int32_t sound) const {
        for (const Rule& r : rules_) {
            if (r.sound == sound) {
                return r.how;
            }
        }
        return default_;
    }

private:
    struct Rule {
        std::int32_t sound;
        CueSound how;
    };
    std::vector<Rule> rules_;
    CueSound default_{};
};

/* A sound cue on an animation keyframe: it fires once when the animation reaches `cue_keyframe` and re-arms
 * while the animation is before it again. `armed_bit` in `flags` remembers that it has fired. */
inline bool keyframe_cue(std::int32_t keyframe, std::int32_t cue_keyframe, std::int32_t& flags,
                         std::int32_t armed_bit) {
    if (keyframe >= cue_keyframe) {
        if ((flags & armed_bit) == 0) {
            flags |= armed_bit;
            return true;
        }
        return false;
    }
    flags &= ~armed_bit;
    return false;
}

}  // namespace sh::maps
