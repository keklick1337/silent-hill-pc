// SPDX-License-Identifier: GPL-3.0-or-later
// The paper map screen (engine/screens/paper_map.h): page files and placement from the shipped map_screen.json, the
// arrow placement on a few known spots, the page picture tiles, the marks, the widescreen view and the game state
// steps against a recording host.

#include <filesystem>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "engine/screens/paper_map.h"
#include "framework/sh_test.h"

using namespace sh::screens;
namespace draw2d = sh::draw2d;

namespace {

const char* const kPages[25] = {
    "OtherPlaces",   "OldTown",       "FogCentralTown", "AltCentralTown", "ResortTown",
    "FogSchoolBF",   "FogSchool1F",   "FogSchool2F",    "FogSchoolRF",    "AltSchoolBF",
    "AltSchool1F",   "AltSchool2F",   "AltSchoolRF",    "FogSewer1F",     "FogSewer2F",
    "AltSewer",      "FogHospitalBF", "FogHospital1F",  "FogHospital2F",  "FogHospital3F",
    "AltHospitalBF", "AltHospital1F", "AltHospital2F",  "AltHospital3F",  "PaperMap24"};

// The disc files as the base game numbers them (MP_* from 1896, MR_* from 1910).
const char* const kFiles[19] = {"TIM/MP_0TOWN.TIM", "TIM/MP_BSTLN.TIM", "TIM/MP_DRN1F.TIM", "TIM/MP_DRN2F.TIM",
                                "TIM/MP_DRNBK.TIM", "TIM/MP_HSP1F.TIM", "TIM/MP_HSP2F.TIM", "TIM/MP_HSP3F.TIM",
                                "TIM/MP_HSPBF.TIM", "TIM/MP_RSORT.TIM", "TIM/MP_SCL1F.TIM", "TIM/MP_SCL2F.TIM",
                                "TIM/MP_SCLBF.TIM", "TIM/MP_SCLRF.TIM", "TIM/MR_0TOWN.TIM", "TIM/MR_BSTLN.TIM",
                                "TIM/MR_DRN.TIM",   "TIM/MR_HSP.TIM",   "TIM/MR_SCL.TIM"};

PaperMapBook shipped_book() {
    auto loaded = load_map_screen(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {}, nullptr);
    SH_CHECK(static_cast<bool>(loaded));
    PaperMapNames names;
    names.page = [](const std::string& n) {
        for (int i = 0; i < 25; ++i)
            if (n == kPages[i]) return i;
        return -1;
    };
    names.file = [](const std::string& n) {
        for (int i = 0; i < 19; ++i)
            if (n == kFiles[i]) return 1896 + i;
        return -1;
    };
    names.flag = [](const std::string& n) {  // a number of its own for each flag name, from 1000
        static std::map<std::string, int> numbers;
        return numbers.emplace(n, 1000 + static_cast<int>(numbers.size())).first->second;
    };
    return PaperMapBook::build(std::move(loaded.value()), names);
}

constexpr std::int32_t kCell = 40 * 4096;

class RecordingHost final : public PaperMapHost {
public:
    std::vector<std::string> calls;
    std::vector<std::pair<PaperMapLayer, draw2d::Command>> drawn;
    int step_ = 0;
    int page = 6;
    bool from_inventory = false;
    bool fade_finished = false;
    PaperMapInput pressed;

    int step() override { return step_; }
    void set_step(int s) override {
        step_ = s;
        calls.push_back("step " + std::to_string(s));
    }
    bool opened_from_inventory() override { return from_inventory; }
    bool opened_from_game() override { return !from_inventory; }
    void set_return_to_game() override { calls.push_back("return_to_game"); }
    void return_to_previous() override { calls.push_back("previous"); }
    void frame_upkeep() override {}
    void enter_display() override { calls.push_back("enter_display"); }
    void leave_display() override { calls.push_back("leave_display"); }
    void set_vblank_interval(int v) override { calls.push_back("vblank " + std::to_string(v)); }
    void store_frame() override { calls.push_back("store"); }
    void restore_frame() override { calls.push_back("restore"); }
    void flush_frame_lists() override { calls.push_back("flush"); }
    void reload_map_item_textures() override { calls.push_back("items"); }
    void draw_now(const draw2d::Command&) override { ++dims; }
    void wait_vblank() override {}
    void submit(PaperMapLayer layer, const draw2d::Command& c) override { drawn.emplace_back(layer, c); }
    float wide_factor() override { return 1.0f; }
    void note_wide_frame() override {}
    void pause_music() override {}
    int saved_page() override { return page; }
    void load_page_image(int f) override { calls.push_back("image " + std::to_string(f)); }
    void load_marking_texture(int f) override { calls.push_back("marks " + std::to_string(f)); }
    void seek_file(int f) override { calls.push_back("seek " + std::to_string(f)); }
    void finish_loads() override { calls.push_back("finish"); }
    void pump_file_queue() override {}
    void wait_file_queue() override {}
    PaperMapImage page_image() override { return {1, 5, 16, 224, 15}; }
    std::array<std::int16_t, 2> marking_palette() override { return {480, 255}; }
    void stop_radio() override { calls.push_back("radio"); }
    void play_map_sound() override { calls.push_back("sound"); }
    void fade_in() override { calls.push_back("fade_in"); }
    void fade_out() override { calls.push_back("fade_out"); }
    bool fade_idle() override { return true; }
    bool fade_done() override { return fade_finished; }
    void reset_fade_step() override { calls.push_back("reset_fade"); }
    PaperMapInput input() override { return pressed; }
    bool has_page(int p) override { return p == 7; }
    bool flag(int) override { return true; }
    PaperMapPlayer player() override { return {kCell * 2 + kCell / 2, kCell / 2, 0, "map1_s01"}; }
    std::int32_t sin(std::int32_t) override { return 0; }
    std::int32_t cos(std::int32_t) override { return 4096; }
    int dims = 0;
};

bool has(const std::vector<std::string>& calls, const std::string& c) {
    for (const std::string& s : calls)
        if (s == c) return true;
    return false;
}

}  // namespace

