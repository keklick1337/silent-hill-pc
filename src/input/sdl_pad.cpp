// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/sdl_pad.h"

#include "engine/config/pc_config.h"
#include "engine/log/sh_log.h"
#include "input/pad.h"
#include "input/pointer_math.h"
#include "input/stick_shape.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

// Read by the screens and hosts that poll raw keys; kept in step with the SDL keyboard array.
extern "C" {
const unsigned char* g_sdlKeyboardState = nullptr;
}

namespace sh::input {
namespace {

constexpr std::uint16_t kNone = 0xFFFF;
constexpr std::uint16_t kDpadBits = kButtonUp | kButtonRight | kButtonDown | kButtonLeft;
constexpr int kPressHigh = 24000;  // a value must exceed this to press...
constexpr int kReleaseLow = 8000;  // ...and fall under this to release
constexpr int kHalfScale = 16384;
constexpr int kActiveStick = 12000;

// One pad button: its bit in the active-low word and where each bind set stores it.
struct Button
{
    std::uint16_t bit;
    int ControllerBindings::* pad;
    int KeyboardBindings::* key;
};

constexpr Button kButtons[] = {
    {kButtonSquare, &ControllerBindings::square, &KeyboardBindings::square},
    {kButtonCircle, &ControllerBindings::circle, &KeyboardBindings::circle},
    {kButtonTriangle, &ControllerBindings::triangle, &KeyboardBindings::triangle},
    {kButtonCross, &ControllerBindings::cross, &KeyboardBindings::cross},
    {kButtonL1, &ControllerBindings::l1, &KeyboardBindings::l1},
    {kButtonR1, &ControllerBindings::r1, &KeyboardBindings::r1},
    {kButtonL2, &ControllerBindings::l2, &KeyboardBindings::l2},
    {kButtonR2, &ControllerBindings::r2, &KeyboardBindings::r2},
    {kButtonUp, &ControllerBindings::dpad_up, &KeyboardBindings::dpad_up},
    {kButtonDown, &ControllerBindings::dpad_down, &KeyboardBindings::dpad_down},
    {kButtonLeft, &ControllerBindings::dpad_left, &KeyboardBindings::dpad_left},
    {kButtonRight, &ControllerBindings::dpad_right, &KeyboardBindings::dpad_right},
    {kButtonL3, &ControllerBindings::l3, &KeyboardBindings::l3},
    {kButtonR3, &ControllerBindings::r3, &KeyboardBindings::r3},
    {kButtonSelect, &ControllerBindings::select, &KeyboardBindings::select},
    {kButtonStart, &ControllerBindings::start, &KeyboardBindings::start},
};

// A bind is a button index, or an axis index with the AXIS flag (triggers, sticks).
// Digital buttons read as 0 or 32767.
int bindValue(SDL_Gamepad* pad, int bind)
{
    if (bind < 0)
        return 0;
    if (bind & kBindAxis)
    {
        const int axis = bind & ~(kBindAxis | kBindInverse);
        int value = SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(axis));
        if (std::abs(value) > 500 && (bind & kBindInverse))
            value = -value;
        return value;
    }
    return SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(bind)) ? 32767 : 0;
}

// Schmitt trigger: an analog input wavering near one threshold must not chatter the bit.
std::uint16_t padWord(SDL_Gamepad* pad, const ControllerBindings& map, std::uint16_t previous)
{
    std::uint16_t word = kNone;
    for (const Button& b : kButtons)
    {
        const int value = bindValue(pad, map.*b.pad);
        const bool wasPressed = (previous & b.bit) == 0;
        if (value > (wasPressed ? kReleaseLow : kPressHigh))
            word &= static_cast<std::uint16_t>(~b.bit);
    }
    return word;
}

std::uint16_t keyWord(const std::uint8_t* keys, const KeyboardBindings& map)
{
    std::uint16_t word = kNone;
    for (const Button& b : kButtons)
        if (keys[map.*b.key])
            word &= static_cast<std::uint16_t>(~b.bit);
    return word;
}

