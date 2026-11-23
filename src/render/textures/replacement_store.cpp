// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "render/textures/replacement_store.h"

namespace sh::textures {

namespace {

// A texture page word: bits 0-3 the page column (64 cells each), bit 4 the page row (256 lines), bits 7-8 the
// colour depth of what it samples.
struct Page {
    int x, y;
    int bits;   // 4, 8, 16; 0 for the depth no picture has
    int cells;  // cells across one page at this depth: 256 texels of 4 bits are 64 cells
};

Page page_of(std::uint16_t word) {
    constexpr int kBits[] = {4, 8, 16, 0};
    constexpr int kCells[] = {64, 128, 256, 256};
    const int depth = (word >> 7) & 3;
    return {(word & 0xF) * 64, ((word >> 4) & 1) * 256, kBits[depth], kCells[depth]};
}

constexpr int kPageLines = 256;
// The display buffers take the left 320 columns of video memory; a direct-colour page there is a draw that reads
// the frame back, never a picture.
constexpr int kFrameColumns = 320;

bool has_palette(int bits) { return bits == 4 || bits == 8; }
int texels_per_cell(int bits) { return bits == 4 ? 4 : bits == 8 ? 2 : 1; }
int palette_cells(int bits) { return bits == 8 ? 256 : 16; }

bool spans_meet(int a, int a_len, int b, int b_len) { return a < b + b_len && b < a + a_len; }

}  // namespace

bool ReplacementStore::upload(TextureRef& texture, std::span<const std::uint8_t> rgba, int width, int height,
                              bool point_sampled) {
    if (width <= 0 || height <= 0 ||
        rgba.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
        return false;
    }
    if (texture != nullptr && texture.use_count() == 1 && texture->width == width && texture->height == height) {
        sink_.update(texture->id, width, height, rgba);
    } else {
        texture.reset();
        const std::uint32_t id = sink_.create(width, height, rgba);
        if (id == 0) return false;
        texture = std::make_shared<Texture>(sink_, id, width, height);
    }
    sink_.set_point_sampled(texture->id, point_sampled);
    return true;
}

ReplacementStore::Picture* ReplacementStore::picture(const PictureKey& key) {
    for (Picture& p : pictures_) {
        if (p.key == key) return &p;
    }
    return nullptr;
}

void ReplacementStore::remove(std::size_t index) {
    if (index + 1 != pictures_.size()) pictures_[index] = std::move(pictures_.back());
    pictures_.pop_back();
    ++change_;
}

bool ReplacementStore::place(const PictureKey& key, const TextureRef& texture) {
    if (Picture* known = picture(key)) {
        known->texture = texture;
        return true;
    }
    if (pictures_.size() >= kPictures) return false;
    pictures_.push_back(Picture{key, texture, false});
    ++change_;
    return true;
}

bool ReplacementStore::put(const PictureKey& key, std::span<const std::uint8_t> rgba, int width, int height,
                           bool point_sampled) {
    Picture* known = picture(key);
    if (known == nullptr && pictures_.size() >= kPictures) return false;
    TextureRef texture = known != nullptr ? known->texture : nullptr;
    if (known != nullptr) known->texture.reset();  // the pixels may be rewritten in place when nobody else shows them
    if (!upload(texture, rgba, width, height, point_sampled)) {
        // Nothing to show for the key: its draws go back to video memory.
        if (known != nullptr) remove(static_cast<std::size_t>(known - pictures_.data()));
        return false;
    }
    return place(key, texture);
}

bool ReplacementStore::put_rows(const PictureKey& first, int rows, std::span<const std::uint8_t> rgba, int width,
                                int height, bool point_sampled) {
    TextureRef texture;
    if (!upload(texture, rgba, width, height, point_sampled)) return false;
    const bool one = rows < 2 || first.palette_x < 0 || first.palette_y < 0;
    bool any = false;
    for (int row = 0; row < (one ? 1 : rows); ++row) {
        PictureKey key = first;
        key.palette_y += row;
        any = place(key, texture) || any;
    }
    return any;
}

bool ReplacementStore::pin(const PictureKey& key, bool pinned) {
    Picture* p = picture(key);
    if (p == nullptr) return false;
    p->pinned = pinned;
    return true;
}

void ReplacementStore::cells_written(int x, int y, int w, int h) {
    for (std::size_t i = 0; i < pictures_.size();) {
        const Picture& p = pictures_[i];
        const PictureKey& k = p.key;
        const bool pixels = spans_meet(k.x, k.w, x, w) && spans_meet(k.y, k.h, y, h);
        const bool palette = k.palette_x >= 0 && spans_meet(k.palette_x, palette_cells(k.bits), x, w) &&
                             spans_meet(k.palette_y, 1, y, h);
        if (!p.pinned && (pixels || palette)) {
            remove(i);  // the last picture took this place: look at it next
        } else {
            ++i;
        }
    }
}

bool ReplacementStore::put_sheet(int sheet, std::span<const std::uint8_t> rgba, int width, int height, int native_w,
                                 int native_h) {
    if (sheet < 0 || sheet >= kSheets) return false;
    if (sheets_.empty()) sheets_.resize(kSheets);
    Sheet& s = sheets_[static_cast<std::size_t>(sheet)];
    if (!upload(s.texture, rgba, width, height, width == native_w && height == native_h)) return false;
    s.native_w = native_w;
    s.native_h = native_h;
    return true;
}

// The first picture a draw through this page and palette samples, or -1.
int ReplacementStore::match(std::uint16_t page_word, std::uint16_t palette) const {
    const Page page = page_of(page_word);
    const int palette_x = (palette & 0x3F) * 16;
    const int palette_y = (palette >> 6) & 0x1FF;
    for (std::size_t i = 0; i < pictures_.size(); ++i) {
        const PictureKey& k = pictures_[i].key;
        // A page reaches `cells` to the right of its origin and 256 lines down; the picture need only meet that
        // window (a picture may start left of the page a draw addresses it through). The palette row tells apart
        // pictures that share cells.
        if (k.bits != page.bits) continue;
        if (!spans_meet(k.x, k.w, page.x, page.cells) || !spans_meet(k.y, k.h, page.y, kPageLines)) continue;
        if (k.palette_x >= 0 && (k.palette_x != palette_x || k.palette_y != palette_y)) continue;
        return static_cast<int>(i);
    }
    return -1;
}

std::optional<Replacement> ReplacementStore::find(std::uint16_t page_word, std::uint16_t palette) const {
    const Page page = page_of(page_word);

    if (palette & 0x8000) {
        // A sheet's word. A direct-colour draw does not use its palette word at all (frame read-backs leave it
        // unset), so whatever number such a word seems to carry is not one.
        const int row = ((palette >> 6) & 0x3FF) - kSheetPaletteRow;
        if (!has_palette(page.bits) || row < 0) return std::nullopt;
        const std::size_t sheet = static_cast<std::size_t>((row / kSheetRowGroup) * 64 + (palette & 0x3F));
        if (sheet >= sheets_.size() || sheets_[sheet].texture == nullptr) return std::nullopt;
        const Sheet& s = sheets_[sheet];
        return Replacement{s.texture->id, s.native_w, s.native_h, 0, 0, s.texture->width, s.texture->height};
    }

    if (!has_palette(page.bits) && page.x < kFrameColumns) return std::nullopt;
    if (pictures_.empty()) return std::nullopt;

    const std::uint32_t pair = static_cast<std::uint32_t>(page_word) << 16 | palette;
    Remembered& r = remembered_[(pair * 2654435761u) >> 24];
    if (r.change != change_ || r.pair != pair) r = Remembered{pair, change_, match(page_word, palette)};
    if (r.picture < 0) return std::nullopt;

    const Picture& p = pictures_[static_cast<std::size_t>(r.picture)];
    const int scale = texels_per_cell(p.key.bits);
    return Replacement{p.texture->id,    p.key.w * scale,  p.key.h,          (page.x - p.key.x) * scale,
                       page.y - p.key.y, p.texture->width, p.texture->height};
}

}  // namespace sh::textures
