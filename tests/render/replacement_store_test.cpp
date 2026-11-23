// The replacement pictures (render/textures/replacement_store): which replacement a draw samples for a texture page
// and a palette word, what a write to video memory removes, pinned pictures, shared rows, sheets.

#include "framework/sh_test.h"
#include "render/textures/replacement_store.h"

#include <cstdint>
#include <map>
#include <vector>

namespace {

namespace tx = sh::textures;

// Textures as the store asked for them.
class FakeSink final : public tx::TextureSink {
public:
    struct Made {
        int width = 0, height = 0;
        std::vector<std::uint8_t> pixels;
        bool point_sampled = false;
        int updates = 0;
    };

    std::uint32_t create(int width, int height, std::span<const std::uint8_t> rgba) override {
        if (refuse) return 0;
        live[++last] = Made{width, height, {rgba.begin(), rgba.end()}, false, 0};
        ++created;
        return last;
    }
    void update(std::uint32_t id, int, int, std::span<const std::uint8_t> rgba) override {
        live.at(id).pixels.assign(rgba.begin(), rgba.end());
        ++live.at(id).updates;
    }
    void destroy(std::uint32_t id) override { live.erase(id); }
    void set_point_sampled(std::uint32_t id, bool on) override { live.at(id).point_sampled = on; }

    std::map<std::uint32_t, Made> live;
    std::uint32_t last = 0;
    int created = 0;
    bool refuse = false;
};

std::vector<std::uint8_t> pixels(int width, int height, std::uint8_t value) {
    return std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, value);
}

// A texture page word: column (64 cells), row (256 lines), depth 0 (4 bits), 1 (8 bits), 2 (16 bits).
constexpr std::uint16_t page(int column, int row, int depth) {
    return static_cast<std::uint16_t>(column | row << 4 | depth << 7);
}
// A palette word: x in 16-cell steps, y a row.
constexpr std::uint16_t palette(int x, int y) { return static_cast<std::uint16_t>(y << 6 | x / 16); }

}  // namespace

SH_TEST(replacement_store_finds_a_picture_by_page_and_palette) {
    FakeSink sink;
    tx::ReplacementStore store(sink);
    // A 4-bit picture of 256x240 texels at cells (704, 256): 64 cells wide, palette at (0, 480).
    const tx::PictureKey key{704, 256, 64, 240, 0, 480, 4};
    SH_CHECK(store.put(key, pixels(512, 480, 9), 512, 480, false));
    SH_CHECK(store.picture_count() == 1);

    const auto found = store.find(page(11, 1, 0), palette(0, 480));
    SH_CHECK(found.has_value());
    if (found) {
        SH_CHECK(found->texture != 0);
        SH_CHECK(found->native_w == 256 && found->native_h == 240);
        SH_CHECK(found->offset_x == 0 && found->offset_y == 0);
        SH_CHECK(found->width == 512 && found->height == 480);
        SH_CHECK(!sink.live.at(found->texture).point_sampled);
    }
    // Another palette row, another depth, another page: the original stays.
    SH_CHECK(!store.find(page(11, 1, 0), palette(0, 481)));
    SH_CHECK(!store.find(page(11, 1, 0), palette(16, 480)));
    SH_CHECK(!store.find(page(11, 1, 1), palette(0, 480)));
    SH_CHECK(!store.find(page(10, 1, 0), palette(0, 480)));
    SH_CHECK(!store.find(page(11, 0, 0), palette(0, 480)));
}

SH_TEST(replacement_store_offsets_a_page_inside_a_wide_picture) {
    FakeSink sink;
    tx::ReplacementStore store(sink);
    // An 8-bit picture of 320x240 texels from cell 320: 160 cells, which is more than the 128 of one page. It does
    // not start on a page: the page at 256 reaches it from the left, the page at 448 from inside.
    const tx::PictureKey key{320, 0, 160, 240, 0, 500, 8};
    SH_CHECK(store.put(key, pixels(320, 240, 1), 320, 240, false));

    const auto left = store.find(page(4, 0, 1), palette(0, 500));
    SH_CHECK(left.has_value());
    if (left) {
        SH_CHECK(left->native_w == 320 && left->native_h == 240);
        SH_CHECK(left->offset_x == (256 - 320) * 2 && left->offset_y == 0);
    }
    const auto inside = store.find(page(7, 0, 1), palette(0, 500));
    SH_CHECK(inside.has_value());
    if (inside) SH_CHECK(inside->offset_x == (448 - 320) * 2);
    // The page at 128 ends where the picture begins (128 cells of 8 bits reach 256).
    SH_CHECK(!store.find(page(2, 0, 1), palette(0, 500)));
    // The lower page row starts below the picture's 240 lines.
    SH_CHECK(!store.find(page(5, 1, 1), palette(0, 500)));
}