// Mouse buttons 1..5 and the wheel (slots 6, 7) clear the pad bits the config bound to them.
// The wheel latch is decayed once per frame elsewhere, so reading it here does not consume it.
std::uint16_t mouseWord()
{
    const auto& mask = bindings().mouseButtons;
    std::uint16_t word = kNone;
    const SDL_MouseButtonFlags down = SDL_GetMouseState(nullptr, nullptr);
    for (int b = 1; b <= 5; ++b)
        if ((down & mouse_button_bit(b)) && mask[b])
            word &= static_cast<std::uint16_t>(~mask[b]);
    if (wheel().upFrames > 0 && mask[6])
        word &= static_cast<std::uint16_t>(~mask[6]);
    if (wheel().downFrames > 0 && mask[7])
        word &= static_cast<std::uint16_t>(~mask[7]);
    return word;
}

// The move stick is read axis by axis by the game's own thresholds, so its round gate is stretched to the square.
StickZones moveZones() { return {g_PcConfig.padDeadzone, g_PcConfig.padOuterDeadzone, true}; }

StickZones lookZones() { return {g_PcConfig.padLookDeadzone, g_PcConfig.padOuterDeadzone, false}; }

std::uint8_t axisByte(int value) { return static_cast<std::uint8_t>(value / 256 + 128); }

void storeWord(PadPortBlock& block, std::uint16_t word) { block.buttons = word; }

std::uint16_t loadWord(const PadPortBlock& block) { return block.buttons; }

// Axis  of the block: right X, right Y, left X, left Y.
std::uint8_t& axis(PadPortBlock& block, int a)
{
    std::uint8_t* const axes[] = {&block.right_x, &block.right_y, &block.left_x, &block.left_y};
    return *axes[a];
}

void setAxes(PadPortBlock& block, std::uint8_t value)
{
    for (int a = 0; a < 4; ++a)
        axis(block, a) = value;
}

void setNeutral(PadPortBlock& block)
{
    storeWord(block, kNone);
    setAxes(block, 127);
}

} // namespace

PadSystem& PadSystem::instance()
{
    static PadSystem state;
    return state;
}

bool PadSystem::live(int slot) const
{
    return slots_[slot].gamepad && SDL_GamepadConnected(slots_[slot].gamepad);
}

// A Steam Controller can be plugged in yet hidden from SDL while the Steam client holds
// it. Say so, because the report that arrives is only "the controller does nothing".
void PadSystem::steamHint() const
{
    constexpr Uint16 kValve = 0x28DE, kWired = 0x1102, kDongle = 0x1142;
    SDL_hid_device_info* list = SDL_hid_enumerate(kValve, 0);
    bool hardware = false;
    for (SDL_hid_device_info* it = list; it; it = it->next)
        hardware = hardware || it->product_id == kWired || it->product_id == kDongle;
    SDL_hid_free_enumeration(list);
    if (!hardware)
        return;

    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    bool seen = false;
    for (int i = 0; ids && i < count; ++i)
        seen = seen || SDL_GetJoystickVendorForID(ids[i]) == kValve;
    SDL_free(ids);

    if (seen)
        SH_LOG_INFO(INPUT, "Steam Controller: hardware detected and opened by SDL");
    else
        SH_LOG_WARN(INPUT, "Steam Controller: hardware detected but SDL could not open it. The Steam "
                           "client is holding it (desktop configuration = mouse). Add the game to "
                           "Steam and launch it from there, or exit Steam first.");
}

