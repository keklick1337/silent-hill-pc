#pragma once

// Map event triggers: once per gameplay frame the state machine
// asks which event of the map's event list runs next. Every event is gated by two story flags, has a
// trigger shape around one of the map's points (box, facing a point, facing a segment, oriented box,
// or always), and an activation kind (touch, exclusive, button, item). The first event whose gates,
// shape and activation pass wins; item events instead register an item slot that the inventory
// screen checks, and are run on the frame after the player used a matching item.
//
// Bit-exactness contract: with the same inputs the result and the state (item slots, cached facing
// probe) equal the original's to the last bit, including its integer overflow behaviour.

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace sh::gameflow {

// The states of the game state machine. Map events name the state they run in; kNone is the "no
// change" answer of the trigger scan.
enum class GameState : std::uint8_t {
    Gameplay = 0,
    OptionsMenu = 1,
    StatusMenu = 2,
    MapScreen = 3,
    Movie = 4,
    LoadMap = 5,
    LoadRoom = 6,
    ReadMessage = 7,
    SaveMenu = 8,
    SaveMenuAlt = 9,
    EventCallback = 10,
    EventSetFlag = 11,
    EventPlaySound = 12,
    GameOver = 13,
    Paused = 14,
    None = 15,
};

// How an event is triggered. Values are the event data's 4-bit signed field; values outside the
// named ones carry no shape test (the event passes as if touched).
enum class TriggerShape : std::int8_t {
    EndOfList = -1,
    Always = 0,         // no shape and no activation test: runs whenever the flags allow it
    Box = 1,            // player inside an axis-aligned box of half extents param0/4, param1/4 m (half_x_q2, half_z_q2)
    FacingPoint = 2,    // player near the point and facing it
    FacingSegment = 3,  // player's facing ray crosses a segment through the point
    OrientedBox = 4,    // player inside a box rotated by param0 (heading_q8), param1/16 m half wide (half_width_q4)
};

enum class Activation : std::uint8_t {
    Touch = 0,
    Exclusive = 1,  // runs once while it stays the running event
    Button = 2,     // needs the action button
    Item = 3,       // needs an item used from the inventory
};

// A map point as the triggers read it: XZ position (Q12) and the two 8-bit shape parameters
// (param0 is usually a Q8 angle, 256 per turn).
struct TriggerPoint {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::uint8_t param0 = 0;
    std::uint8_t param1 = 0;
};

struct EventTrigger {
    std::int16_t required_flag = 0;  // 0 = none; otherwise the flag must be set
    std::int16_t blocking_flag = 0;  // 0 = none; see EventRules
    TriggerShape shape = TriggerShape::Always;
    Activation activation = Activation::Touch;
    std::uint8_t point = 0;          // index into the map's points
    std::uint8_t item = 0;           // item id an Item event waits for
    std::uint8_t state = 0;          // GameState value the event runs in (5-bit field)
    std::uint8_t param = 0;          // state parameter (message, sound, callback index, ...)
};

// Per-game rules of the trigger scan (data of the game, supplied by the host).
struct EventRules {
    // A blocking flag for which this answers true (the paper map marks) only blocks exclusive and set-flag
    // events; any other blocks every event. Empty: every flag blocks every event.
    std::function<bool(std::int16_t flag)> soft_blocking;
};

// Map access and flag storage of the host.
class EventHost {
public:
    virtual ~EventHost() = default;
    virtual bool flag(std::int16_t flag) const = 0;
    virtual void set_flag(std::int16_t flag) = 0;
    // The map point an event refers to (the host owns the point list; indices are not range-checked
    // by the event data).
    virtual TriggerPoint point(std::uint8_t index) const = 0;
};

struct TriggerFrame {
    std::int32_t player_x = 0;        // Q12
    std::int32_t player_z = 0;        // Q12
    std::int32_t player_heading = 0;  // Q12 angle
    std::int32_t tick = 0;            // frame counter of the game; the facing probe is cached per tick
    std::int32_t used_item = 0;       // item used from the inventory on an item slot, 0 = none
    int current_event = -1;           // index of the running event, -1 = none
    bool buttons_disabled = false;    // button events are not considered this frame
    bool action_pressed = false;      // action button went down this frame
    bool player_busy = false;         // player cannot interact (aiming, hurt, dead, ...)
    // The scene is dark and the flashlight is off: button events other than room and map loads with
    // a parameter above 1 are skipped.
    bool dark_without_light = false;
    std::int32_t facing_reach = 0;    // Q12 reach of FacingPoint triggers
};

// What the scan decided. Fields left empty keep the host's previous value.
struct TriggerResult {
    bool has_event = false;          // `event` is written (-1 = no running event)
    int event = -1;
    int item_slot = -1;              // the event came from this item slot (item used this frame)
    bool has_state = false;
    std::uint8_t state = static_cast<std::uint8_t>(GameState::None);
    bool has_param = false;
    std::uint8_t param = 0;
    bool has_event_item = false;     // the item that started the running event (0 = none)
    std::int32_t event_item = 0;
    bool consume_used_item = false;  // the host clears its "item used" input
    std::int16_t flag_set = 0;       // a set-flag event fired and set this flag (0 = none)
};

class EventTriggers {
public:
    static constexpr int kItemSlots = 5;
    static constexpr int kMaxEvents = 256;

    struct ItemSlot {
        std::int32_t item = -1;  // -1 = free
        int event = -1;
        EventTrigger trigger{};  // the event as registered (the list may be gone when the item is used)
    };

    void set_rules(const EventRules& rules) { rules_ = rules; }

    // One scan. `events` is the map's list without the end marker (null data = no list loaded).
    TriggerResult update(const TriggerFrame& frame, std::span<const EventTrigger> events, bool has_events,
                         EventHost& host);

    const std::array<ItemSlot, kItemSlots>& item_slots() const { return slots_; }

    // The button event the action button would start in this frame's place, or -1: the gates, the shape and the
    // dark rule as update() reads them, for a hint of what the button would do (the touch screen's action button).
    // Changes nothing, the cached facing probe included; `frame.action_pressed` is not read.
    int button_event_in_reach(const TriggerFrame& frame, std::span<const EventTrigger> events,
                              const EventHost& host) const;

    // Number of item events that found no free slot (the original wrote past its array).
    int slot_overflows() const { return slot_overflows_; }

    // Shape tests, exposed for tests.
    bool facing_point(const TriggerFrame& frame, const TriggerPoint& p);
    static bool facing_segment(const TriggerFrame& frame, const TriggerPoint& p);
    static bool oriented_box(const TriggerFrame& frame, const TriggerPoint& p);

private:
    void clear_slots();

    EventRules rules_{};
    std::array<ItemSlot, kItemSlots> slots_{};
    int slot_overflows_ = 0;
    // Point 1/8 m behind the player, refreshed once per tick (the first tick it is refreshed on is
    // the first one after tick 0).
    std::int32_t probe_tick_ = 0;
    std::int32_t probe_x_ = 0;
    std::int32_t probe_z_ = 0;
};

}  // namespace sh::gameflow