SH_TEST(replacement_store_direct_colour_pictures) {
    FakeSink sink;
    tx::ReplacementStore store(sink);
    // No palette: any palette word matches.
    SH_CHECK(store.put({640, 0, 256, 256, -1, -1, 16}, pixels(8, 8, 3), 8, 8, false));
    const auto found = store.find(page(10, 0, 2), 0x1234);
    SH_CHECK(found.has_value());
    if (found) SH_CHECK(found->native_w == 256 && found->native_h == 256);

    // The display buffers' columns: a direct-colour page there reads the frame back and never a picture.
    SH_CHECK(store.put({0, 0, 320, 240, -1, -1, 16}, pixels(8, 8, 3), 8, 8, false));
    SH_CHECK(!store.find(page(0, 0, 2), 0));
    SH_CHECK(!store.find(page(4, 0, 2), 0));
    // From column 320 on a direct-colour page takes a picture its 256 cells meet: cells 320..575 meet neither,
    // cells 448..703 meet the first.
    SH_CHECK(!store.find(page(5, 0, 2), 0));
    SH_CHECK(store.find(page(7, 0, 2), 0).has_value());
}

SH_TEST(replacement_store_same_key_replaces_in_place) {
    FakeSink sink;
    tx::ReplacementStore store(sink);
    const tx::PictureKey key{704, 0, 64, 256, 0, 480, 4};
    SH_CHECK(store.put(key, pixels(4, 4, 1), 4, 4, true));
    const std::uint32_t first = store.find(page(11, 0, 0), palette(0, 480))->texture;
    SH_CHECK(sink.live.at(first).point_sampled);

    // The same size: the texture keeps its id and gets the new pixels and sampling.
    SH_CHECK(store.put(key, pixels(4, 4, 2), 4, 4, false));
    SH_CHECK(store.picture_count() == 1);
    SH_CHECK(store.find(page(11, 0, 0), palette(0, 480))->texture == first);
    SH_CHECK(sink.live.at(first).updates == 1 && sink.live.at(first).pixels[0] == 2);
    SH_CHECK(!sink.live.at(first).point_sampled);

    // Another size: a new texture, the old one is gone.
    SH_CHECK(store.put(key, pixels(8, 4, 3), 8, 4, false));
    SH_CHECK(store.picture_count() == 1 && sink.live.size() == 1);
    SH_CHECK(store.find(page(11, 0, 0), palette(0, 480))->width == 8);
}

SH_TEST(replacement_store_write_to_video_memory_drops_what_it_touches) {
    FakeSink sink;
    tx::ReplacementStore store(sink);
    const tx::PictureKey a{704, 0, 64, 256, 0, 480, 4};     // palette cells 0..15 of row 480
    const tx::PictureKey b{768, 0, 64, 256, 16, 480, 4};    // palette cells 16..31
    const tx::PictureKey c{832, 0, 128, 256, 256, 490, 8};  // palette cells 256..511 of row 490
    SH_CHECK(store.put(a, pixels(2, 2, 1), 2, 2, false));
    SH_CHECK(store.put(b, pixels(2, 2, 2), 2, 2, false));
    SH_CHECK(store.put(c, pixels(2, 2, 3), 2, 2, false));

    store.cells_written(0, 0, 320, 240);  // elsewhere
    SH_CHECK(store.picture_count() == 3);
    store.cells_written(767, 255, 1, 1);  // the last cell of a's rectangle
    SH_CHECK(store.picture_count() == 2);
    SH_CHECK(!store.find(page(11, 0, 0), palette(0, 480)));
    store.cells_written(31, 480, 1, 1);  // the last palette cell of b's row
    SH_CHECK(store.picture_count() == 1);
    store.cells_written(255, 490, 1, 1);   // one cell left of c's palette
    store.cells_written(256, 489, 16, 1);  // the row above it
    SH_CHECK(store.picture_count() == 1);
    store.cells_written(500, 490, 1, 1);  // inside the 256 cells of an 8-bit palette
    SH_CHECK(store.picture_count() == 0);
    SH_CHECK(sink.live.empty());
}

