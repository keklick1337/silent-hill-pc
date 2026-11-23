// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/loader/queue/image_step.h"

#include <algorithm>

namespace sh::loader {

namespace {

int clamp_rows(int rows, int most) { return std::clamp(rows, 1, most); }

}  // namespace

int ImageStep::bit_depth(int mode) {
    switch (mode & 0x7) {
        case 0: return 4;
        case 1: return 8;
        case 2: return 16;
        case 3: return 24;
        default: return 0;
    }
}

bool ImageStep::run(ImageStepHost& host, const ImageRequest& r) {
    // Rows still owed by an earlier update that ran out of budget: everything else of this step
    // (uploads, mod pictures, the slot's base) already happened then and is not redone.
    const int resume = (resume_key_ == r.key && resume_ticket_ == r.ticket) ? resume_row_ : 0;
    resume_key_ = 0;

    // A palette row past VRAM names a chara pool slot: the picture goes into the slot's own texture
    // and nothing is uploaded, because the pixel rectangle aliases a real pool page.
    const bool pool_slot = r.target.place && r.target.palette_y >= spec_.pool_palette_row;

    VramRect pixels = r.pixels;
    if (r.target.place) {
        pixels.x = r.target.u + ((r.target.page_high & 0xF) << 6);
        pixels.y = r.target.v + ((r.target.page_high << 4) & 0x100);
    }
    if (!pool_slot && resume == 0) {
        host.upload_pixels(pixels);
    }
    const int bpp = bit_depth(r.mode);

    VramRect palette{};
    const bool has_palette = r.palette.has_value();
    if (has_palette) {
        palette = *r.palette;
        if (r.target.palette_x != -1) {
            palette.x = r.target.palette_x;
            palette.y = r.target.palette_y;
        }
        if (!pool_slot && resume == 0) {
            host.upload_palette(palette);
        }
    }
    if (resume == 0) {
        host.trace(pixels, bpp, palette, has_palette, pool_slot);
    }
    if (resume == 0 && bpp > 0 && host.dump_enabled()) {
        host.dump(pixels, has_palette ? r.palette->w : 0, has_palette ? palette.h : 1, bpp);
    }

    // Gutterless glyph cells of an HD font bleed into each other under bilinear sampling.
    host.nearest(r.font16 || r.font24);

    if (pool_slot) {
        pool(host, r, resume, pixels, palette, has_palette, bpp);
    } else if (!vram(host, r, resume, pixels, palette, has_palette, bpp)) {
        return false;
    }
    host.nearest(false);
    return true;
}

void ImageStep::pool(ImageStepHost& host, const ImageRequest& r, int resume, const VramRect& pixels,
                     const VramRect& palette, bool has_palette, int bpp) {
    // The slot number is spread over palette row groups and the palette x cell.
    const int slot = ((r.target.palette_y - spec_.pool_palette_row) / spec_.pool_rows) * 64 + r.target.palette_x / 16;
    if (has_palette) {
        host.slot_palette(slot, palette.x, palette.y);
    }
    const int width = bpp == 4 ? pixels.w * 4 : bpp == 8 ? pixels.w * 2 : bpp == 24 ? (pixels.w * 2) / 3 : pixels.w;
    const int height = pixels.h;
    const std::optional<std::string> mod = host.pending_picture();
    const bool per_row = mod.has_value() && host.per_row(*mod);

    if (bpp <= 0 || !r.valid_file) {
        host.note("[POOLTEX] slot " + std::to_string(slot) + ": bad TIM (mode=" + std::to_string(r.mode) +
                  ") or invalid info — not registered");
        return;
    }
    // A whole mod picture covers the slot with one palette; a per-row set lies over the disc art.
    if (!per_row && mod.has_value() && !mod->empty() && host.slot_whole(slot, *mod, width, height)) {
        return;
    }
    if (resume == 0) {
        host.slot_disc(slot, width, height);
    }
    int applied = 0;
    if (per_row) {
        const int rows = clamp_rows(has_palette ? palette.h : 1, spec_.pool_rows);
        for (int row = 0; row < rows; ++row) {
            if (host.slot_row(slot, *mod, row, width, height)) {
                ++applied;
            }
        }
        if (applied > 0) {
            host.note("[POOLTEX] slot " + std::to_string(slot) + ": " + std::to_string(applied) +
                      " loose CLUT-row override(s) from " + *mod);
        }
    }
    // Pack rows of a slot compose later, when a draw samples them (only a few of its rows ever are).
    // VRAM rows cannot wait: they compose inside the nearest-sampling window this step holds, and
    // outside it font packs show ghost glyphs.
    if (applied == 0 && host.pack_entries()) {
        host.slot_pack(slot, pixels, has_palette ? r.palette->w : 0, clamp_rows(has_palette ? palette.h : 1, spec_.pool_rows),
                       bpp, width, height);
    }
}

bool ImageStep::vram(ImageStepHost& host, const ImageRequest& r, int resume, const VramRect& pixels,
                     const VramRect& palette, bool has_palette, int bpp) {
    const std::optional<std::string> mod = host.pending_picture();
    bool replaced = false;
    const int palette_x = has_palette ? palette.x : -1;
    const int palette_y = has_palette ? palette.y : -1;

    // The upload rewrote VRAM: replacements keyed on those cells now show the wrong picture.
    if (resume == 0) {
        host.vram_written(pixels);
        if (has_palette) {
            host.invalidate(palette);
        }
    }

    if (mod.has_value() && !mod->empty()) {
        if (bpp <= 0) {
            host.note("[LOOSE/HIRES/SKIP] " + *mod + ": disc TIM bit-depth unknown (mode=" + std::to_string(r.mode) +
                      "); cannot register override");
        } else if (host.per_row(*mod)) {
            const int rows = clamp_rows(has_palette ? palette.h : 1, spec_.vram_rows);
            int applied = 0;
            for (int row = 0; row < rows; ++row) {
                if (host.vram_row(*mod, row, pixels, palette_x, has_palette ? palette.y + row : -1, bpp)) {
                    ++applied;
                }
            }
            if (applied > 0) {
                replaced = true;
                host.note("[LOOSE/HIRES] " + *mod + ": " + std::to_string(applied) + " loose CLUT-row override(s)");
            }
        } else {
            replaced = host.vram_whole(*mod, pixels, palette_x, palette_y,
                                       clamp_rows(has_palette ? palette.h : 1, spec_.vram_rows), bpp);
        }
    }

    if (!replaced && resume == 0 && bpp > 0 && !r.font16 && !r.font24) {
        replaced = host.vram_upscaled(pixels, palette_x, palette_y, bpp);
    }

    if (!replaced && bpp > 0 && host.pack_entries()) {
        const int palette_width = has_palette ? r.palette->w : 0;
        const int rows = clamp_rows(has_palette ? palette.h : 1, spec_.vram_rows);
        std::uint32_t missing = 0;
        int matched = 0;
        for (int row = resume; row < rows; ++row) {
            if (host.pack_budget_exceeded()) {
                if (!budget_noted_) {
                    budget_noted_ = true;
                    host.note("[TEXPACK] GL byte budget reached — further VRAM rows keep native art");
                }
                break;
            }
            // Out of compose budget for this update: park on this row; the queue runs the step again.
            if (budget_ <= 0) {
                resume_key_ = r.key;
                resume_ticket_ = r.ticket;
                resume_row_ = row;
                host.nearest(false);
                return false;
            }
            bool built = false;
            const PackRow got = host.pack_row(row, pixels, palette_width, bpp, palette_x,
                                              has_palette ? palette.y + row : -1, &built);
            if (built) {
                --budget_;
            }
            if (got != PackRow::Miss) {
                ++matched;
            } else if (row < 32) {
                missing |= 1u << row;
            }
        }
        host.report_uncovered(missing, matched);
    }
    return true;
}

}  // namespace sh::loader