// A joystick without an SDL mapping is not a gamepad to the game; name it once with the
// raw shape a mapping line is written from.
void PadSystem::logUnmapped() const
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    for (int i = 0; ids && i < count; ++i)
    {
        if (SDL_IsGamepad(ids[i]))
            continue;
        const char* name = SDL_GetJoystickNameForID(ids[i]);
        char guid[64];
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(ids[i]), guid, sizeof guid);
        SDL_Joystick* js = SDL_OpenJoystick(ids[i]);
        SH_LOG_WARN(INPUT, "'%s' (vid=%04x pid=%04x guid=%s) has no SDL mapping, so the game cannot "
                           "use it. Raw shape: buttons=%d axes=%d hats=%d. Add a line for that guid "
                           "to gamecontrollerdb.txt next to the exe.",
                    name ? name : "?", unsigned(SDL_GetJoystickVendorForID(ids[i])),
                    unsigned(SDL_GetJoystickProductForID(ids[i])), guid,
                    js ? SDL_GetNumJoystickButtons(js) : -1, js ? SDL_GetNumJoystickAxes(js) : -1,
                    js ? SDL_GetNumJoystickHats(js) : -1);
        if (js)
            SDL_CloseJoystick(js);
    }
    SDL_free(ids);
}

bool PadSystem::init()
{
    if (keys_)
        return true;

    // Hints are latched when the subsystem starts, so they come first. An environment
    // variable still wins over them, which keeps Steam Input overrides working.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
    // The raw Steam Controller driver fights the Steam client, so it is on only when
    // Steam did not launch us (Steam sets one of these for every title, shortcuts included).
    const bool underSteam = SDL_getenv("SteamAppId") || SDL_getenv("SteamGameId") ||
                            SDL_getenv("STEAM_COMPAT_APP_ID");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, underSteam ? "0" : "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_STEAMDECK", "1");
    SH_LOG_INFO(INPUT, "Steam Controller raw driver %s", underSteam ? "off (launched by Steam)" : "on");

    slots_ = {};
    keys_ = reinterpret_cast<const std::uint8_t*>(SDL_GetKeyboardState(nullptr));
    g_sdlKeyboardState = keys_;

    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC))
    {
        SH_LOG_ERROR(INPUT, "SDL gamepad subsystem failed: %s", SDL_GetError());
        return false;
    }

    // The community mapping database sits beside the exe. SDL announces only devices it can
    // map at init time, i.e. before this file is read, and never re-announces a device that
    // becomes mappable later, so the pads already plugged in are adopted explicitly below.
    const int added = SDL_AddGamepadMappingsFromFile("gamecontrollerdb.txt");
    if (added < 0)
        SH_LOG_WARN(INPUT, "gamecontrollerdb.txt not loaded (%s); only SDL's built-in mappings are "
                           "available", SDL_GetError());
    else
        SH_LOG_INFO(INPUT, "gamecontrollerdb.txt: %d mappings added", added);

    steamHint();
    logUnmapped();

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; ++i)
        adopt(ids[i]);
    SDL_free(ids);
    return true;
}

void PadSystem::open(SDL_JoystickID instance, int slot)
{
    Slot& s = slots_[slot];
    if (s.gamepad)
    {
        if (live(slot))
            return;
        close(slot);  // a stale handle must not block the slot forever
    }
    s.gamepad = SDL_OpenGamepad(instance);
    if (!s.gamepad)
        return;
    s.instance = SDL_GetJoystickID(SDL_GetGamepadJoystick(s.gamepad));
    s.latch[0] = s.latch[1] = kNone;
    SH_LOG_INFO(INPUT, "controller '%s' -> slot %d (instance %d)", SDL_GetGamepadName(s.gamepad), slot,
                int(s.instance));
}

void PadSystem::close(int slot)
{
    Slot& s = slots_[slot];
    if (s.gamepad)
        SDL_CloseGamepad(s.gamepad);
    s = Slot{};
}

