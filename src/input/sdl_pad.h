// SPDX-License-Identifier: GPL-3.0-or-later
// The two pad ports of the game, fed by SDL keyboard, mouse and gamepads (input/pad.h is the
// engine-facing side).
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>

#include "engine/game_state/raw_pad.h"
#include "input/pad.h"
#include "input/phone_rumble.h"

namespace sh::input {

// What the game polls for one pad port (kind 0xFF: no pad).
using PadPortBlock = sh::game::RawPad;

class PadSystem
{
public:
    static constexpr int kPorts = 2;

    static PadSystem& instance();

    // Starts SDL gamepads and adopts the ones already plugged in. False when SDL refuses.
    bool init();
    void onDeviceAdded(SDL_JoystickID instance);
    void onDeviceRemoved(SDL_JoystickID instance);

    // The game hands over the block of a port; it is reset to neutral.
    void attach(int port, PadPortBlock* block);
    void enableUpdates() { updatesEnabled_ = true; }

    // Once per frame: rewrites every attached block from keyboard, mouse and pads
    // and retransmits the rumble buffers.
    void update();

    // Keyboard and mouse as an active-low word (0xFFFF when nothing is held).
    std::uint16_t keyboardWord();

    // True when something answers on that port (port 0 always: the keyboard).
    bool connected(int port) const;

    // The game registers a live rumble buffer that it keeps mutating.
    void setRumbleBuffer(int port, std::uint8_t* table, int length);
    void rumble(int pad, const std::uint8_t* table, int length);

    // Queries for the controls panel, binds and blocking loops.
    bool skipHeld() const;
    bool buttonHeld(int sdlButton) const;
    bool bindHeld(int buttonOrAxis) const;
    const char* connectedName() const;
    const char* heldBindName() const;
    int axisValue(int sdlAxis) const;

    const std::uint8_t* keyboardState() const { return keys_; }
    void setActionSource(std::uint16_t (*source)()) { actionSource_ = source; }
    void setTouchSource(TouchSource source) { touchSource_ = source; }
    void setTouchSkip(TouchSkip skip) { touchSkip_ = skip; }

private:
    struct Slot
    {
        SDL_Gamepad* gamepad = nullptr;
        SDL_JoystickID instance = 0;  // 0 while closed (SDL ids start at 1)
        std::uint16_t latch[2] = {0xFFFF, 0xFFFF};  // post-hysteresis words of both bind sets
    };

    bool live(int slot) const;
    void open(SDL_JoystickID instance, int slot);
    void close(int slot);
    void adopt(SDL_JoystickID instance);
    void logUnmapped() const;
    void steamHint() const;
    // Button word and sticks of one pad; returns true when the pad is in use.
    bool readPad(int slot, std::uint16_t& word, short axes[4]);
    bool mergePads(PadPortBlock& block);
    // Android: port 0's rumble on the phone's own vibrator while the pad in use (`target`) has no motors.
    void phoneRumble(int target);

    std::array<Slot, kPorts> slots_{};
    std::array<PadPortBlock*, kPorts> blocks_{};
    std::array<std::uint8_t*, kPorts> rumbleTable_{};
    std::array<int, kPorts> rumbleLength_{};
    int activeSlot_ = 0;
    bool updatesEnabled_ = false;
    const std::uint8_t* keys_ = nullptr;
    std::uint16_t (*actionSource_)() = nullptr;
    TouchSource touchSource_ = nullptr;
    TouchSkip touchSkip_ = nullptr;
    bool touchStickWas_ = false;
#if defined(SDL_PLATFORM_ANDROID)
    SDL_Haptic* phoneVibrator_ = nullptr;
    bool phoneVibratorLooked_ = false;
    PhoneRumble phoneRumble_;
#endif
};

} // namespace sh::input
