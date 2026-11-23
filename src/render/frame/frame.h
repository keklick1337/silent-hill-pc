// The engine frame (docs/RENDER.md section 4): the list of everything drawn in a video frame. Entries
// are references (kind + id into the 2D, 3D or scene stores) placed by an order key: the layer, the slot of the layer
// (a depth bucket the drawer derived itself) and the display buffer whose lists are being built. The frame owns the
// order: slots are drawn from the highest down to 0, entries of one slot latest first.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sh::frame {

struct Rect {
    std::int16_t x = 0, y = 0, w = 0, h = 0;
};

enum class Kind : std::uint8_t {
    Draw2D,   // id into sh::draw2d::frame_commands()
    Draw3D,   // id into sh::draw3d::frame_commands()
    Scene,    // scene-renderer item code: 0 the map cells, n + 1 the posed mesh n
    Capture,  // pack the frame drawn so far into the video-memory rect in Entry::rect, so later draws sample it
    Area,     // the drawing area changes to Entry::rect (corner, size)
};

enum class Layer : std::uint8_t {
    World,    // the 3D scene
    Overlay,  // the screen overlay drawn over the scene
    Inset,    // a layer of its own drawn inside world slot 0 once linked there (the pickup screen's item close-up)
};
inline constexpr int kLayerCount = 3;
inline constexpr int kBufferCount = 2;  // display buffers: the lists of one are built while the other is shown

// Slots of a layer (the depth resolution the game's drawers bucket into).
constexpr std::uint32_t slot_count(Layer layer) {
    return layer == Layer::World ? 2048u : layer == Layer::Overlay ? 16u : 512u;
}

struct OrderKey {
    Layer layer = Layer::World;
    std::uint32_t slot = 0;
};

struct Entry {
    Kind kind = Kind::Draw2D;
    std::uint32_t id = 0;
    std::uint64_t seq = 0;      // submission order, increasing for the whole run
    const void* key = nullptr;  // identity of the packet the drawer reserved for it (depth records are keyed by it)
    std::uint32_t gen = 0;      // frame generation of the submission
    Layer layer = Layer::World;
    std::uint8_t buffer = 0;
    std::uint32_t slot = 0;
    bool used = false;
    std::uint32_t used_gen = 0;  // frame generation that drew it
    // The layer's depth steps down one slot at this entry before it is drawn: a capture that ends an offscreen scene
    // stands where the game's area change was, and the original's table walk counts that blanked change as a slot.
    bool steps_down = false;
    Rect rect;  // Capture and Area entries
};

class Frame {
public:
    // Puts an entry at `key` in the lists of display buffer `buffer`. `packet` is only the identity of a packet the
    // drawer reserved (null otherwise).
    void submit(Kind kind, std::uint32_t id, OrderKey key, int buffer, const void* packet = nullptr, Rect rect = {});

    // The lists of a layer of `buffer` start again. Entries stay until drawn (a list built but not drawn in one frame
    // is drawn with the next build of the same buffer); the world layer forgets its link to the inset.
    void clear(Layer layer, int buffer);

    // From now on the inset of `buffer` is drawn inside world slot 0: after the world slot-0 entries submitted later
    // and before those submitted earlier.
    void link_inset(int buffer);

    // Visits the entries of a layer of `buffer` in drawing order and consumes them. `slot(k, inset)` is called as the
    // walk enters each slot (all of them, from the top down, including the inset's slots inside world slot 0 when it
    // is linked), `entry(e)` for each entry of the slot. Entries of slots past the layer are drawn in its top slot.
    template <class SlotFn, class EntryFn>
    void take_layer(Layer layer, int buffer, SlotFn&& slot, EntryFn&& entry) {
        plan(layer, buffer);
        for (const Step& s : steps_) {
            if (s.is_slot) {
                slot(s.slot, s.inset);
                continue;
            }
            Entry& e = entries_[s.index];
            e.used = true;
            e.used_gen = generation_;
            entry(const_cast<const Entry&>(e));
        }
    }

    // The same order without consuming; `entry(e)` may change the entry it is given.
    template <class EntryFn>
    void visit_layer(Layer layer, int buffer, EntryFn&& entry) {
        plan(layer, buffer);
        for (const Step& s : steps_)
            if (!s.is_slot) entry(entries_[s.index]);
    }

    // Entries waiting to be drawn.
    std::size_t pending() const;

    // A frame was presented: entries drawn before it and entries that nothing drew for several frames are dropped.
    // What this frame drew stays for one more frame, for redraw_last().
    void end_frame();

    // The entries the last presented frame drew wait to be drawn again: a frame presented between two game ticks
    // shows the same lists. Returns how many.
    std::size_t redraw_last();

    // The lists built and not drawn are dropped: an update that is not presented (one of several making up a long
    // frame) leaves nothing for the next one to draw.
    void discard_pending();

private:
    struct Step {
        bool is_slot = false;
        bool inset = false;
        std::uint32_t slot = 0;
        std::uint32_t index = 0;  // entry index (entries only)
    };
    static constexpr std::uint64_t kNotLinked = ~std::uint64_t{0};

    void plan(Layer layer, int buffer);
    void plan_slots(Layer layer, int buffer, bool inset, std::uint64_t below_seq);

    std::vector<Entry> entries_;
    std::vector<Step> steps_;
    std::vector<std::uint32_t> scratch_;
    std::uint64_t next_seq_ = 0;
    std::uint64_t inset_link_[kBufferCount] = {kNotLinked, kNotLinked};  // seq of the link, per buffer
    std::uint32_t generation_ = 0;
};

Frame& frame();

// The display buffer whose lists the game is building (the game's frame bridge installs where it reads it from).
using BufferSource = int (*)();
void set_buffer_source(BufferSource source);
int build_buffer();

// Places an entry at `key` in the lists being built. `packet` is only the identity of a packet the drawer
// reserved (null otherwise).
void submit_keyed(Kind kind, std::uint32_t id, OrderKey key, const void* packet = nullptr, Rect rect = {});

// Per-frame state the game sets around draws and the renderer reads.
struct State {
    float chara_fade = 0.f;         // 0..1 fade of the character being drawn; the flashlight must not relight it
    bool no_shadow_cast = false;    // the geometry being drawn does not cast a flashlight shadow (the player's body)
    std::uint32_t item_depth_max = 0;  // largest view depth submitted by a drawer this frame, for depth normalisation

    // Origin the game's draw environment puts on screen-space coordinates (the PS1 draw-env offset); zero while the
    // environment draws to an offscreen target. Set by the GPU layer whenever the environment changes.
    float draw_offset_x = 0.f;
    float draw_offset_y = 0.f;

    void set_draw_offset(bool enabled, int x, int y) {
        draw_offset_x = enabled ? static_cast<float>(x) : 0.f;
        draw_offset_y = enabled ? static_cast<float>(y) : 0.f;
    }

    void note_item_depth(std::uint32_t sz) {
        if (sz > item_depth_max) item_depth_max = sz;
    }
    // Returns the maximum and starts a new one.
    std::uint32_t take_item_depth() {
        const std::uint32_t v = item_depth_max;
        item_depth_max = 0;
        return v;
    }
};

State& state();

}  // namespace sh::frame