// Config-pinned slots accept exactly their device index; unpinned slots take any newcomer.
void PadSystem::adopt(SDL_JoystickID instance)
{
    for (int i = 0; i < kPorts; ++i)
        if (slots_[i].gamepad && slots_[i].instance == instance)
            return;  // an ADDED event for a pad the startup scan already took

    int count = 0, index = -1;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    for (int i = 0; ids && i < count; ++i)
        if (ids[i] == instance)
            index = i;
    SDL_free(ids);

    for (int i = 0; i < kPorts; ++i)
    {
        if (slots_[i].gamepad)
            continue;
        const int pinned = bindings().controllerSlot[i];
        if (pinned != -1 && pinned != index)
            continue;
        open(instance, i);
        return;
    }
}

void PadSystem::onDeviceAdded(SDL_JoystickID instance)
{
    steamHint();
    logUnmapped();
    for (int i = 0; i < kPorts; ++i)  // free handles that went stale
        if (slots_[i].gamepad && !live(i))
            close(i);
    adopt(instance);
}

// REMOVED events carry the joystick instance id, not a device index.
void PadSystem::onDeviceRemoved(SDL_JoystickID instance)
{
    for (int i = 0; i < kPorts; ++i)
    {
        if (slots_[i].gamepad && slots_[i].instance == instance)
        {
            SH_LOG_INFO(INPUT, "controller in slot %d disconnected", i);
            close(i);
        }
    }
}

void PadSystem::attach(int port, PadPortBlock* block)
{
    if (port < 0 || port >= kPorts)
        return;
    blocks_[port] = block;
    if (!block)
        return;
    if (block->kind != 0x41 && block->kind != 0x73)
        block->kind = port == 0 ? 0x41 : 0xFF;  // the keyboard is always there for port 0
    storeWord(*block, kNone);
    setAxes(*block, 128);
}

bool PadSystem::readPad(int slot, std::uint16_t& word, short axes[4])
{
    Slot& s = slots_[slot];
    const Bindings& binds = bindings();
    const std::uint16_t primary = padWord(s.gamepad, binds.controller, s.latch[0]);
    const std::uint16_t secondary = padWord(s.gamepad, binds.controllerSecondary, s.latch[1]);
    s.latch[0] = primary;
    s.latch[1] = secondary;

    // Either bind set presses; the d-pad can be taken out of movement while keyboard arrows,
    // which have their own word, and d-pad action binds (raw reads) keep working.
    word = primary & secondary;
    if (binds.dpadMovementDisabled)
        word |= kDpadBits;

    const ControllerBindings& m = binds.controller;
    const int rx = bindValue(s.gamepad, m.axis_right_x);
    const int ry = bindValue(s.gamepad, m.axis_right_y);
    const int lx = bindValue(s.gamepad, m.axis_left_x);
    const int ly = bindValue(s.gamepad, m.axis_left_y);
    const StickAxes look = shape_stick(rx, ry, lookZones());
    const StickAxes move = shape_stick(lx, ly, moveZones());
    axes[0] = static_cast<short>(look.x);
    axes[1] = static_cast<short>(look.y);
    axes[2] = static_cast<short>(move.x);
    axes[3] = static_cast<short>(move.y);

    return word != kNone || std::abs(rx) > kActiveStick || std::abs(ry) > kActiveStick || std::abs(lx) > kActiveStick ||
           std::abs(ly) > kActiveStick;
}

// Single player: every connected pad drives port 0. A bit is pressed if any pad presses it,
// and each axis takes the pad pushed furthest from centre, so an idle second pad never
// fights the one in use.
bool PadSystem::mergePads(PadPortBlock& block)
{
    std::uint16_t buttons = kNone;
    short best[4] = {0, 0, 0, 0};
    bool any = false;

    for (int i = 0; i < kPorts; ++i)
    {
        if (!live(i))
            continue;
        any = true;
        std::uint16_t word;
        short axes[4];
        const bool inUse = readPad(i, word, axes);
        buttons &= word;
        for (int a = 0; a < 4; ++a)
            if (std::abs(axes[a]) > std::abs(best[a]))
                best[a] = axes[a];
        if (inUse)
            activeSlot_ = i;  // rumble follows the pad that is being used
    }

    if (!any)
    {
        setNeutral(block);
        return false;
    }
    storeWord(block, buttons);
    for (int a = 0; a < 4; ++a)
        axis(block, a) = axisByte(best[a]);
    return true;
}

