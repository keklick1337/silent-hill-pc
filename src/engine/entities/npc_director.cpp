// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The director of the map NPCs.

#include "engine/entities/npc_director.h"

#include <algorithm>

namespace sh::entities {

namespace {

constexpr std::int32_t kCellSize = 40 << 12;  // one map chunk, Q12 metres

std::int32_t metres(std::int32_t m) { return m << 12; }
std::int32_t square(std::int32_t m) { return m * m; }

}  // namespace

void NpcDirector::spawn_pass(NpcHost& host, bool inner_ring, const std::array<SpawnRow, kRowCount>& rows,
                             std::int32_t group_chara_low, std::int32_t group_chara_high) {
    const NpcRules::Spawn& sp = rules_.spawn;
    int slot = 0;
    for (int i = 0; i < kRowCount && host.frame_vblanks() < sp.frame_budget_vblanks; ++i) {
        const std::int32_t limit = host.npc_slot_limit();
        const std::uint32_t full = limit >= 32 ? 0xFFFFFFFFu : ((1u << limit) - 1u);
        if (host.npc_slots_taken() == full) {
            break;
        }
        const SpawnRow& row = rows[static_cast<std::size_t>(i)];
        std::int32_t& cooldown = cooldown_[static_cast<std::size_t>(i)];
        if (cooldown > 0) {
            --cooldown;
        }
        if (host.spawning_blocked() || !host.row_enabled(i) || host.row_occupied(i) || row.flags == 0 ||
            !host.difficulty_allows(row.difficulty_min) || !host.ground_allows(row.x, row.z)) {
            continue;
        }
        std::int32_t px = 0, pz = 0;
        host.player_position(px, pz);
        bool in_range;
        if (host.long_view()) {
            in_range = !outside_radius(px, pz, row.x, row.z, metres(sp.long_view_outer)) &&
                       (!inner_ring || (outside_radius(px, pz, row.x, row.z, metres(sp.long_view_inner)) &&
                                        !host.point_in_view(row.x, row.z)));
        } else {
            in_range = !outside_radius(px, pz, row.x, row.z, metres(sp.ring_outer)) &&
                       (!inner_ring || outside_radius(px, pz, row.x, row.z, metres(sp.ring_inner)));
        }
        if (!in_range || cooldown != 0) {
            continue;
        }
        cooldown = sp.cooldown_ticks;
        while (slot < kSlotCapacity && (host.npc_slots_taken() >> slot & 1u) != 0) {
            ++slot;
        }
        if (slot >= kSlotCapacity) {
            break;
        }
        SpawnRequest request;
        request.row = i;
        request.slot = slot;
        request.chara = row.chara > 0 ? row.chara : (i < 16 ? group_chara_low : group_chara_high);
        request.data = row;
        host.spawn(request);
    }
}

void NpcDirector::begin_tick(NpcHost& host) {
    host.player_position(player_x_, player_z_);
    radio_.reset();
}

NpcReview NpcDirector::review(NpcHost& host, int slot, const NpcView& npc) {
    NpcReview out;
    if (!is_monster(npc.chara)) {
        return out;
    }
    const std::uint32_t dist = tracked_distance_sq(npc.x, npc.z, player_x_, player_z_);
    const bool chunked = host.chunked_interior();

    if (npc.health >= 0 && !npc.no_radio_static) {
        bool same_cell = true;
        if (chunked) {
            same_cell = (npc.x + kCellSize * 4) / kCellSize == (player_x_ + kCellSize * 4) / kCellSize &&
                        (npc.z + kCellSize * 4) / kCellSize == (player_z_ + kCellSize * 4) / kCellSize;
        }
        if (same_cell) {
            radio_.offer(slot, dist, npc.x, npc.z);
        }
    }

    const NpcRules::Despawn& d = rules_.despawn;
    const bool corpse_near = !chunked && npc.health < 0;
    bool free_it;
    if (host.long_view()) {
        free_it = dist > static_cast<std::uint32_t>(square(d.long_view_hard)) ||
                  (dist > static_cast<std::uint32_t>(square(corpse_near ? d.long_view_corpse : d.long_view_alive)) &&
                   !host.point_in_view(npc.x, npc.z));
    } else {
        free_it = dist > static_cast<std::uint32_t>(square(corpse_near ? d.corpse : d.alive));
    }
    if (free_it && !npc.remains) {
        out.free_slot = true;
        return out;
    }
    if (!host.long_view()) {
        std::int32_t cx = 0, cz = 0;
        const std::int32_t hide_sq = square(d.hide_distance);
        bool hide;
        if (host.hide_distance_from_player()) {
            hide = dist > static_cast<std::uint32_t>(hide_sq);
        } else {
            host.camera_position(cx, cz);
            hide = shifted_difference_sq(cx, cz, npc.x, npc.z) > hide_sq;
        }
        out.visibility = hide ? Visibility::Hide : Visibility::Show;
    } else {
        out.visibility = Visibility::Show;
    }
    return out;
}

int NpcDirector::end_tick(NpcHost& host, bool radio_on, std::array<RadioChannel, kRadioSlots>& channels) {
    const int pitch = radio_.pitch_state();
    if (!radio_on) {
        return pitch;
    }
    int heard = 0;
    while (heard < kRadioEntries && radio_.entry(heard).slot != kNoSlot) {
        ++heard;
    }
    if (!host.radio_static_allowed(heard, radio_.entry(0).slot, radio_.entry(0).distance_sq)) {
        for (int l = 0; l < kRadioSlots; ++l) {
            RadioChannel& ch = channels[static_cast<std::size_t>(l)];
            if (ch.playing != kNoSlot) {
                host.radio_stop(l);
            }
            ch.playing = kNoSlot;
            ch.target = kNoSlot;
            ch.entry = kNoSlot;
        }
        return pitch;
    }
    RadioSlotSet taken;
    for (RadioChannel& ch : channels) {
        int idx = ch.target == kNoSlot ? kNoSlot : radio_.index_of(ch.target);
        if (idx >= 0) {
            ch.entry = idx;
            taken.set(static_cast<std::size_t>(ch.target));
        } else {
            ch.target = kNoSlot;
        }
    }
    for (RadioChannel& ch : channels) {
        if (ch.target == kNoSlot) {
            const int idx = radio_.take_free(taken);
            ch.entry = idx;
            ch.target = idx != kNoSlot ? radio_.entry(idx).slot : kNoSlot;
        }
    }
    const bool half = !host.chunked_interior();  // outdoors the static carries twice as far
    for (int l = 0; l < kRadioSlots; ++l) {
        RadioChannel& ch = channels[static_cast<std::size_t>(l)];
        if (ch.playing == kNoSlot) {
            if (ch.target >= 0) {
                host.radio_start(l);
            }
        } else if (ch.target >= 0) {
            const RadioEntry& e = radio_.entry(ch.entry);
            const std::int32_t balance = host.stereo_balance(e.x, e.z);
            std::int32_t volume = host.square_root_q12(static_cast<std::int32_t>(e.distance_sq << 12)) >> 8;
            if (half) {
                volume >>= 1;
            }
            host.radio_update(l, balance, std::clamp(volume, 0, 255));
        } else {
            host.radio_stop(l);
        }
        ch.playing = ch.target;
    }
    return pitch;
}

const NpcRules::IdleClip* NpcDirector::idle_clip(std::int32_t chara) const {
    for (const NpcRules::IdleClip& c : rules_.idle_clips) {
        if (c.number == chara) {
            return &c;
        }
    }
    return nullptr;
}

}  // namespace sh::entities