SH_TEST(replacement_store_pinned_pictures_stay) {
    FakeSink sink;
    tx::ReplacementStore store(sink);
    const tx::PictureKey key{704, 0, 64, 256, 0, 480, 4};
    SH_CHECK(!store.pin(key, true));  // nothing there yet
    SH_CHECK(store.put(key, pixels(2, 2, 1), 2, 2, true));
    SH_CHECK(store.pin(key, true));
    store.cells_written(0, 0, 1024, 512);
    SH_CHECK(store.picture_count() == 1);
    // Putting the same key again keeps the pin.
    SH_CHECK(store.put(key, pixels(2, 2, 2), 2, 2, true));
    store.cells_written(0, 0, 1024, 512);
    SH_CHECK(store.picture_count() == 1);

    SH_CHECK(store.pin(key, false));
    store.cells_written(0, 0, 1024, 512);
    SH_CHECK(store.picture_count() == 0);
}

SH_TEST(replacement_store_rows_share_one_texture) {
    FakeSink sink;
    tx::ReplacementStore store(sink);
    const tx::PictureKey first{704, 0, 64, 256, 0, 480, 4};
    SH_CHECK(store.put_rows(first, 4, pixels(8, 8, 5), 8, 8, false));
    SH_CHECK(store.picture_count() == 4);
    SH_CHECK(sink.created == 1 && sink.live.size() == 1);
    const std::uint32_t shared = store.find(page(11, 0, 0), palette(0, 480))->texture;
    for (int row = 0; row < 4; ++row) SH_CHECK(store.find(page(11, 0, 0), palette(0, 480 + row))->texture == shared);
    SH_CHECK(!store.find(page(11, 0, 0), palette(0, 484)));

    // A row of its own does not change what the other rows show.
    tx::PictureKey second = first;
    second.palette_y = 481;
    SH_CHECK(store.put(second, pixels(8, 8, 7), 8, 8, false));
    SH_CHECK(store.picture_count() == 4 && sink.live.size() == 2);
    SH_CHECK(store.find(page(11, 0, 0), palette(0, 481))->texture != shared);
    SH_CHECK(sink.live.at(shared).pixels[0] == 5);

    // The shared texture lives until its last row goes.
    store.cells_written(0, 480, 16, 1);
    store.cells_written(0, 481, 16, 1);
    store.cells_written(0, 482, 16, 1);
    SH_CHECK(store.picture_count() == 1 && sink.live.count(shared) == 1);
    store.cells_written(0, 483, 16, 1);
    SH_CHECK(sink.live.empty());

    // A picture without a palette is put once, whatever the row count.
    SH_CHECK(store.put_rows({640, 0, 256, 256, -1, -1, 16}, 16, pixels(8, 8, 5), 8, 8, false));
    SH_CHECK(store.picture_count() == 1);
}

SH_TEST(replacement_store_first_put_wins_and_removal_moves_the_last) {
    FakeSink sink;
    tx::ReplacementStore store(sink);
    // Three pictures a draw through page 11 with palette (0, 480) can all take: the first put wins.
    const tx::PictureKey a{704, 0, 16, 256, 0, 480, 4};
    const tx::PictureKey b{720, 0, 16, 256, 0, 480, 4};
    const tx::PictureKey c{736, 0, 16, 256, 0, 480, 4};
    SH_CHECK(store.put(a, pixels(1, 1, 1), 1, 1, false));
    SH_CHECK(store.put(b, pixels(2, 1, 2), 2, 1, false));
    SH_CHECK(store.put(c, pixels(3, 1, 3), 3, 1, false));
    SH_CHECK(store.find(page(11, 0, 0), palette(0, 480))->width == 1);
    // Without the first, the last takes its place in the order.
    store.cells_written(704, 0, 1, 1);
    SH_CHECK(store.find(page(11, 0, 0), palette(0, 480))->width == 3);
    SH_CHECK(store.find(page(11, 0, 0), palette(0, 480))->offset_x == (704 - 736) * 4);
    store.cells_written(736, 0, 1, 1);
    SH_CHECK(store.find(page(11, 0, 0), palette(0, 480))->width == 2);
}

