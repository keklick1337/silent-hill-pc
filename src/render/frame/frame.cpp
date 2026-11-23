#include "render/frame/frame.h"

#include <algorithm>

namespace sh::frame {

State& state() {
    static State s;
    return s;
}

namespace {

int clamp_buffer(int buffer) { return buffer < 0 || buffer >= kBufferCount ? 0 : buffer; }

}  // namespace

void Frame::submit(Kind kind, std::uint32_t id, OrderKey key, int buffer, const void* packet, Rect rect) {
    Entry e;
    e.kind = kind;
    e.id = id;
    e.seq = next_seq_++;
    e.key = packet;
    e.gen = generation_;
    e.layer = key.layer;
    e.buffer = static_cast<std::uint8_t>(clamp_buffer(buffer));
    const std::uint32_t top = slot_count(key.layer) - 1;
    e.slot = key.slot > top ? top : key.slot;
    e.rect = rect;
    entries_.push_back(e);
}

void Frame::clear(Layer layer, int buffer) {
    if (layer == Layer::World) inset_link_[clamp_buffer(buffer)] = kNotLinked;
}

void Frame::link_inset(int buffer) { inset_link_[clamp_buffer(buffer)] = next_seq_; }

std::size_t Frame::pending() const {
    return static_cast<std::size_t>(
        std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.used; }));
}

// Steps of one layer: every slot from the top down, each followed by its entries latest first. Inside world slot 0 a
// linked inset goes between the entries submitted after the link and those submitted before it.
void Frame::plan_slots(Layer layer, int buffer, bool inset, std::uint64_t link_seq) {
    std::vector<std::uint32_t> order;
    for (std::uint32_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        if (!e.used && e.layer == layer && e.buffer == buffer) order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [this](std::uint32_t a, std::uint32_t b) {
        const Entry& x = entries_[a];
        const Entry& y = entries_[b];
        return x.slot != y.slot ? x.slot > y.slot : x.seq > y.seq;
    });
    std::size_t i = 0;
    for (std::uint32_t s = slot_count(layer); s-- > 0;) {
        steps_.push_back(Step{true, inset, s, 0});
        if (s == 0 && link_seq != kNotLinked) {
            while (i < order.size() && entries_[order[i]].slot == 0 && entries_[order[i]].seq >= link_seq)
                steps_.push_back(Step{false, inset, 0, order[i++]});
            plan_slots(Layer::Inset, buffer, true, kNotLinked);
        }
        while (i < order.size() && entries_[order[i]].slot == s) steps_.push_back(Step{false, inset, s, order[i++]});
    }
}

void Frame::plan(Layer layer, int buffer) {
    steps_.clear();
    buffer = clamp_buffer(buffer);
    plan_slots(layer, buffer, layer == Layer::Inset, layer == Layer::World ? inset_link_[buffer] : kNotLinked);
}

void Frame::end_frame() {
    ++generation_;
    if (entries_.empty()) return;
    // A list is built in one frame and may be drawn in the next: entries are kept for a few generations.
    constexpr std::uint32_t kKeepGenerations = 4;
    std::erase_if(entries_, [this](const Entry& e) {
        return e.used ? generation_ - e.used_gen > 1 : generation_ - e.gen > kKeepGenerations;
    });
}

void Frame::discard_pending() {
    std::erase_if(entries_, [](const Entry& e) { return !e.used; });
}

std::size_t Frame::redraw_last() {
    std::size_t count = 0;
    for (Entry& e : entries_) {
        if (!e.used || generation_ - e.used_gen != 1) continue;
        e.used = false;
        e.gen = generation_;
        ++count;
    }
    return count;
}

namespace {
BufferSource g_buffer_source = nullptr;
}

void set_buffer_source(BufferSource source) { g_buffer_source = source; }

int build_buffer() { return g_buffer_source != nullptr ? g_buffer_source() : 0; }

void submit_keyed(Kind kind, std::uint32_t id, OrderKey key, const void* packet, Rect rect) {
    frame().submit(kind, id, key, build_buffer(), packet, rect);
}

Frame& frame() {
    static Frame f;
    return f;
}

}  // namespace sh::frame
