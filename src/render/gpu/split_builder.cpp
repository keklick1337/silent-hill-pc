#include "render/gpu/split_builder.h"

#include "engine/log/sh_log.h"

#include <cstring>

namespace sh::gpu {

namespace {

Blend page_blend(std::uint16_t tpage) { return static_cast<Blend>(((tpage >> 5) & 3) + 1); }
TexFormat page_format(std::uint16_t tpage) { return static_cast<TexFormat>((tpage >> 7) & 3); }

// The state key of a Prims split: everything that makes two primitives need different draw state.
struct Key {
    Blend blend;
    TexFormat format;
    TextureRef texture;
    DepthMode depth;
    bool glyph;
    bool mixed;
};

bool same_split(const Split& s, const Key& k, const PrimState& st) {
    // The override offset and the feedback mode are compared even for prims that sample no override: two chunks of
    // one override texture at different page origins must not share a split.
    return s.kind == Split::Kind::Prims && s.blend == k.blend && s.format == k.format && s.texture == k.texture &&
           s.depth == k.depth && s.override_info.offset_x == st.override_info.offset_x &&
           s.override_info.offset_y == st.override_info.offset_y &&
           s.override_info.feedback_screen == st.override_info.feedback_screen && s.mask_mode == st.mask_mode &&
           s.env.clip == st.env.clip && s.env.draw_on_display == st.env.draw_on_display && s.glyph == k.glyph &&
           s.mixed == k.mixed && s.label == st.label;
}

}  // namespace

void SplitBuilder::begin_primitive(bool semi, std::uint8_t r0) {
    state_.semi = semi;
    state_.r0 = r0;
    state_.feedback = false;
    state_.scratch = false;
    state_.glyph = false;
}

void SplitBuilder::close_open_split() {
    if (batch_.splits.empty()) return;
    Split& s = batch_.splits.back();
    if (s.kind == Split::Kind::Prims) s.count = vertex_count() - s.first;
}

bool SplitBuilder::room_for_split() {
    if (batch_.splits.size() < kMaxSplits) return true;
    // A frame with more state changes than one batch holds draws what it has and goes on: geometry past the cap
    // is not dropped. The line is rate-limited, a dense frame would otherwise log per split.
    ++stats_.cap_flushes;
    if (stats_.cap_flushes <= 5 || (stats_.cap_flushes & 0xFFFu) == 0)
        SH_LOG_WARN(RENDER, "split cap (%u) reached, batch flushed early (%llu times so far)", kMaxSplits,
                    static_cast<unsigned long long>(stats_.cap_flushes));
    flush();
    return true;
}

Split& SplitBuilder::push_split(Split::Kind kind) {
    close_open_split();
    room_for_split();
    Split& s = batch_.splits.emplace_back();
    s.kind = kind;
    s.env = state_.env;
    s.disp = state_.disp;
    s.mask_mode = state_.mask_mode;
    s.first = vertex_count();
    s.count = 0;
    return s;
}

void SplitBuilder::begin_prims(bool semi, bool textured, DepthMode depth) {
    const PrimState& st = state_;
    const std::uint16_t tpage = st.env.tpage;

    Key k;
    k.blend = semi ? page_blend(tpage) : Blend::None;
    // The opaque base layer of the scene-scratch soft focus fades in at the damp instead of covering the frame.
    if (st.scratch && !semi && st.feedback_damp < 1.f) k.blend = Blend::ConstantAlpha;
    k.format = page_format(tpage);
    k.texture = textured ? TextureRef{TextureRef::Kind::Vram, 0, 0} : TextureRef{};
    const bool override = textured && st.override_texture.kind != TextureRef::Kind::White;
    if (override) {
        k.format = TexFormat::Rgba32;
        k.texture = st.override_texture;  // with its normal map: same colour, different normals must not batch
    }
    // One program draws every video-memory format and untextured prims, so only an override (its own texture)
    // still splits on texture; the reserved page format 3 keeps its own path.
    k.mixed = mixed_program_ && (!textured || k.format <= TexFormat::Direct16);
    if (k.mixed) {
        k.format = TexFormat::Direct16;
        k.texture = TextureRef{TextureRef::Kind::Vram, 0, 0};
    }
    k.depth = depth;
    k.glyph = textured && st.glyph;

    if (!batch_.splits.empty() && same_split(batch_.splits.back(), k, st)) return;

    Split& s = push_split(Split::Kind::Prims);
    s.blend = k.blend;
    s.format = k.format;
    s.texture = k.texture;
    s.override_info = st.override_info;
    s.depth = k.depth;
    s.mixed = k.mixed;
    s.glyph = k.glyph;
    s.label = st.label;
}

Vertex* SplitBuilder::reserve(std::uint32_t count) {
    const std::size_t at = batch_.vertices.size();
    if (at + count > kMaxVertices) return nullptr;
    batch_.vertices.resize(at + count);
    return batch_.vertices.data() + at;
}

void SplitBuilder::truncate(std::uint32_t count) {
    if (count < batch_.vertices.size()) batch_.vertices.resize(count);
}

void SplitBuilder::append(std::span<const Vertex> vertices) {
    if (vertices.empty()) return;
    Vertex* v = reserve(static_cast<std::uint32_t>(vertices.size()));
    if (v == nullptr) {
        // Full: draw what is there and continue the same split in the next batch.
        Split open;
        const bool reopen = !batch_.splits.empty() && batch_.splits.back().kind == Split::Kind::Prims;
        if (reopen) open = batch_.splits.back();
        flush();
        if (reopen) {
            open.first = 0;
            open.count = 0;
            batch_.splits.push_back(open);
        }
        v = reserve(static_cast<std::uint32_t>(vertices.size()));
        if (v == nullptr) return;  // larger than a whole batch
    }
    std::memcpy(static_cast<void*>(v), vertices.data(), vertices.size_bytes());
}

void SplitBuilder::add_scene(std::int32_t item, float ofs_x, float ofs_y) {
    Split& s = push_split(Split::Kind::Scene);
    s.scene_item = item;
    s.scene_ofs_x = ofs_x;
    s.scene_ofs_y = ofs_y;
}

void SplitBuilder::add_capture(const Rect& rect) {
    Split& s = push_split(Split::Kind::Capture);
    s.capture = rect;
}

void SplitBuilder::flush_if_near_full(std::uint32_t room) {
    if (batch_.vertices.size() + room > kMaxVertices) flush();
}

void SplitBuilder::flush() {
    // The page override is per primitive (resolve_page sets it before every textured split), so it is not reset
    // here: a flush between begin_prims and reserve of one primitive must keep it.
    close_open_split();
    if (!batch_.splits.empty()) {
        ++stats_.flushes;
        if (flush_) flush_(batch_);
    }
    batch_.clear();
}

void SplitBuilder::discard() {
    batch_.clear();
    state_.override_texture = TextureRef{};
    state_.override_info = OverrideInfo{};
}

}  // namespace sh::gpu
