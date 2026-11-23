// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The image step after a queued TIM read: where the picture and its
// palette go in VRAM (or into a chara pool slot that has no VRAM of its own), and which replacements
// cover it: a mod's whole picture or per-palette-row pictures, the Cyrillic font, and pack rows
// (pictures a host keeps per palette row) composed under a per-update budget. The host does the
// uploads and the texture work; this class decides what happens, in what order, and resumes a step the
// budget cut short.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace sh::loader {

struct VramRect {
    int x = 0, y = 0, w = 0, h = 0;
};

// Where the request says the picture goes; `place` false = where the file says.
struct ImageTarget {
    bool place = false;
    int u = 0, v = 0;
    int page_high = 0;       // the second byte of the texture page word: bits 0-3 x/64, bit 4 y/256
    int palette_x = -1;      // -1 = where the file says
    int palette_y = -1;
};

struct ImageRequest {
    std::uintptr_t key = 0;  // the queue record (with `ticket`, it names the step to resume)
    std::int32_t ticket = 0;
    bool valid_file = true;
    bool font16 = false;     // the 12x16 font atlas
    bool font24 = false;     // the credits font
    ImageTarget target;
    VramRect pixels;         // the file's own rectangles
    std::optional<VramRect> palette;
    int mode = 0;            // TIM flags: bits 0-2 bit depth (0 4bpp, 1 8bpp, 2 16bpp, 3 24bpp)
};

struct ImageSpec {
    int pool_palette_row = 512;  // a palette row at or past this names a pool slot, not VRAM
    int pool_rows = 16;          // palette rows of one pool slot
    int vram_rows = 16;          // palette rows a replacement covers in VRAM
    int compose_budget = 2;      // pack rows built per queue update
};

enum class PackRow { Canvas, Dds, Miss };

class ImageStepHost {
public:
    virtual ~ImageStepHost() = default;

    virtual void upload_pixels(const VramRect& r) = 0;
    virtual void upload_palette(const VramRect& r) = 0;
    virtual void trace(const VramRect& pixels, int bpp, const VramRect& palette, bool has_palette, bool pool) = 0;
    virtual bool dump_enabled() = 0;
    virtual void dump(const VramRect& pixels, int palette_width, int rows, int bpp) = 0;
    virtual void nearest(bool on) = 0;
    virtual std::optional<std::string> pending_picture() = 0;
    virtual bool per_row(const std::string& path) = 0;
    virtual void note(const std::string& line) = 0;

    // A pool slot.
    virtual void slot_palette(int slot, int x, int y) = 0;
    virtual bool slot_whole(int slot, const std::string& path, int width, int height) = 0;
    virtual void slot_disc(int slot, int width, int height) = 0;
    virtual bool slot_row(int slot, const std::string& path, int row, int width, int height) = 0;
    virtual bool pack_entries() = 0;
    virtual void slot_pack(int slot, const VramRect& pixels, int palette_width, int rows, int bpp, int width,
                           int height) = 0;

    // VRAM.
    virtual void vram_written(const VramRect& pixels) = 0;
    virtual void invalidate(const VramRect& r) = 0;
    virtual bool vram_row(const std::string& path, int row, const VramRect& pixels, int palette_x, int palette_y,
                          int bpp) = 0;
    // The whole picture across `rows` palette rows.
    virtual bool vram_whole(const std::string& path, const VramRect& pixels, int palette_x, int palette_y, int rows,
                            int bpp) = 0;
    // No mod replaced the picture: the upscaled one (texture_upscale) over it, through palette row `palette_y` when
    // it has a palette; false without one.
    virtual bool vram_upscaled(const VramRect& /*pixels*/, int /*palette_x*/, int /*palette_y*/, int /*bpp*/) {
        return false;
    }
    virtual bool pack_budget_exceeded() = 0;
    // Composes palette row `row`; `built` is true when it was built now rather than taken from a cache.
    virtual PackRow pack_row(int row, const VramRect& pixels, int palette_width, int bpp, int palette_x,
                             int palette_y, bool* built) = 0;
    virtual void report_uncovered(std::uint32_t missing, int matched) = 0;
};

class ImageStep {
public:
    explicit ImageStep(ImageSpec spec = {}) : spec_(spec) {}

    // Once per queue update, before its after-load step. Building every palette row of a
    // multi-palette TIM at once was a 200-300 ms hitch on room entry, so only `compose_budget` rows
    // are built per update. A row put off is never dropped: the step reports itself unfinished, the
    // queue keeps counting it, and every blocking wait keeps updating until the last row is built.
    void refill() { budget_ = spec_.compose_budget; }

    // False when the compose budget ran out: run it again on a later update (the uploads are not redone).
    bool run(ImageStepHost& host, const ImageRequest& r);

    static int bit_depth(int mode);

private:
    void pool(ImageStepHost& host, const ImageRequest& r, int resume, const VramRect& pixels,
              const VramRect& palette, bool has_palette, int bpp);
    bool vram(ImageStepHost& host, const ImageRequest& r, int resume, const VramRect& pixels,
              const VramRect& palette, bool has_palette, int bpp);

    ImageSpec spec_;
    int budget_ = 0;
    bool budget_noted_ = false;  // the GL byte budget is logged once
    std::uintptr_t resume_key_ = 0;
    std::int32_t resume_ticket_ = 0;
    int resume_row_ = 0;
};

}  // namespace sh::loader
