// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The powered weapons' motor (chainsaw and rock drill): a power level that starts at 32 when the weapon is switched on and runs
// down to 0 when it is switched off, the remembered map (a map change restarts the motor), and
// which of the motor sounds are running. The module decides; the host plays the four motor
// sound channels and clears the player's power state. Standard library only.

#pragma once

#include <cstdint>

namespace sh::items {

// What the caller asks of the motor. Any other value is ignored, as in the original.
enum class MotorEvent : std::int8_t {
    Reset = -1,         // inventory opened, weapon swapped: silence and forget everything
    PowerOn = 0,        // the weapon starts: motor at full power
    DrillEngage = 1,    // the drill bit sound joins the motor (rock drill only)
    PowerDown = 2,      // one frame of running down
    DrillRelease = 3,   // the drill's motor stops unless its bit is engaged
    Sustain = 4,        // one frame of keeping the motor going (restarts it on a new map)
};

// Side effects, called in the original's order. A volume of -1 fades the channel out; 0 stops
// it; the second number of a channel is its pitch offset.
class MotorHost {
public:
    virtual ~MotorHost() = default;
    virtual void stop_all_sounds() = 0;                                     // all four channels to 0
    virtual void base_sound(std::int32_t volume) = 0;                       // the common hum
    virtual void motor_sound(bool chainsaw, std::int32_t volume, std::int32_t pitch) = 0;  // chainsaw or drill motor
    virtual void drill_bit_sound(std::int32_t volume, std::int32_t pitch) = 0;
    virtual void clear_power_timer() = 0;   // the player's gas weapon power timer
    virtual void clear_power_effect() = 0;  // the player's running-weapon attack state (the drill smoke)
};

class PoweredWeaponMotor {
public:
    // Bits of `running()`.
    static constexpr std::uint8_t kChainsawRunning = 1 << 0;
    static constexpr std::uint8_t kDrillRunning = 1 << 1;
    static constexpr std::uint8_t kDrillBitEngaged = 1 << 2;
    static constexpr std::int8_t kFullPower = 32;
    // The packed attack (weapon id + 10 * input type) the motor reads as "the rock drill".
    static constexpr std::int32_t kRockDrillAttack = 2;

    // `held_attack` is the packed attack of the held weapon; `map_index` the current map;
    // `frame_time` the frame time in 4096 per second.
    void handle(std::int32_t held_attack, std::int8_t event, std::int32_t map_index, std::int32_t frame_time,
                MotorHost& host);

    std::int32_t map_index() const { return map_index_; }
    std::int8_t power() const { return power_; }
    std::int8_t pitch() const { return pitch_; }
    std::uint8_t running() const { return running_; }

private:
    std::int32_t map_index_ = 0;
    std::int8_t power_ = 0;   // 32 at full power, 0 stopped
    std::int8_t pitch_ = 0;   // power - 32 while running down
    std::uint8_t running_ = 0;
};

// The Hyper Blaster's beam colour (0 red, 1 yellow, 2 green, 3 rainbow) from the 5-bit count of
// special items picked up: one colour per eight.
constexpr std::int32_t hyper_blaster_beam_color(std::uint8_t special_items_picked_up) {
    return special_items_picked_up >> 3;
}

}  // namespace sh::items