std::uint16_t PadSystem::keyboardWord()
{
    if (!keys_)
        return kNone;
    SDL_PumpEvents();

    const Bindings& binds = bindings();
    std::uint16_t word = keyWord(keys_, binds.keyboard);
    if (binds.mouseAndSecondary)
    {
        word &= keyWord(keys_, binds.keyboardSecondary);
        word &= mouseWord();
    }
    if (actionSource_)
        word &= actionSource_();
    return word;
}

void PadSystem::update()
{
    if (!updatesEnabled_)
        return;

    // The touch screen first: its controls hold actions the keyboard word below reads.
    const TouchStick touch = touchSource_ ? touchSource_() : TouchStick{};
    const std::uint16_t keyboard = keyboardWord();
    for (int port = 0; port < kPorts; ++port)
    {
        PadPortBlock* block = blocks_[port];
        if (!block)
            continue;

        bool attached;
        if (port == 0)
        {
            attached = mergePads(*block);
            int target = activeSlot_;
            if (target < 0 || target >= kPorts || !live(target))
                target = 0;
            if (rumbleTable_[0] && rumbleLength_[0] > 0 && live(target))
                rumble(target, rumbleTable_[0], rumbleLength_[0]);
            phoneRumble(target);
        }
        else
        {
            attached = live(port);
            if (attached)
            {
                std::uint16_t word;
                short axes[4];
                readPad(port, word, axes);
                storeWord(*block, word);
                for (int a = 0; a < 4; ++a)
                    axis(*block, a) = axisByte(axes[a]);
            }
            else
                setNeutral(*block);
            if (rumbleTable_[port] && rumbleLength_[port] > 0 && attached)
                rumble(port, rumbleTable_[port], rumbleLength_[port]);
        }

        // Analog mode follows the config instead of the original Select+Start toggle.
        if (attached)
            block->kind = bindings().movement == Movement::Dpad ? 0x41 : 0x73;

        const bool touchStick = port == 0 && touch.active;
        if (touchStick)
        {
            block->status = 0;
            block->kind = 0x73;
            block->left_x = touch.x;
            block->left_y = touch.y;
        }
        else if (port == 0 && touchStickWas_ && !attached)
            block->kind = 0x41;
        if (port == 0)
            touchStickWas_ = touchStick;

        if ((bindings().keyboardPorts & (1 << port)) && keyboard != kNone)
        {
            block->status = 0;
            block->kind = touchStick ? 0x73 : 0x41;  // the keyboard is digital only; the touch stick is not
            storeWord(*block, loadWord(*block) & keyboard);
        }
    }
}

bool PadSystem::connected(int port) const
{
    if (port == 0)
        return true;
    return port > 0 && port < kPorts && live(port);
}

void PadSystem::setRumbleBuffer(int port, std::uint8_t* table, int length)
{
    if (port < 0 || port >= kPorts)
        return;
    rumbleTable_[port] = table;
    rumbleLength_[port] = table ? length : 0;
    if (rumbleLength_[port] == 0 && slots_[port].gamepad)
        SDL_RumbleGamepad(slots_[port].gamepad, 0, 0, 0);
}

void PadSystem::rumble(int pad, const std::uint8_t* table, int length)
{
    if (length <= 0 || pad < 0 || pad >= kPorts)
        return;
    Uint16 high = static_cast<Uint16>(table[0] * 255);
    Uint16 low = length > 1 ? static_cast<Uint16>(table[1] * 255) : 0;
    // Below this a motor does not visibly turn.
    if (low != 0 && low < 4096)
        low = 4096;
    if (high != 0 && high < 4096)
        high = 4096;
    if (slots_[pad].gamepad)
        SDL_RumbleGamepad(slots_[pad].gamepad, low, high, 200);
}