SH_TEST(paper_map_page_files_and_links) {
    const PaperMapBook book = shipped_book();
    // The original's relative tables: picture index per page and marking texture index (-1 = none).
    const int image[24] = {0, 0, 1, 1, 9, 12, 10, 11, 13, 12, 10, 11, 13, 2, 3, 4, 8, 5, 6, 7, 8, 5, 6, 7};
    const int marks[24] = {-1, 0, 1, 1, 1, 4, 4, 4, 4, 4, 4, 4, 4, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3};
    for (int p = 0; p < 24; ++p) {
        SH_CHECK(book.page(p)->image_file == 1896 + image[p]);
        SH_CHECK(book.page(p)->marking_file == (marks[p] < 0 ? -1 : 1910 + marks[p]));
    }
    SH_CHECK(book.page(24)->image_file == -1 && book.page(25) == nullptr && book.page(-1) == nullptr);
    SH_CHECK(book.page(6)->up == 7 && book.page(6)->down == 5 && book.page(1)->up == 0);
}

SH_TEST(paper_map_player_placement) {
    const PaperMapBook book = shipped_book();
    // School: the anchor of the cell plus the offset inside it (cell (2, 0) of the school grid, centre of the cell).
    PaperMapPlayer p{kCell * 2 + kCell / 2, kCell / 2, 100, "map1_s01"};
    const auto school = paper_map_player_point(book, 6, p);
    SH_CHECK(school && school->heading == 100);
    // Sewer 1F: x from -z >> 11, z from -x >> 11 plus 40, turned -90 degrees.
    p = {4096 * 10, -4096 * 20, 0, "map1_s03"};
    const auto sewer = paper_map_player_point(book, 13, p);
    SH_CHECK(sewer && sewer->x == 40 && sewer->z == 40 - 20 && sewer->heading == -1024);
    // Old Town in map0_s01: a fixed point; in a map the page has no rule for, no arrow.
    p.map = "map0_s01";
    const auto town = paper_map_player_point(book, 1, p);
    SH_CHECK(town && town->x == 33 && town->z == -28);
    p.map = "map3_s00";
    SH_CHECK(!paper_map_player_point(book, 1, p));
    SH_CHECK(!paper_map_player_point(book, 0, p));
    // A negative x borrows from z in the packed form, as in the original.
    SH_CHECK(paper_map_packed_point(PaperMapPoint{-1, 5, 0}) == (5 << 16) - 1);
    SH_CHECK(paper_map_packed_point(std::nullopt) == 0);
    SH_CHECK(paper_map_fold_heading(2049) == -2047 && paper_map_fold_heading(2048) == 2048);
}