namespace sh::entities {

void RadioChannels::forget(NpcHost& host) {
    for (int i = 0; i < kRadioSlots; ++i) {
        RadioChannel& ch = channels_[static_cast<std::size_t>(i)];
        ch.entry = kNoSlot;
        ch.target = kNoSlot;
        ch.spare = 0;
        // Also "not playing": a load that skips the silencing would otherwise keep a stale zero and never start.
        ch.playing = kNoSlot;
        host.radio_stop(i);
    }
}

void RadioChannels::silence(NpcHost& host) {
    for (RadioChannel& ch : channels_) {
        ch.playing = kNoSlot;
    }
    for (int i = 0; i < kRadioSlots; ++i) {
        host.radio_stop(i);
    }
}

void NpcDirector::restart_if_due(NpcSlots& slots, int slot, std::int32_t chara) {
    const NpcRules::StateRestart& sr = rules_.state_restart;
    for (std::size_t i = 0; i < sr.session_numbers.size(); ++i) {
        if (sr.session_numbers[i] == chara) {
            if (!session_restarted_[i]) {
                slots.restart_state(slot);
                session_restarted_[i] = true;
            }
            return;
        }
    }
    if (std::find(sr.spawn_numbers.begin(), sr.spawn_numbers.end(), chara) == sr.spawn_numbers.end() ||
        slot >= sr.spawn_slots || slot >= kSlotCapacity) {
        return;
    }
    const std::size_t s = static_cast<std::size_t>(slot);
    if (restart_chara_plus1_[s] != chara + 1) {
        restart_done_[s] = false;
        restart_chara_plus1_[s] = chara + 1;
    }
    if (!restart_done_[s] && slots.control_state(slot) == 0) {
        slots.restart_state(slot);
        restart_done_[s] = true;
    }
}

int NpcDirector::update(NpcHost& host, NpcSlots& slots, RadioChannels& radio, bool radio_on) {
    begin_tick(host);
    const int count = slots.slot_count();
    for (int k = 0; k < count; ++k) {
        const NpcView view = slots.view(k);
        if (!is_actor(view.chara)) {
            continue;
        }
        const NpcReview r = review(host, k, view);
        if (r.free_slot) {
            slots.free(k);
            continue;
        }
        if (r.visibility != Visibility::Keep) {
            slots.set_visible(k, r.visibility == Visibility::Show);
        }
        slots.unlock_animation(k);
        // Still loading: skip the tick but keep the slot (map code expects the character).
        if (!slots.animation_loaded(k)) {
            continue;
        }
        if (!slots.has_behaviour(k)) {
            // No behaviour on this map: pose it and draw it, so the model is not lost.
            if (slots.visible(k)) {
                slots.pose_still(k, idle_clip(view.chara), rules_.idle_clip_duration);
                slots.draw(k);
            }
            continue;
        }
        restart_if_due(slots, k, view.chara);
        slots.before_behaviour(k);
        if (!slots.pose_ready(k)) {
            continue;
        }
        slots.behave(k);
        if (slots.visible(k)) {
            slots.draw(k);
        }
    }
    return end_tick(host, radio_on, radio.channels());
}

}  // namespace sh::entities
