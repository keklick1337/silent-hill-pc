// The image step after a TIM read (engine/loader/queue/image_step): placement, pool slots, mod
// pictures, and pack rows under the per-update budget.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/loader/queue/image_step.h"

#include "framework/sh_test.h"

namespace ld = sh::loader;

namespace {

std::string rect(const ld::VramRect& r) {
    return std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + "x" + std::to_string(r.h);
}

class Host final : public ld::ImageStepHost {
public:
    std::vector<std::string> log;
    std::optional<std::string> pending;
    bool rows = false, whole = false, pack = false;
    ld::PackRow pack_answer = ld::PackRow::Canvas;

    void upload_pixels(const ld::VramRect& r) override { log.push_back("pixels " + rect(r)); }
    void upload_palette(const ld::VramRect& r) override { log.push_back("palette " + rect(r)); }
    void trace(const ld::VramRect&, int bpp, const ld::VramRect&, bool, bool pool) override {
        log.push_back("trace " + std::to_string(bpp) + (pool ? " pool" : ""));
    }
    bool dump_enabled() override { return false; }
    void dump(const ld::VramRect&, int, int, int) override {}
    void nearest(bool on) override { log.push_back(on ? "nearest on" : "nearest off"); }
    std::optional<std::string> pending_picture() override { return std::exchange(pending, std::nullopt); }
    bool per_row(const std::string&) override { return rows; }
    void note(const std::string& line) override { log.push_back("note " + line); }
    void slot_palette(int slot, int, int) override { log.push_back("slot palette " + std::to_string(slot)); }
    bool slot_whole(int slot, const std::string&, int w, int h) override {
        log.push_back("slot whole " + std::to_string(slot) + " " + std::to_string(w) + "x" + std::to_string(h));
        return whole;
    }
    void slot_disc(int slot, int, int) override { log.push_back("slot disc " + std::to_string(slot)); }
    bool slot_row(int, const std::string&, int row, int, int) override {
        log.push_back("slot row " + std::to_string(row));
        return true;
    }
    bool pack_entries() override { return pack; }
    void slot_pack(int slot, const ld::VramRect&, int, int rows_, int, int, int) override {
        log.push_back("slot pack " + std::to_string(slot) + " rows " + std::to_string(rows_));
    }
    void vram_written(const ld::VramRect& r) override { log.push_back("written " + rect(r)); }
    void invalidate(const ld::VramRect& r) override { log.push_back("invalidate " + rect(r)); }
    bool vram_row(const std::string&, int row, const ld::VramRect&, int, int y, int) override {
        log.push_back("row " + std::to_string(row) + " at " + std::to_string(y));
        return true;
    }
    bool vram_whole(const std::string&, const ld::VramRect&, int, int, int rows_, int) override {
        log.push_back("whole rows " + std::to_string(rows_));
        return whole;
    }
    bool pack_budget_exceeded() override { return false; }
    ld::PackRow pack_row(int row, const ld::VramRect&, int, int, int, int, bool* built) override {
        log.push_back("pack " + std::to_string(row));
        *built = true;
        return pack_answer;
    }
    void report_uncovered(std::uint32_t missing, int matched) override {
        log.push_back("uncovered " + std::to_string(missing) + " " + std::to_string(matched));
    }
};

ld::ImageRequest request() {
    ld::ImageRequest r;
    r.key = 1;
    r.ticket = 7;
    r.pixels = {640, 0, 64, 256};
    r.palette = ld::VramRect{0, 480, 16, 3};
    r.mode = 0x8;  // 4bpp with a palette
    return r;
}

}  // namespace

SH_TEST(image_step_uploads_where_the_file_says) {
    Host h;
    ld::ImageStep step;
    step.refill();
    SH_CHECK(step.run(h, request()));
    SH_CHECK((h.log == std::vector<std::string>{"pixels 640,0 64x256", "palette 0,480 16x3", "trace 4", "nearest off",
                                                "written 640,0 64x256", "invalidate 0,480 16x3", "nearest off"}));
}

SH_TEST(image_step_target_moves_picture_and_palette) {
    Host h;
    ld::ImageStep step;
    ld::ImageRequest r = request();
    r.target = {true, 32, 16, 0x1A, 256, 500};  // page x 10*64, y bit set
    step.refill();
    SH_CHECK(step.run(h, r));
    SH_CHECK(h.log[0] == "pixels 672,272 64x256");
    SH_CHECK(h.log[1] == "palette 256,500 16x3");
}

SH_TEST(image_step_pool_slot_skips_vram) {
    Host h;
    h.pack = true;
    ld::ImageStep step;
    ld::ImageRequest r = request();
    r.target = {true, 0, 0, 0, 48, 512 + 16};  // slot 1*64 + 3
    step.refill();
    SH_CHECK(step.run(h, r));
    SH_CHECK((h.log == std::vector<std::string>{"trace 4 pool", "nearest off", "slot palette 67", "slot disc 67",
                                                "slot pack 67 rows 3", "nearest off"}));
}

SH_TEST(image_step_pool_slot_whole_mod_picture) {
    Host h;
    h.pending = "CHARA/X.TIM";
    h.whole = true;
    ld::ImageStep step;
    ld::ImageRequest r = request();
    r.target = {true, 0, 0, 0, 0, 512};
    step.refill();
    SH_CHECK(step.run(h, r));
    SH_CHECK(h.log[3] == "slot whole 0 256x256");  // 4bpp: four texels per halfword
    SH_CHECK(h.log.size() == 5);
}

SH_TEST(image_step_mod_rows_in_vram) {
    Host h;
    h.pending = "CHARA/X.TIM";
    h.rows = true;
    ld::ImageStep step;
    step.refill();
    SH_CHECK(step.run(h, request()));
    SH_CHECK(h.log[6] == "row 0 at 480" && h.log[8] == "row 2 at 482");
    SH_CHECK(h.log[9] == "note [LOOSE/HIRES] CHARA/X.TIM: 3 loose CLUT-row override(s)");
}

SH_TEST(image_step_pack_rows_resume_under_budget) {
    Host h;
    h.pack = true;
    h.pack_answer = ld::PackRow::Miss;
    ld::ImageStep step;
    step.refill();
    SH_CHECK(!step.run(h, request()));  // two rows built, the third waits
    SH_CHECK(h.log.back() == "nearest off" && h.log[h.log.size() - 2] == "pack 1");
    h.log.clear();
    step.refill();
    SH_CHECK(step.run(h, request()));  // only the owed row; nothing uploaded again
    SH_CHECK((h.log == std::vector<std::string>{"nearest off", "pack 2", "uncovered 4 0", "nearest off"}));
    h.log.clear();
    step.refill();
    ld::ImageRequest other = request();
    other.ticket = 8;  // a different request: starts over
    SH_CHECK(!step.run(h, other));
    SH_CHECK(h.log[0] == "pixels 640,0 64x256");
}
