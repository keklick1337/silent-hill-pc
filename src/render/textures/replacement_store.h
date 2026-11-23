// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Full-colour pictures that stand in for pictures in video memory (docs/RENDER.md, section 3.5). The 2D
// screens, the fonts and the 3D effects sample 1024x512 video memory through a texture page and a palette; a
// replacement is an RGBA texture of any size keyed to the cells the replaced picture occupies there, and a draw
// whose page and palette meet those cells samples the replacement instead, stretched over the original.
//
// Two kinds are kept:
//   - pictures: keyed by the rectangle (and palette row) of a picture in video memory. A mod's HD picture of a
//     disc TIM (the section on models and textures), the pages of the game font, the input
//     prompt atlas;
//   - sheets: keyed by a number carried in a palette word no video memory row can have. Art the engine makes
//     itself and that never lives in video memory (the bullet holes).
//
// The store decides which replacement a draw takes; the textures themselves belong to the renderer, reached
// through TextureSink so the store works (and is tested) without a device.

#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace sh::textures {

// The renderer's texture registry, as far as the store needs it. Ids are never 0.
class TextureSink {
public:
    virtual ~TextureSink() = default;
    // An RGBA8 texture, rows top first; 0 when it cannot be made.
    virtual std::uint32_t create(int width, int height, std::span<const std::uint8_t> rgba) = 0;
    // New pixels for the whole of a texture made with this width and height.
    virtual void update(std::uint32_t id, int width, int height, std::span<const std::uint8_t> rgba) = 0;
    virtual void destroy(std::uint32_t id) = 0;
    // Point sampling instead of the preset's filter.
    virtual void set_point_sampled(std::uint32_t id, bool on) = 0;
};

// Where the replaced picture lives in video memory, in 16-bit cells. A picture of 4 or 8 bits is seen through one
// palette row; a direct-colour picture has none (-1, -1).
struct PictureKey {
    int x = 0, y = 0, w = 0, h = 0;
    int palette_x = -1, palette_y = -1;
    int bits = 4;

    bool operator==(const PictureKey&) const = default;
};

// What a draw needs to sample a replacement in place of the original.
struct Replacement {
    std::uint32_t texture = 0;
    int native_w = 0, native_h = 0;  // texels of the replaced picture: the draw's texel coordinates span these
    int offset_x = 0, offset_y = 0;  // the texture page's origin inside the replaced picture, in its texels
    int width = 0, height = 0;       // pixels of the replacement
};

class ReplacementStore {
public:
    // Palette rows of video memory end at 511: a palette word with a row from here on names a sheet.
    static constexpr int kSheetPaletteRow = 512;
    static constexpr int kSheetRowGroup = 16;  // rows one sheet answers to
    static constexpr int kSheets = 512;
    // As many pictures as there ever were; the font pages and a mod's 2D pictures use a few dozen.
    static constexpr std::size_t kPictures = 256;

    // The palette word a draw uses to sample sheet `sheet`: the number is spread over the palette x cell and the
    // row groups past video memory.
    static constexpr std::uint16_t sheet_palette(int sheet) {
        return static_cast<std::uint16_t>(((kSheetPaletteRow + (sheet / 64) * kSheetRowGroup) << 6) | (sheet % 64));
    }

    explicit ReplacementStore(TextureSink& sink) : sink_(sink) {}
    ReplacementStore(const ReplacementStore&) = delete;
    ReplacementStore& operator=(const ReplacementStore&) = delete;

    // Replaces the picture at `key` (the texture of an equal key is reused). False when the texture cannot be made
    // or the store is full.
    bool put(const PictureKey& key, std::span<const std::uint8_t> rgba, int width, int height, bool point_sampled);
    // The same picture under `rows` palette rows from the key's on, for a picture drawn in its own colours whatever
    // palette a draw selects: one texture serves them all. A key without a palette is put once. False when no row
    // could be put.
    bool put_rows(const PictureKey& first, int rows, std::span<const std::uint8_t> rgba, int width, int height,
                  bool point_sampled);
    // A pinned picture stays when its cells are written (its video memory home never held it: the font pages sit
    // on cells that hold other art). False when no picture has this key.
    bool pin(const PictureKey& key, bool pinned);
    // The game wrote these cells: pictures whose rectangle or palette row they touch no longer show what a draw
    // keyed to them would sample, and go.
    void cells_written(int x, int y, int w, int h);

    // Sets sheet `sheet`; `native_w` x `native_h` is the texel space its draws address it in. A sheet as large as
    // its texel space is point sampled.
    bool put_sheet(int sheet, std::span<const std::uint8_t> rgba, int width, int height, int native_w, int native_h);

    // The replacement a draw with this texture page and palette word samples, if any.
    std::optional<Replacement> find(std::uint16_t page, std::uint16_t palette) const;

    std::size_t picture_count() const { return pictures_.size(); }
    // Whether a picture is put at `key` (not yet taken away by a write to its cells).
    bool has(const PictureKey& key) const {
        for (const Picture& p : pictures_)
            if (p.key == key) return true;
        return false;
    }

private:
    // A texture of the sink, destroyed with its last user.
    struct Texture {
        Texture(TextureSink& sink, std::uint32_t id, int width, int height)
            : sink(sink), id(id), width(width), height(height) {}
        ~Texture() { sink.destroy(id); }
        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;

        TextureSink& sink;
        std::uint32_t id;
        int width, height;
    };
    using TextureRef = std::shared_ptr<Texture>;
    struct Picture {
        PictureKey key;
        TextureRef texture;
        bool pinned = false;
    };
    struct Sheet {
        TextureRef texture;
        int native_w = 0, native_h = 0;
    };
    // The answer to one (page, palette) pair while the pictures stay as they are: a frame asks for the same few
    // pairs once per drawn primitive.
    struct Remembered {
        std::uint32_t pair = 0;
        std::uint32_t change = 0;  // the value of change_ it was found at; 0 never matches
        int picture = -1;
    };

    // New pixels for `texture`: in place when it is this size and nobody shares it, else a new texture.
    bool upload(TextureRef& texture, std::span<const std::uint8_t> rgba, int width, int height, bool point_sampled);
    bool place(const PictureKey& key, const TextureRef& texture);
    Picture* picture(const PictureKey& key);
    void remove(std::size_t index);
    int match(std::uint16_t page, std::uint16_t palette) const;

    TextureSink& sink_;
    // In the order they were put; a removed picture's place is taken by the last one. The first match wins a draw.
    std::vector<Picture> pictures_;
    std::vector<Sheet> sheets_;
    std::uint32_t change_ = 1;  // counts every change of pictures_
    mutable std::array<Remembered, 256> remembered_{};
};

}  // namespace sh::textures