SH_TEST(replacement_store_full_and_failed_textures) {
    FakeSink sink;
    tx::ReplacementStore store(sink);
    for (int i = 0; i < static_cast<int>(tx::ReplacementStore::kPictures); ++i) {
        SH_CHECK(store.put({704, 0, 64, 256, (i % 60) * 16, 256 + i / 60, 4}, pixels(1, 1, 1), 1, 1, false));
    }
    SH_CHECK(!store.put({704, 0, 64, 256, 0, 300, 4}, pixels(1, 1, 1), 1, 1, false));
    SH_CHECK(store.picture_count() == tx::ReplacementStore::kPictures);
    // A known key is still replaced when the store is full.
    SH_CHECK(store.put({704, 0, 64, 256, 0, 256, 4}, pixels(2, 2, 1), 2, 2, false));

    FakeSink broken;
    broken.refuse = true;
    tx::ReplacementStore empty(broken);
    SH_CHECK(!empty.put({704, 0, 64, 256, 0, 300, 4}, pixels(1, 1, 1), 1, 1, false));
    SH_CHECK(!empty.put_rows({704, 0, 64, 256, 0, 300, 4}, 4, pixels(1, 1, 1), 1, 1, false));
    SH_CHECK(!empty.put_sheet(3, pixels(1, 1, 1), 1, 1, 1, 1));
    SH_CHECK(empty.picture_count() == 0);
    // Too few pixels for the size.
    FakeSink sink2;
    tx::ReplacementStore other(sink2);
    SH_CHECK(!other.put({0, 300, 64, 100, 0, 400, 4}, pixels(1, 1, 1), 2, 2, false));
    SH_CHECK(sink2.created == 0);
}

SH_TEST(replacement_store_sheets) {
    FakeSink sink;
    tx::ReplacementStore store(sink);
    using Store = tx::ReplacementStore;
    // The word of sheet 255: row 512 + 3 * 16 in bits 6..15, 63 in bits 0..5.
    SH_CHECK(Store::sheet_palette(255) == static_cast<std::uint16_t>(((512 + 48) << 6) | 63));
    SH_CHECK(Store::sheet_palette(0) == static_cast<std::uint16_t>(512 << 6));

    SH_CHECK(!store.find(page(0, 0, 0), Store::sheet_palette(255)));
    SH_CHECK(store.put_sheet(255, pixels(256, 256, 4), 256, 256, 128, 128));
    const auto found = store.find(page(0, 0, 0), Store::sheet_palette(255));
    SH_CHECK(found.has_value());
    if (found) {
        SH_CHECK(found->native_w == 128 && found->native_h == 128);
        SH_CHECK(found->offset_x == 0 && found->offset_y == 0);
        SH_CHECK(found->width == 256 && found->height == 256);
        SH_CHECK(!sink.live.at(found->texture).point_sampled);  // larger than its texel space: filtered
    }
    // Any page with a palette, any row of the sheet's group; never a direct-colour page.
    SH_CHECK(store.find(page(9, 1, 1), static_cast<std::uint16_t>(Store::sheet_palette(255) + (5 << 6))).has_value());
    SH_CHECK(!store.find(page(9, 1, 2), Store::sheet_palette(255)));
    SH_CHECK(!store.find(page(0, 0, 0), Store::sheet_palette(254)));

    // A sheet of the size of its texel space is point sampled; putting it again reuses the texture.
    SH_CHECK(store.put_sheet(7, pixels(64, 64, 1), 64, 64, 64, 64));
    const std::uint32_t id = store.find(page(0, 0, 0), Store::sheet_palette(7))->texture;
    SH_CHECK(sink.live.at(id).point_sampled);
    SH_CHECK(store.put_sheet(7, pixels(64, 64, 2), 64, 64, 64, 64));
    SH_CHECK(store.find(page(0, 0, 0), Store::sheet_palette(7))->texture == id);

    SH_CHECK(!store.put_sheet(Store::kSheets, pixels(1, 1, 1), 1, 1, 1, 1));
    SH_CHECK(!store.put_sheet(-1, pixels(1, 1, 1), 1, 1, 1, 1));
    // The pictures are untouched by sheets and by writes meant for pictures.
    store.cells_written(0, 0, 1024, 512);
    SH_CHECK(store.find(page(0, 0, 0), Store::sheet_palette(255)).has_value());
}