SH_TEST(paper_map_tiles_and_marks) {
    const PaperMapBook book = shipped_book();
    // The whole page: 3 x 2 tiles covering -160..160 by -240..240.
    const auto tiles = paper_map_tiles({0, 0, 4096}, {1, 5, 0, 224, 15}, 128, nullptr);
    SH_CHECK(tiles.size() == 6);
    SH_CHECK(tiles[0].xy[0] == (draw2d::Point{-160, -240}) && tiles[0].color[0] == (draw2d::Rgb{128, 128, 128}));
    SH_CHECK(tiles[5].xy[3].x == 159 && tiles[5].xy[3].y == 238);
    // No marking texture on page 0; all marks of the school 1F page are textured translucent quads.
    SH_CHECK(!paper_map_marks(book, 0, 1, 0, 0, {0, 0, 4096}, 0, 0, [](int) { return true; }));
    const auto all = paper_map_marks(book, 6, 1, 0, 0, {0, 0, 4096}, 480, 255, [](int) { return true; });
    SH_CHECK(all && !all->empty() && (*all)[0].textured && (*all)[0].shade.translucent);
    const auto none = paper_map_marks(book, 6, 1, 0, 0, {0, 0, 4096}, 480, 255, [](int) { return false; });
    SH_CHECK(none && none->empty());
    // One mark: a flag that is no half of the page's marks draws nothing but still answers true.
    const auto one = paper_map_marks(book, 6, 2, book.page(6)->mark_flags[0][0] - 10, 77, {0, 0, 4096}, 0, 0,
                                     [](int) { return true; });
    SH_CHECK(one && one->empty());
    // The first half of mark i draws that half; the second half of mark i draws the second half of mark i + 1 (the
    // original halves the flag's distance from the page's first flag, rounding up).
    const PaperMapPage* school = book.page(6);
    SH_CHECK(school != nullptr && school->mark_flags.size() >= 3);
    const auto first_half = paper_map_marks(book, 6, 2, school->mark_flags[1][0], 77, {0, 0, 4096}, 0, 0,
                                            [](int) { return true; });
    const auto from_mark = [&](int mark, int half) {
        const MapMark& m = book.data().markings[static_cast<std::size_t>(school->marks)].marks[static_cast<std::size_t>(mark)];
        return m.sprites[static_cast<std::size_t>(half)] != 0;
    };
    SH_CHECK(first_half && first_half->size() == (from_mark(1, 0) ? 1u : 0u));
    const auto second_half = paper_map_marks(book, 6, 2, school->mark_flags[0][1], 77, {0, 0, 4096}, 0, 0,
                                             [](int) { return true; });
    SH_CHECK(second_half && second_half->size() == (from_mark(1, 1) ? 1u : 0u));
}

SH_TEST(paper_map_wide_view) {
    // 4:3: unchanged. Wide enough for the whole page: centred, the tiles span the full page.
    SH_CHECK(paper_map_wide_view(40, 2048, 1.0f).left == 40 && !paper_map_wide_view(40, 2048, 1.0f).span.on);
    const PaperMapWideView full = paper_map_wide_view(0, 4096, 1.5f);
    SH_CHECK(full.span.on && full.span.left == 0 && full.span.right == 320 && full.left == 0);
    const PaperMapWideView half = paper_map_wide_view(0, 2048, 1.2f);
    SH_CHECK(half.span.on && half.span.left == 0 && half.span.right == 192 && half.left == 16);
    const PaperMapView v = paper_map_view(-1, 0, 4096);
    SH_CHECK(v.left == 0 && v.top == 0 && v.scale == 4096);
    SH_CHECK(paper_map_view(80 * 4096, 60 * 4096, 0).left == 80 && paper_map_view(0, 0, 0).scale == 2048);
}

SH_TEST(paper_map_screen_steps) {
    const PaperMapBook book = shipped_book();
    PaperMapScreen screen;
    RecordingHost host;
    screen.update(host, book);  // open
    SH_CHECK(host.step_ == 2 && has(host.calls, "store") && has(host.calls, "image 1906") && has(host.calls, "fade_in"));
    // Show: page picture, marks and the arrow; Up turns to the page above (the player has page 7).
    host.calls.clear();
    host.pressed.up = true;
    screen.update(host, book);
    SH_CHECK(host.step_ == 3 && has(host.calls, "seek 1907") && has(host.calls, "fade_out"));
    bool page_drawn = false;
    for (const auto& [layer, c] : host.drawn) page_drawn = page_drawn || layer == PaperMapLayer::Page;
    SH_CHECK(page_drawn);
    host.pressed = {};
    host.fade_finished = true;
    screen.update(host, book);  // turn done
    SH_CHECK(host.step_ == 1);
    host.calls.clear();
    screen.update(host, book);  // load: same marking texture, so only the picture
    SH_CHECK(host.step_ == 2 && has(host.calls, "image 1907") && !has(host.calls, "marks 1914"));
    // Close from the game: fade out, then restore and back.
    host.pressed.map = true;
    screen.update(host, book);
    SH_CHECK(host.step_ == 4);
    host.pressed = {};
    host.calls.clear();
    screen.update(host, book);
    SH_CHECK(has(host.calls, "restore") && has(host.calls, "leave_display") && has(host.calls, "previous"));
    // Close into the inventory: the dimming loop runs at once.
    RecordingHost inv;
    inv.from_inventory = true;
    PaperMapScreen s2;
    s2.update(inv, book);
    inv.pressed.map = true;  // the map button does not close it from the inventory
    s2.update(inv, book);
    SH_CHECK(inv.step_ == 2);
    inv.pressed.cancel = true;
    s2.update(inv, book);
    SH_CHECK(inv.step_ == 4 && inv.dims == 2 * kPaperMapCloseDimFrames && has(inv.calls, "reset_fade"));
}
