// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Runtime bridge for the semantic input actions. See pc_actions.h.

#include "input/pc_actions.h"

#include "engine/config/pc_config.h"
#include "engine/input/input_actions.h"
#include "engine/log/sh_log.h"
#include "engine/hooks/hook_bus.h"
#include "input/pad.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

using namespace sh::input;
using sh::Result;

namespace {

constexpr int kField = 24;
static_assert(kNameMax + 1 == kField);

char s_fields[kSchemes][kActionCount][kSlots][kField];
bool s_seen[kSchemes][kActionCount];
int s_scheme = 0;

// Resolved binds: a scancode, or a mouse source (1..5 buttons, 6/7 wheel).
struct Source {
    int scancode = SDL_SCANCODE_UNKNOWN;
    int mouse = 0;
};
Source s_sources[kSchemes][kActionCount][kSlots];
int s_resolvedGen = -1;
int s_dirtyGen = 0;

// Per action, the bits latched when it was pressed (kept until release).
std::uint16_t s_kbLatch[kActionCount];
std::uint16_t s_mouseLatch[kActionCount];
std::uint16_t s_tapLatch[kActionCount];
std::uint16_t s_tapBits = 0;
bool s_kbHeld[kActionCount];
bool s_mouseHeld[kActionCount];
// The touch screen's controls count as keys: their own latch, so a key and a finger on one action do not fight.
std::uint16_t s_touchLatch[kActionCount];
bool s_touchHeld[kActionCount];
unsigned int s_touchActions = 0;
int s_suspended = 0;  // PcActions_SetSuspended
static_assert(kActionCount <= 32, "touch actions are one bit each");

// Back and Pause give the same pad bit in gameplay; the pause menu needs to know which one
// paused. SDL ticks of the last Back press that became the pause bit, 0 after a Pause press.
Uint64 s_backPauseTicks = 0;
constexpr Uint64 kBackPauseWindowMs = 500;

int storage_scheme(Action a, int scheme) { return (info(a).flags & kFlagGlobal) ? 0 : (scheme != 0 ? 1 : 0); }

void set_binding(Action a, int scheme, const Binding& b) {
    const int s = storage_scheme(a, scheme);
    for (int slot = 0; slot < kSlots; ++slot) {
        std::snprintf(s_fields[s][static_cast<int>(a)][slot], kField, "%s", b[static_cast<std::size_t>(slot)].c_str());
    }
    ++s_dirtyGen;
}

Binding get_binding(Action a, int scheme) {
    const int s = storage_scheme(a, scheme);
    return {s_fields[s][static_cast<int>(a)][0], s_fields[s][static_cast<int>(a)][1]};
}

// "action_<name>" -> slot 0, "action_<name>_2" -> slot 1.
bool parse_slot_key(const char* key, Action* out, int* slot) {
    if (key == nullptr) {
        return false;
    }
    std::string_view k(key);
    if (!k.starts_with("action_")) {
        return false;
    }
    k.remove_prefix(7);
    int s = 0;
    if (k.ends_with("_2")) {
        k.remove_suffix(2);
        s = 1;
    }
    const std::optional<Action> a = find_action(k);
    if (!a || (s == 1 && (info(*a).flags & kFlagSingleSlot))) {
        return false;
    }
    *out = *a;
    *slot = s;
    return true;
}

int parse_mouse(const char* v) {
    if (SDL_strcasecmp(v, "MouseWheelUp") == 0) return 6;
    if (SDL_strcasecmp(v, "MouseWheelDown") == 0) return 7;
    if (SDL_strncasecmp(v, "Mouse", 5) == 0 && v[5] >= '1' && v[5] <= '5' && v[6] == '\0') return v[5] - '0';
    return 0;
}

void resolve() {
    const int gen = s_dirtyGen + g_PcBindsGen * 7919;
    if (gen == s_resolvedGen) {
        return;
    }
    s_resolvedGen = gen;
    for (int s = 0; s < kSchemes; ++s) {
        for (int a = 0; a < kActionCount; ++a) {
            for (int slot = 0; slot < kSlots; ++slot) {
                const char* v = s_fields[storage_scheme(static_cast<Action>(a), s)][a][slot];
                Source src;
                if (!is_unbound(v)) {
                    src.mouse = parse_mouse(v);
                    if (src.mouse == 0) {
                        src.scancode = lookup_key(v, SDL_SCANCODE_UNKNOWN);
                    }
                }
                s_sources[s][a][slot] = src;
            }
        }
    }
}

bool mouse_down(int m, Uint32 buttons) {
    if (m >= 1 && m <= 5) return (buttons & SDL_BUTTON_MASK(m)) != 0;
    if (m == 6) return wheel().upFrames > 0;
    if (m == 7) return wheel().downFrames > 0;
    return false;
}

PadConfig query(Context* ctx) {
    unsigned short raw[13];
    *ctx = static_cast<Context>(PcActions_CtxQuery(raw));
    bool any = false;
    for (unsigned short v : raw) any |= v != 0;
    if (!any) {
        return default_pad_config();  // before the game has loaded its settings
    }
    PadConfig c;
    c.enter = raw[0], c.cancel = raw[1], c.skip = raw[2], c.action = raw[3], c.aim = raw[4], c.light = raw[5];
    c.run = raw[6], c.view = raw[7], c.step_left = raw[8], c.step_right = raw[9], c.pause = raw[10];
    c.item = raw[11], c.map = raw[12];
    return c;
}

std::string legacy_value(std::string_view key, int scheme, bool defaults) {
    const std::string k(key);
    const char* v = defaults ? PcConfig_BindDefault(k.c_str(), scheme) : PcConfig_BindField(k.c_str(), scheme, nullptr);
    return v ? std::string(v) : std::string();
}

void save_action(Action a, int scheme) {
    const std::string key = config_key(a, scheme);
    const std::string value = format_binding(get_binding(a, scheme));
    PcConfig_SaveKeyValue(key.c_str(), value.c_str());
}

}  // namespace

