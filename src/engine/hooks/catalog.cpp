#include "engine/hooks/catalog.h"

#include <array>

namespace sh::hooks {

namespace {

constexpr FieldInfo kNewGame[] = {{"difficulty", "string", false, "easy, normal or hard"}};
constexpr FieldInfo kSlot[] = {
    {"slot", "int", false, "save number (0 when a save writes a new entry); the quick slot (1..) of a quick save"},
    {"quick", "bool", false, "an instant quick save (quick_save_instant), not a save of the list"},
};
constexpr FieldInfo kTick[] = {{"tick", "int", false, "logic ticks since boot"}};
constexpr FieldInfo kMap[] = {{"map", "string", false, "map name, e.g. map2_s00"}};
constexpr FieldInfo kQuickKey[] = {
    {"map", "string", false, "map name, e.g. map2_s00"},
    {"instant", "bool", false, "quick_save_instant: the key saves or loads the quick slot at once, no screen"},
};
constexpr FieldInfo kRoom[] = {
    {"map", "string", false, "map name"},
    {"room", "int", false, "room index of the map's room grid"},
    {"prev_room", "int", false, "room before, -1 for the first room after a map load"},
};
constexpr FieldInfo kPickup[] = {
    {"item", "string", false, "item id, e.g. HandgunBullets"},
    {"count", "int", true, "how many are added"},
    {"flag", "string", false, "pickup flag name, \"\" when none"},
    {"map", "string", false, "map name"},
    {"x", "int", false, "Q12, the pickup's map point"},
    {"y", "int", false, "Q12, Harry's height"},
    {"z", "int", false, "Q12, the pickup's map point"},
};
constexpr FieldInfo kPicked[] = {
    {"item", "string", false, "item id"},
    {"count", "int", false, "how many were added"},
    {"flag", "string", false, "pickup flag name, \"\" when none"},
    {"map", "string", false, "map name"},
};
constexpr FieldInfo kItem[] = {{"item", "string", false, "item id"}};
constexpr FieldInfo kGiven[] = {
    {"item", "string", false, "item id"},
    {"count", "int", true, "how many are added (0..255)"},
    {"map", "string", false, "map name"},
    {"source", "string", false, "event (a story event or cutscene), console, script or debug (debug keys)"},
};
constexpr FieldInfo kQuickHeal[] = {
    {"item", "string", false, "the healing item about to be spent"},
    {"amount", "int", true, "Q12 health it restores (409600 = 100)"},
    {"health", "int", false, "Q12, Harry's health before the heal"},
};
constexpr FieldInfo kDamage[] = {
    {"amount", "int", true, "Q12 hit strength (409600 = 100 health), before the difficulty scaling"},
    {"source", "string", false, "character id of the attacker, \"\" when none"},
    {"source_slot", "int", false, "the attacker's entity slot, -1 when none"},
};
constexpr FieldInfo kRadioStatic[] = {
    {"map", "string", false, "map name"},
    {"heard", "int", false, "monsters in the radio's reach this tick (the three nearest at most)"},
    {"nearest_slot", "int", false, "entity slot of the nearest one, -1 when none"},
    {"nearest", "int", false, "Q12 distance of the nearest one, -1 when none"},
};
constexpr FieldInfo kDeath[] = {{"map", "string", false, "map name"}};
constexpr FieldInfo kSpawn[] = {
    {"chara", "string", false, "character id, e.g. Groaner"},
    {"slot", "int", false, "entity slot (1..)"},
    {"x", "int", false, "Q12"},
    {"z", "int", false, "Q12"},
};
constexpr FieldInfo kEntityDeath[] = {
    {"chara", "string", false, "character id"},
    {"slot", "int", false, "entity slot (1..)"},
};
constexpr FieldInfo kFlag[] = {
    {"flag", "string", false, "flag id"},
    {"value", "bool", false, "true set, false cleared"},
};
constexpr FieldInfo kMenu[] = {{"menu", "string", false, "menu id (options, options.mods...) or inventory, map, save"}};
constexpr FieldInfo kCutscene[] = {{"id", "string", false, "cutscene name (its DMS file, e.g. CAFE)"}};
constexpr FieldInfo kAction[] = {{"action", "string", false, "input action name, e.g. interact"}};
constexpr FieldInfo kMapEvent[] = {
    {"map", "string", false, "map name"},
    {"handler", "string", false, "readable handler name, \"\" when the map has none for it"},
    {"index", "int", false, "the handler's index in the map's table"},
};

constexpr std::array kEvents = {
    EventInfo{"game.boot", false, std::span<const FieldInfo>{}, "the game and mod scripts were loaded"},
    EventInfo{"game.new_game", false, kNewGame, "a new game starts, before its first map loads"},
    EventInfo{"game.load", false, kSlot, "a save was read, before its map loads"},
    EventInfo{"game.save", false, kSlot, "before a save is written"},
    EventInfo{"game.quick_save", true, kQuickKey,
              "the quick save key opens the save screen, or saves at once (instant); cancel does neither"},
    EventInfo{"game.quick_load", true, kQuickKey,
              "the quick load key opens the load screen, or loads the quick save (instant); cancel does neither"},
    EventInfo{"game.tick", false, kTick, "every 30 Hz logic tick while in game"},
    EventInfo{"map.load", false, kMap, "a map was loaded"},
    EventInfo{"map.unload", false, kMap, "the map is left, right before the next one loads"},
    EventInfo{"map.room_enter", false, kRoom, "Harry entered another room"},
    EventInfo{"item.pickup", true, kPickup, "Harry starts picking an item up; cancel leaves it lying"},
    EventInfo{"item.picked", false, kPicked, "an item was added to the inventory"},
    EventInfo{"item.use", true, kItem, "an item is used from the inventory; cancel does nothing"},
    EventInfo{"item.given", true, kGiven,
              "an item is added without a pickup (story event, console, script, debug keys); cancel adds "
              "nothing"},
    EventInfo{"player.damage", true, kDamage, "Harry loses health; cancel keeps it"},
    EventInfo{"player.quick_heal", true, kQuickHeal, "the quick heal key is about to spend an item; cancel spends nothing"},
    EventInfo{"player.death", false, kDeath, "Harry died, the game over starts"},
    EventInfo{"entity.spawn", false, kSpawn, "a character appeared in a slot"},
    EventInfo{"entity.death", false, kEntityDeath, "a character's health fell to zero"},
    EventInfo{"flag.set", false, kFlag, "an event flag changed"},
    EventInfo{"menu.open", false, kMenu, "a menu or a game screen opened"},
    EventInfo{"cutscene.start", false, kCutscene, "a cutscene started"},
    EventInfo{"cutscene.end", false, kCutscene, "control is back after a cutscene"},
    EventInfo{"input.action", true, kAction, "an input action was pressed; cancel swallows the press"},
    EventInfo{"map.event", true, kMapEvent, "a map event handler is about to run; cancel skips it"},
    EventInfo{"radio.static", true, kRadioStatic,
              "every tick the radio is switched on, before its noise sounds; cancel keeps the radio's own noise "
              "silent this tick (a script sounds its own)"},
};

}  // namespace

std::span<const EventInfo> catalog() { return kEvents; }

const EventInfo* find_event(std::string_view name) {
    for (const EventInfo& e : kEvents) {
        if (e.name == name) {
            return &e;
        }
    }
    return nullptr;
}

}  // namespace sh::hooks
