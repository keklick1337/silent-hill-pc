// Front end of the primitive renderer (RENDER.md section 3): collects the frame's vertex stream and splits.
// Everything that draws through the renderer appends here: the Frame consumers (draw2d / draw3d commands, scene,
// capture entries). A new split starts when the state key changes (blend, format, texture, override, mask mode,
// depth mode, draw area, dfe, glyph, mixed, label). Flushes hand the batch to the Renderer.
#pragma once

#include "render/gpu/gpu_types.h"

#include <cstdint>
#include <functional>
#include <span>

namespace sh::gpu {

// State the next primitives are recorded under; the front end keeps it current (draw env, tpage packets, the
// hi-res override lookup, mask bits).
struct PrimState {
    DrawEnv env;
    DispEnv disp;
    TextureRef override_texture;  // Kind::Override when a hi-res texture replaces the page, else White
    OverrideInfo override_info;
    std::uint8_t mask_mode = 0;
    float feedback_damp = 1.f;
    const char* label = nullptr;

    // Per primitive, reset by begin_primitive().
    bool semi = false;            // semi-transparency bit of the primitive
    std::uint8_t r0 = 128;        // red modulation (the loading trail alternates 127/128)
    bool feedback = false;        // samples the display-buffer band (set by resolve_page)
    bool scratch = false;         // samples the live scene-scratch rect: opaque prims fade at the damp (ConstantAlpha)
    bool glyph = false;           // point-sampled text glyph (set by resolve_page)
};

class SplitBuilder {
public:
    static constexpr std::uint32_t kMaxVertices = 1u << 18;
    static constexpr std::uint32_t kMaxSplits = 16384;

    using FlushFn = std::function<void(SplitBatch&)>;
    explicit SplitBuilder(FlushFn flush) : flush_(std::move(flush)) {}

    PrimState& state() { return state_; }
    const PrimState& state() const { return state_; }

    // Starts a primitive: resets the per-primitive flags of state().
    void begin_primitive(bool semi, std::uint8_t r0);
    // Opens (or continues) the split for a primitive. `semi`: semi-transparent (blend from the tpage word).
    void begin_prims(bool semi, bool textured, DepthMode depth = DepthMode::Disabled);
    // Appends whole triangles to the open split.
    void append(std::span<const Vertex> vertices);
    Vertex* reserve(std::uint32_t count);  // in place; null when the batch is full (then flush first)

    void add_scene(std::int32_t item, float ofs_x, float ofs_y);
    void add_capture(const Rect& rect);

    // Flush when fewer than `room` vertices are left (whole-town frames exceed one buffer).
    void flush_if_near_full(std::uint32_t room = 24);
    void flush();  // hands the batch to the renderer and starts a new one
    // Drops the batch without drawing it (ResetGraph, a cancelled frame) and forgets the page override.
    void discard();
    // Gives back reserved vertices that were not written: the stream ends at `count` again (count <= vertex_count()).
    void truncate(std::uint32_t count);

    // All formats and untextured prims in one program (prim_batch draws every format; console `mixshader 0`
    // splits per format).
    void set_mixed_program(bool on) { mixed_program_ = on; }
    bool mixed_program() const { return mixed_program_; }

    SplitBatch& batch() { return batch_; }
    std::uint32_t vertex_count() const { return static_cast<std::uint32_t>(batch_.vertices.size()); }

    struct Stats {
        std::uint64_t flushes = 0, cap_flushes = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    void close_open_split();
    bool room_for_split();
    Split& push_split(Split::Kind kind);
    FlushFn flush_;
    PrimState state_;
    SplitBatch batch_;
    bool mixed_program_ = true;
    Stats stats_;
};

}  // namespace sh::gpu