extern "C" {

void PcActions_ResetDefaults(void) {
    for (int s = 0; s < kSchemes; ++s) {
        for (int a = 0; a < kActionCount; ++a) {
            set_binding(static_cast<Action>(a), s, default_binding(static_cast<Action>(a), s));
            s_seen[s][a] = false;
        }
    }
    set_keyboard_action_source(PcActions_PadWord);
}

int PcActions_ParseConfigLine(const char* key, const char* value) {
    const std::optional<KeyRef> ref = parse_config_key(key ? key : "");
    if (!ref) {
        return 0;
    }
    Result<Binding> b = parse_binding(value ? value : "");
    if (!b) {
        SH_LOG_WARN(CONFIG, "%s: %s, default kept", key, b.error().c_str());
        return 1;
    }
    set_binding(ref->action, ref->scheme, b.value());
    s_seen[storage_scheme(ref->action, ref->scheme)][static_cast<int>(ref->action)] = true;
    return 1;
}

void PcActions_FinishLoad(int migrateLegacy) {
    if (migrateLegacy) {
        int moved = 0;
        for (int s = 0; s < kSchemes; ++s) {
            for (int i = 0; i < kActionCount; ++i) {
                const Action a = static_cast<Action>(i);
                if (storage_scheme(a, s) != s || s_seen[s][i]) {
                    continue;
                }
                const auto cur = [s](std::string_view k) { return legacy_value(k, s, false); };
                const auto def = [s](std::string_view k) { return legacy_value(k, s, true); };
                const std::optional<Binding> b = migrate_legacy(a, cur, def);
                if (!b) {
                    continue;
                }
                set_binding(a, s, *b);
                save_action(a, s);
                ++moved;
                SH_LOG_INFO(CONFIG, "v8: %s = %s (from the PS1-button binds)", config_key(a, s).c_str(),
                            format_binding(*b).c_str());
            }
        }
        SH_LOG_INFO(CONFIG, "v8: keyboard binds are actions now, %d carried over", moved);
    }
    PcActions_Project();
}

void PcActions_Project(void) {
    for (int s = 0; s < kSchemes; ++s) {
        for (int i = 0; i < kActionCount; ++i) {
            const Action a = static_cast<Action>(i);
            const ActionInfo& ai = info(a);
            if (!(ai.flags & kFlagPcOnly) || storage_scheme(a, s) != s) {
                continue;
            }
            const std::array<std::string_view, kSlots> keys = {ai.legacy_key, ai.legacy_key2};
            for (int slot = 0; slot < kSlots; ++slot) {
                if (keys[static_cast<std::size_t>(slot)].empty()) {
                    continue;
                }
                const std::string k(keys[static_cast<std::size_t>(slot)]);
                char* field = PcConfig_BindField(k.c_str(), s, nullptr);
                if (field != nullptr) {
                    std::snprintf(field, kField, "%s", s_fields[s][i][slot]);
                }
            }
        }
    }
}

char* PcActions_BindField(const char* key, int scheme, int* outPerScheme) {
    Action a;
    int slot;
    if (outPerScheme) *outPerScheme = 0;
    if (!parse_slot_key(key, &a, &slot)) {
        return nullptr;
    }
    if (outPerScheme) *outPerScheme = (info(a).flags & kFlagGlobal) ? 0 : 1;
    return s_fields[storage_scheme(a, scheme)][static_cast<int>(a)][slot];
}

const char* PcActions_BindDefault(const char* key, int scheme) {
    Action a;
    int slot;
    if (!parse_slot_key(key, &a, &slot)) {
        return "";
    }
    const ActionInfo& ai = info(a);
    const int s = (ai.flags & kFlagGlobal) ? 0 : (scheme != 0 ? 1 : 0);
    return ai.defaults[static_cast<std::size_t>(s)][static_cast<std::size_t>(slot)].data();
}

int PcActions_ConfigLine(const char* key, int scheme, char* outKey, int keyCap, char* outValue, int valueCap) {
    Action a;
    int slot;
    if (!parse_slot_key(key, &a, &slot)) {
        return 0;
    }
    std::snprintf(outKey, static_cast<std::size_t>(keyCap), "%s", config_key(a, scheme).c_str());
    std::snprintf(outValue, static_cast<std::size_t>(valueCap), "%s", format_binding(get_binding(a, scheme)).c_str());
    return 1;
}

int PcActions_Save(const char* key, int scheme) {
    Action a;
    int slot;
    if (!parse_slot_key(key, &a, &slot)) {
        return 0;
    }
    save_action(a, scheme);
    ++s_dirtyGen;
    PcActions_Project();
    return 1;
}

void PcActions_SetScheme(int scheme) { s_scheme = scheme != 0 ? 1 : 0; }

void PcActions_SetTouchActions(unsigned int held) { s_touchActions = held; }

void PcActions_SetSuspended(int devices) { s_suspended = devices; }

unsigned short PcActions_PadWord(void) {
    int nk = 0;
    const Uint8* ks = (const Uint8*)SDL_GetKeyboardState(&nk);
    const Uint32 mb = SDL_GetMouseState(nullptr, nullptr);
    Context ctx = Context::Menu;
    bool queried = false;
    PadConfig cfg;
    std::uint16_t bits = 0;
    std::uint16_t taps = 0;
    std::uint16_t solid = 0;  // the same bit held by an ordinary action stays a held button

    resolve();
    for (int i = 0; i < kActionCount; ++i) {
        const Action a = static_cast<Action>(i);
        if (info(a).flags & kFlagPcOnly) {
            continue;
        }
        bool kb = false, mouse = false;
        for (int slot = 0; slot < kSlots; ++slot) {
            const Source& src = s_sources[s_scheme][i][slot];
            if (src.mouse != 0) {
                mouse |= mouse_down(src.mouse, mb);
            } else if (ks != nullptr && src.scancode > 0 && src.scancode < nk) {
                kb |= ks[src.scancode] != 0;
            }
        }
        if (s_suspended & 1) kb = false;
        if (s_suspended & 2) mouse = false;
        const bool touch = (s_touchActions >> i) & 1u;
        if (touch && !s_touchHeld[i]) {
            if (!queried) {
                cfg = query(&ctx);
                queried = true;
            }
            s_touchLatch[i] = pad_bits(a, ctx, cfg);
            if (a == Action::Back && ctx == Context::Gameplay && (s_touchLatch[i] & cfg.pause) != 0) {
                s_backPauseTicks = SDL_GetTicks();
                if (s_backPauseTicks == 0) s_backPauseTicks = 1;
            }
        }
        s_touchHeld[i] = touch;
        if (!touch) s_touchLatch[i] = 0;
        bits |= s_touchLatch[i];
        solid |= s_touchLatch[i];
        if ((kb && !s_kbHeld[i]) || (mouse && !s_mouseHeld[i])) {
            if (!queried) {
                cfg = query(&ctx);
                queried = true;
            }
        }
        if (kb && !s_kbHeld[i]) {
            s_kbLatch[i] = pad_bits(a, ctx, cfg);
        }
        if (mouse && !s_mouseHeld[i]) {
            // Outside the world the mouse is the menu cursor.
            const bool world = ctx == Context::World || ctx == Context::Gameplay;
            s_mouseLatch[i] = world ? pad_bits(a, ctx, cfg) : 0;
        }
        if ((kb && !s_kbHeld[i]) || (mouse && !s_mouseHeld[i])) {
            s_tapLatch[i] = tap_bits(a, ctx, cfg);
        }
        if ((kb && !s_kbHeld[i]) || (mouse && !s_mouseHeld[i])) {
            // A cancelled press latches nothing, so the whole hold stays swallowed until the key is released.
            if (sh::hooks::bus().has("input.action")) {
                sh::hooks::Event ev("input.action", true);
                ev.set("action", std::string(info(a).name), true);
                if (!sh::hooks::bus().emit(ev)) {
                    if (kb && !s_kbHeld[i]) s_kbLatch[i] = 0;
                    if (mouse && !s_mouseHeld[i]) s_mouseLatch[i] = 0;
                }
            }
            const std::uint16_t got = s_kbLatch[i] | s_mouseLatch[i];
            if (a == Action::Back && ctx == Context::Gameplay && (got & cfg.pause) != 0) {
                s_backPauseTicks = SDL_GetTicks();
                if (s_backPauseTicks == 0) s_backPauseTicks = 1;
            } else if (a == Action::Pause && (got & cfg.pause) != 0) {
                s_backPauseTicks = 0;
            }
        }
        s_kbHeld[i] = kb;
        s_mouseHeld[i] = mouse;
        if (!kb) s_kbLatch[i] = 0;
        if (!mouse) s_mouseLatch[i] = 0;
        if (!kb && !mouse) s_tapLatch[i] = 0;
        bits |= s_kbLatch[i] | s_mouseLatch[i];
        taps |= s_tapLatch[i] & (s_kbLatch[i] | s_mouseLatch[i]);
        solid |= (s_kbLatch[i] | s_mouseLatch[i]) & ~s_tapLatch[i];
    }
    s_tapBits = static_cast<std::uint16_t>(taps & ~solid);
    return static_cast<unsigned short>(~bits);
}

unsigned int PcActions_TapBits(void) { return s_tapBits; }

int PcActions_TakePauseFromBack(void) {
    const Uint64 at = s_backPauseTicks;
    s_backPauseTicks = 0;
    if (at == 0) {
        return 0;
    }
    const int back = static_cast<int>(Action::Back);
    // Still held, or a tap released before the game read the pause press.
    return ((s_kbLatch[back] | s_mouseLatch[back] | s_touchLatch[back]) != 0 || SDL_GetTicks() - at < kBackPauseWindowMs) ? 1 : 0;
}

unsigned int PcActions_MouseBits(void) {
    unsigned int bits = 0;
    for (int i = 0; i < kActionCount; ++i) {
        bits |= s_mouseLatch[i];
    }
    return bits;
}

int PcActions_Describe(int i, char* out, int cap) {
    if (i < 0 || i > kActionCount || cap <= 0) {
        return 0;
    }
    if (i == 0) {
        std::snprintf(out, static_cast<std::size_t>(cap), "input actions, %s scheme:", s_scheme ? "altcam" : "classic");
        return 1;
    }
    const Action a = static_cast<Action>(i - 1);
    const Binding b = get_binding(a, s_scheme);
    std::snprintf(out, static_cast<std::size_t>(cap), " %-14s %s%s%s", std::string(info(a).name).c_str(), b[0].c_str(),
                  is_unbound(b[1]) ? "" : ", ", is_unbound(b[1]) ? "" : b[1].c_str());
    return 1;
}

}  // extern "C"