#if defined(SDL_PLATFORM_ANDROID)
namespace {

// SDL's Java side lists the phone's own motor as a haptic device of this name (SDLHapticHandler.pollHapticDevices).
SDL_Haptic* openPhoneVibrator()
{
    int count = 0;
    SDL_HapticID* ids = SDL_GetHaptics(&count);
    SDL_Haptic* found = nullptr;
    for (int i = 0; ids && i < count && !found; ++i)
    {
        const char* name = SDL_GetHapticNameForID(ids[i]);
        if (!name || std::strcmp(name, "VIBRATOR_SERVICE") != 0)
            continue;
        found = SDL_OpenHaptic(ids[i]);
        if (found && !SDL_InitHapticRumble(found))
        {
            SH_LOG_WARN(INPUT, "the phone's vibrator does not rumble: %s", SDL_GetError());
            SDL_CloseHaptic(found);
            found = nullptr;
        }
    }
    SDL_free(ids);
    if (found)
        SH_LOG_INFO(INPUT, "rumble on the phone's vibrator while no pad with motors is in use");
    else
        SH_LOG_INFO(INPUT, "no phone vibrator; the rumble plays only on pads");
    return found;
}

} // namespace
#endif

void PadSystem::phoneRumble(int target)
{
#if defined(SDL_PLATFORM_ANDROID)
    bool padMotors = false;
    if (live(target))
        padMotors = SDL_GetBooleanProperty(SDL_GetGamepadProperties(slots_[target].gamepad),
                                           SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
    // The buffer is empty while the game's Vibration option is off or the vibration is paused.
    const float amplitude = padMotors ? 0.0f
                                      : phone_amplitude(rumbleTable_[0], rumbleLength_[0], g_PcConfig.phoneVibration);
    if (!phoneVibrator_)
    {
        if (amplitude <= 0.0f || phoneVibratorLooked_)
            return;
        phoneVibratorLooked_ = true;
        phoneVibrator_ = openPhoneVibrator();
        if (!phoneVibrator_)
            return;
    }
    const PhoneVibration v = phoneRumble_.frame(amplitude, static_cast<std::uint32_t>(SDL_GetTicks()));
    if (v.kind == PhoneVibration::Kind::Play)
        SDL_PlayHapticRumble(phoneVibrator_, v.amplitude, v.length_ms);
    else if (v.kind == PhoneVibration::Kind::Stop)
        SDL_StopHapticRumble(phoneVibrator_);
#else
    (void)target;
#endif
}

// The skip action of blocking loops (FMV) follows the configured Action and Start binds.
bool PadSystem::skipHeld() const
{
    if (touchSkip_ && touchSkip_())
        return true;
    for (int i = 0; i < kPorts; ++i)
    {
        if (!slots_[i].gamepad)
            continue;
        SDL_Gamepad* g = slots_[i].gamepad;
        const Bindings& binds = bindings();
        if (bindValue(g, binds.controller.cross) > kHalfScale ||
            bindValue(g, binds.controllerSecondary.cross) > kHalfScale ||
            bindValue(g, binds.controller.start) > kHalfScale)
            return true;
    }
    return false;
}

// Read straight from SDL, not from the keyboard-merged word, so a keyboard key on the same
// pad button cannot trigger a controller-only action.
bool PadSystem::buttonHeld(int sdlButton) const
{
    if (sdlButton < 0)
        return false;
    for (int i = 0; i < kPorts; ++i)
        if (live(i) && SDL_GetGamepadButton(slots_[i].gamepad, static_cast<SDL_GamepadButton>(sdlButton)))
            return true;
    return false;
}

// Same for a bind that may be a trigger or stick axis.
bool PadSystem::bindHeld(int buttonOrAxis) const
{
    if (buttonOrAxis < 0)
        return false;
    for (int i = 0; i < kPorts; ++i)
        if (live(i) && std::abs(bindValue(slots_[i].gamepad, buttonOrAxis)) > kHalfScale)
            return true;
    return false;
}

const char* PadSystem::connectedName() const
{
    for (int i = 0; i < kPorts; ++i)
        if (live(i))
            return SDL_GetGamepadName(slots_[i].gamepad);
    return nullptr;
}

// Bind name of a button or trigger held on any pad, limited to the names a pad bind in config.cfg
// takes; sticks are movement and never returned.
const char* PadSystem::heldBindName() const
{
    for (int i = 0; i < kPorts; ++i)
    {
        if (!live(i))
            continue;
        SDL_Gamepad* g = slots_[i].gamepad;
        for (int b = SDL_GAMEPAD_BUTTON_SOUTH; b <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT; ++b)
            if (SDL_GetGamepadButton(g, static_cast<SDL_GamepadButton>(b)))
                return SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(b));
        for (SDL_GamepadAxis trigger : {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER})
            if (SDL_GetGamepadAxis(g, trigger) > kHalfScale)
                return SDL_GetGamepadStringForAxis(trigger);
    }
    return nullptr;
}

