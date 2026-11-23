// SPDX-License-Identifier: GPL-3.0-or-later
// The pad driver calls of the game, answered from PadSystem.
#include "input/sdl_pad.h"

using sh::input::PadSystem;

namespace {

// Pad states as the driver reports them.
constexpr int kPadDisconnected = 0;
constexpr int kPadStable = 6;

// The port argument carries the slot in bit 4 (bits 0-1 are the multitap, unused here).
int slotOf(int port) { return (port >> 4) & 1; }

} // namespace

extern "C" {

// Pad communication completes every frame here. The vibration engine skips its
// DualShock detection forever if this says "not yet".
int PadChkVsync(void) { return 1; }

int PadGetState(int port)
{
    return PadSystem::instance().connected(slotOf(port)) ? kPadStable : kPadDisconnected;
}

int PadInfoMode(int, int, int) { return 7; }  // DualShock id as the game expects it

int PadSetMainMode(int, int, int) { return 0; }

// PS1 semantics: the buffer stays registered and is retransmitted every frame.
void PadSetAct(int port, unsigned char* table, int len)
{
    PadSystem& pads = PadSystem::instance();
    const int slot = slotOf(port);
    pads.setRumbleBuffer(slot, table, len);
    pads.rumble(slot, table, len);
}

}
