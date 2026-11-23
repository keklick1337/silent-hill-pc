// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/items/powered_weapon.h"

namespace sh::items {

namespace {

constexpr std::int32_t kFadeOut = -1;
// Frame time units per power step while running down (two steps a frame at 30 fps).
constexpr std::int32_t kRunDownDivisor = 68;

}  // namespace

void PoweredWeaponMotor::handle(std::int32_t held_attack, std::int8_t event, std::int32_t map_index,
                                std::int32_t frame_time, MotorHost& host) {
    const bool drill = held_attack == kRockDrillAttack;

    switch (static_cast<MotorEvent>(event)) {
        case MotorEvent::Reset:
            map_index_ = map_index;
            pitch_ = 0;
            running_ = 0;
            power_ = kFullPower;
            host.stop_all_sounds();
            host.clear_power_timer();
            break;

        case MotorEvent::PowerOn:
            map_index_ = map_index;
            pitch_ = 0;
            power_ = kFullPower;
            host.motor_sound(!drill, kFullPower, 0);
            host.base_sound(0);
            running_ |= drill ? kDrillRunning : kChainsawRunning;
            break;

        case MotorEvent::DrillEngage:
            if (drill) {
                host.drill_bit_sound(static_cast<std::uint8_t>(power_), pitch_);
                running_ |= kDrillBitEngaged;
            }
            break;

        case MotorEvent::PowerDown: {
            if (power_ == 0) {
                break;
            }
            const std::int32_t step = frame_time / kRunDownDivisor;
            // The byte wraps before the clamp, and the pitch is taken from the wrapped value.
            power_ = static_cast<std::int8_t>(power_ - (step == 0 ? 1 : step));
            pitch_ = static_cast<std::int8_t>(power_ - kFullPower);
            power_ = power_ < 0 ? std::int8_t{0} : (power_ > kFullPower ? kFullPower : power_);

            host.motor_sound(!drill, 0, 0);
            host.base_sound(kFadeOut);
            if (drill) {
                host.drill_bit_sound(static_cast<std::uint8_t>(power_), pitch_);
            }
            if (power_ == 0) {
                host.stop_all_sounds();
                running_ = 0;
                pitch_ = 0;
                host.clear_power_timer();
                host.clear_power_effect();
            }
            break;
        }

        case MotorEvent::DrillRelease:
            if ((running_ & kDrillRunning) != 0 && (running_ & kDrillBitEngaged) == 0) {
                host.motor_sound(false, 0, 0);
                host.base_sound(0);
                host.clear_power_timer();
                running_ = static_cast<std::uint8_t>(running_ - kDrillRunning);
            }
            break;

        case MotorEvent::Sustain:
            if (map_index != map_index_) {
                map_index_ = map_index;
                pitch_ = 0;
                power_ = kFullPower;
                running_ = drill ? static_cast<std::uint8_t>(kDrillRunning | kDrillBitEngaged) : kChainsawRunning;
            }
            host.motor_sound(!drill, static_cast<std::uint8_t>(power_), pitch_);
            if (drill) {
                host.drill_bit_sound(static_cast<std::uint8_t>(power_), pitch_);
            }
            host.base_sound(kFadeOut);
            break;
    }
}

}  // namespace sh::items