// Every bind name held on any pad, buttons in SDL's order and then the triggers, each name once.
void PadSystem::heldBindNames(std::vector<std::string>& out) const
{
    out.clear();
    const auto add = [&out](const char* name) {
        if (name != nullptr && std::find(out.begin(), out.end(), name) == out.end())
            out.emplace_back(name);
    };
    for (int i = 0; i < kPorts; ++i)
    {
        if (!live(i))
            continue;
        SDL_Gamepad* g = slots_[i].gamepad;
        for (int b = SDL_GAMEPAD_BUTTON_SOUTH; b <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT; ++b)
            if (SDL_GetGamepadButton(g, static_cast<SDL_GamepadButton>(b)))
                add(SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(b)));
        for (SDL_GamepadAxis trigger : {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER})
            if (SDL_GetGamepadAxis(g, trigger) > kHalfScale)
                add(SDL_GetGamepadStringForAxis(trigger));
    }
}

// Signed axis of the pad pushed furthest.
int PadSystem::axisValue(int sdlAxis) const
{
    int best = 0;
    for (int i = 0; i < kPorts; ++i)
    {
        if (!live(i))
            continue;
        const int v = SDL_GetGamepadAxis(slots_[i].gamepad, static_cast<SDL_GamepadAxis>(sdlAxis));
        if (std::abs(v) > std::abs(best))
            best = v;
    }
    return best;
}

bool init_pads() { return PadSystem::instance().init(); }
void on_pad_added(std::uint32_t instance) { PadSystem::instance().onDeviceAdded(SDL_JoystickID(instance)); }
void on_pad_removed(std::uint32_t instance) { PadSystem::instance().onDeviceRemoved(SDL_JoystickID(instance)); }
void update_pads() { PadSystem::instance().update(); }
void set_keyboard_action_source(KeyboardActionSource source) { PadSystem::instance().setActionSource(source); }
void set_touch_source(TouchSource source) { PadSystem::instance().setTouchSource(source); }
void set_touch_skip(TouchSkip skip) { PadSystem::instance().setTouchSkip(skip); }

bool skip_held() { return PadSystem::instance().skipHeld(); }
bool controller_button_held(int sdlButton) { return PadSystem::instance().buttonHeld(sdlButton); }
bool controller_bind_held(int bind) { return PadSystem::instance().bindHeld(bind); }
const char* connected_controller_name() { return PadSystem::instance().connectedName(); }
const char* held_bind_name() { return PadSystem::instance().heldBindName(); }
void held_bind_names(std::vector<std::string>& out) { PadSystem::instance().heldBindNames(out); }
int controller_axis(int sdlAxis) { return PadSystem::instance().axisValue(sdlAxis); }

} // namespace sh::input
