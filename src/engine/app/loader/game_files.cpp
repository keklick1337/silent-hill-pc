// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's file queue (game_files.h): sh::loader::FileQueue with the game's numbers, the
// read source (cutscene player, active mods, disc sectors; sh::loader::FileSource), the image step after a TIM read
// (VRAM uploads, mod pictures, HD replacements, fonts; sh::loader::ImageStep) and the animation bind.

#include "engine/app/loader/game_files.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

#include "engine/app/game_view.h"
#include "engine/app/loader/game_files_view.h"
#include "engine/app/loader/upscaled_pictures.h"
#include "engine/debug/dbg_trace.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/loader/queue/file_queue.h"
#include "engine/loader/queue/file_source.h"
#include "engine/loader/queue/image_step.h"
#include "engine/log/sh_log.h"
#include "engine/memory/work_buffers.h"
#include "engine/mods/asset_files_c.h"
#include "engine/mods/mods_c.h"
#include "engine/platform/disc.h"
#include "engine/screens/screens_view.h"
#include "engine/timing/vblank.h"
#include "render/geometry/oversized_models.h"
#include "render/gpu/renderer.h"
#include "render/textures/replacements.h"

// FASTLOAD (config cd_fast_load, on by default; console FASTLOAD): blocking loads are drained at storage speed
// instead of a vertical blank per queue step.
int g_PcFastBlockingLoads = 0;

extern "C" {
void Lm_LooseValidateDiag(const void* raw, unsigned int fileSize, const char* name);
}

namespace sh::loader {
namespace {

constexpr int kSlots = 32;
constexpr std::uint8_t kPlaceFromFile = 0xFF;

// What an animation read carries in its extra bytes.
struct AnimPlace {
    std::uint32_t slot;
    std::uint32_t chara_id;
    void* bones;
};

QueueSpec game_spec() {
    QueueSpec s;
    s.slots = kSlots;
    s.sector_bytes = 2048;
    s.retry_ticks = 8;
    s.retry_rounds = 9;
    s.wait_slack_vblanks = 600;
    return s;
}

FileQueue& queue() {
    static FileQueue q(game_spec());
    return q;
}

DiscFiles& table() { return disc_files(); }
bool file_valid(std::int32_t file) { return file >= 0 && file < table().count(); }
std::string file_name(std::int32_t file) { return file_valid(file) ? table().name(file) : std::string(); }

// Reads a whole mod file (64 MB cap); empty with the failing step logged.
std::optional<std::string> slurp(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        SH_DBG("[LOOSE/WARN] %s: fopen failed (errno=%d %s)", path, errno, std::strerror(errno));
        return std::nullopt;
    }
    long size = -1;
    if (std::fseek(f, 0, SEEK_END) == 0) {
        size = std::ftell(f);
        if (std::fseek(f, 0, SEEK_SET) != 0) size = -1;
    }
    std::optional<std::string> out;
    if (size <= 0) {
        SH_DBG("[LOOSE/WARN] %s: invalid size %ld", path, size);
    } else if (size >= 64 * 1024 * 1024) {
        SH_DBG("[LOOSE/WARN] %s: too large (%ld bytes, cap 64MB)", path, size);
    } else {
        std::string bytes(static_cast<std::size_t>(size), '\0');
        if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) {
            SH_DBG("[LOOSE/WARN] %s: short read of %ld bytes", path, size);
        } else {
            out = std::move(bytes);
        }
    }
    std::fclose(f);
    return out;
}

const unsigned char* bytes_of(const std::string& s) { return reinterpret_cast<const unsigned char*>(s.data()); }

// A mod's replacement of a TIM's whole picture, or an oversized TIM at the disc path handed to the decoder.
bool mod_whole_picture(const char* disc_path, char* out, std::size_t out_size) {
    return ShModAsset_TimWhole(disc_path, out, out_size, nullptr) || ShModAsset_Find(disc_path, out, out_size);
}

// The read of one queued file: the cutscene player, the active mods, the oversized destinations, the disc sectors.
class GameSourceHost final : public FileSourceHost {
public:
    FILE* file = nullptr;

    bool serve_cutscene(const SourceRequest& r) override { return view::cutscene_file_read(r.dest, r.file); }
    bool mods_active() const override { return ShMods_ActiveCount() > 0; }
    bool mod_has_picture(const std::string& path) const override { return ShModAsset_TimHasImage(path.c_str()) != 0; }
    bool mod_open(const std::string& path, std::int64_t* size) override {
        file = ShModAsset_Open(path.c_str(), "rb");
        if (file == nullptr) return false;
        *size = -1;
        if (std::fseek(file, 0, SEEK_END) == 0) {
            const long end = std::ftell(file);
            if (std::fseek(file, 0, SEEK_SET) == 0) *size = end;
        }
        return true;
    }
    std::size_t mod_read(void* dest, std::size_t max) override {
        if (const std::optional<std::size_t> room = sh::memory::work_buffers().room(dest); room && max > *room) {
            SH_ERR("[FSQ] a mod file of up to %zu bytes does not fit its work buffer (%zu bytes); clipped", max, *room);
            max = *room;
        }
        const std::size_t got = std::fread(dest, 1, max, file);
        mod_close();
        return got;
    }
    void mod_close() override {
        if (file != nullptr) std::fclose(file);
        file = nullptr;
    }
    std::size_t dest_capacity(const void* dest) const override {
        return sh::models::oversized_models().capacity(dest);
    }
    void mod_bytes_read(const SourceRequest& r, std::int64_t size) override {
        // Log-only check of a mod's model file; an oversized one was checked before it got its buffer.
        if (sh::models::oversized_models().capacity(r.dest) == 0)
            Lm_LooseValidateDiag(r.dest, static_cast<unsigned int>(size), r.path.c_str());
    }
    void read_sector(std::uint32_t sector, void* dest) override {
        // A whole-file read lands in a work buffer sector by sector: one that runs past the buffer's end stops there.
        if (const std::optional<std::size_t> room = sh::memory::work_buffers().room(dest); room && *room < FileSource::kSectorBytes) {
            SH_ERR("[FSQ] disc sector %u does not fit its work buffer (%zu bytes left); not read", sector, *room);
            return;
        }
        sh::disc::read_sector(static_cast<int>(sector), sh::disc::SectorKind::Data, dest);
    }
    void trace(const SourceRequest& r, const char* from) override { DbgTrace_FileRead(r.file, from); }
    void note(const std::string& line) override { SH_DBG("%s", line.c_str()); }
};

FileSource& source() {
    static FileSource s;
    return s;
}

ImageStep& image_step() {
    static ImageStep step(ImageSpec{sh::textures::ReplacementStore::kSheetPaletteRow,
                                    sh::textures::ReplacementStore::kSheetRowGroup, 16, 2});
    return step;
}

// A TIM in memory, without a copy: the flags word (bit 3: a palette block comes first), then each block's byte
// length, rectangle (x, y, w, h halfwords) and words.
struct TimView {
    std::uint32_t mode = 0;
    const std::int16_t* palette_rect = nullptr;
    std::uint16_t* palette = nullptr;
    const std::int16_t* pixel_rect = nullptr;
    void* pixels = nullptr;
};

bool read_tim(void* data, TimView* out) {
    auto* w = static_cast<std::uint32_t*>(data);
    if (w == nullptr || (w[0] & 0xFF) != 0x10) return false;
    out->mode = w[1];
    w += 2;
    if (out->mode & 0x8) {
        out->palette_rect = reinterpret_cast<const std::int16_t*>(&w[1]);
        out->palette = reinterpret_cast<std::uint16_t*>(&w[3]);
        w += w[0] >> 2;
    }
    out->pixel_rect = reinterpret_cast<const std::int16_t*>(&w[1]);
    out->pixels = &w[3];
    return true;
}

void vram_upload(const VramRect& r, const void* words) {
    if (sh::gpu::Renderer* gpu = sh::gpu::renderer()) {
        gpu->load_image(sh::gpu::Rect{static_cast<std::int16_t>(r.x), static_cast<std::int16_t>(r.y),
                                      static_cast<std::int16_t>(r.w), static_cast<std::int16_t>(r.h)},
                        static_cast<const std::uint16_t*>(words));
    }
}

class GameImageHost final : public ImageStepHost {
public:
    std::int32_t file = -1;
    TimView tim;

    void upload_pixels(const VramRect& r) override { vram_upload(r, tim.pixels); }
    void upload_palette(const VramRect& r) override { vram_upload(r, tim.palette); }
    void trace(const VramRect& p, int bpp, const VramRect& c, bool has_palette, bool pool) override {
        DbgTrace_TimLoaded(file, p.x, p.y, p.w, p.h, bpp, c.x, c.y, has_palette ? c.w : 0, c.h, pool);
    }
    bool dump_enabled() override { return false; }
    void dump(const VramRect&, int, int, int) override {}
    void nearest(bool on) override { point_sampled_ = on; }
    std::optional<std::string> pending_picture() override { return source().pending().pop(file); }
    bool per_row(const std::string& path) override { return ShModAsset_TimPerRow(path.c_str()) != 0; }
    void note(const std::string& line) override { SH_DBG("%s", line.c_str()); }

    // A picture read to a virtual slot is uploaded nowhere and nothing samples it: the scene draws the models that
    // name it from the imported files (a mod's picture of it reaches them there, by its path under convert/).
    void slot_palette(int, int, int) override {}
    bool slot_whole(int, const std::string&, int, int) override { return false; }
    void slot_disc(int, int, int) override {}
    bool slot_row(int, const std::string&, int, int, int) override { return false; }
    void slot_pack(int, const VramRect&, int, int, int, int, int) override {}

    void vram_written(const VramRect& p) override {
        view::pool_write_probe(p.x, p.y, p.w, p.h);
        sh::textures::replacements().cells_written(p.x, p.y, p.w, p.h);
    }
    void invalidate(const VramRect& r) override { sh::textures::replacements().cells_written(r.x, r.y, r.w, r.h); }
    bool vram_row(const std::string& path, int row, const VramRect& p, int palette_x, int palette_y, int bpp) override {
        char found[1024];
        int dds = 0;  // a BC7 .dds row wins over a .png one
        if (!ShModAsset_TimRow(path.c_str(), row, found, sizeof(found), &dds)) return false;
        const auto picture = read(found);
        return picture && sh::textures::replacements().put(key_of(p, palette_x, palette_y, bpp), picture->pixels,
                                                           picture->width, picture->height, point_sampled_);
    }
    bool vram_whole(const std::string& path, const VramRect& p, int cx, int cy, int rows, int bpp) override {
        char whole[1024];
        if (!mod_whole_picture(path.c_str(), whole, sizeof(whole))) return false;
        const auto picture = read(whole);
        if (!picture) return false;
        SH_DBG("[TEXTURES] %s replaces the picture at (%d,%d %dx%d) under %d palette row(s) from (%d,%d), %d bits",
               whole, p.x, p.y, p.w, p.h, rows, cx, cy, bpp);
        return sh::textures::replacements().put_rows(key_of(p, cx, cy, bpp), rows, picture->pixels, picture->width,
                                                     picture->height, point_sampled_);
    }

    bool vram_upscaled(const VramRect& p, int palette_x, int palette_y, int bpp) override {
        if (!table().valid(file)) return false;
        return sh::app::put_upscaled_picture(table().path(file), key_of(p, palette_x, palette_y, bpp),
                                             stp_uniform(bpp));
    }

    // No pack rows: a mod replaces a picture by its name.
    bool pack_entries() override { return false; }
    bool pack_budget_exceeded() override { return false; }
    PackRow pack_row(int, const VramRect&, int, int, int, int, bool* built) override {
        *built = false;
        return PackRow::Miss;
    }
    void report_uncovered(std::uint32_t, int) override {}

private:
    static sh::textures::PictureKey key_of(const VramRect& p, int palette_x, int palette_y, int bpp) {
        return {p.x, p.y, p.w, p.h, palette_x, palette_y, bpp};
    }
    static std::optional<sh::formats::texture::RgbaImage> read(const char* path) {
        const std::optional<std::string> bytes = slurp(path);
        if (!bytes) return std::nullopt;
        return sh::textures::read_picture(path, {bytes_of(*bytes), bytes->size()});
    }

    // Whether the colours the picture is drawn with are all semi-transparent (STP bit) or all solid: palette row 0,
    // or the texels of a 16-bit picture. Word 0 is transparent and counts as neither.
    bool stp_uniform(int bpp) const {
        const std::uint16_t* words = nullptr;
        std::size_t count = 0;
        if (bpp <= 8) {
            if (tim.palette == nullptr || tim.palette_rect == nullptr) return true;
            words = tim.palette;
            count = static_cast<std::size_t>(std::max<int>(tim.palette_rect[2], 0));
        } else if (bpp == 16 && tim.pixel_rect != nullptr) {
            words = static_cast<const std::uint16_t*>(tim.pixels);
            count = static_cast<std::size_t>(std::max<int>(tim.pixel_rect[2], 0)) *
                    static_cast<std::size_t>(std::max<int>(tim.pixel_rect[3], 0));
        }
        int seen = 0;  // bit 0: a solid colour, bit 1: a semi-transparent one
        for (std::size_t i = 0; i < count; ++i)
            if (words[i] != 0) seen |= (words[i] & 0x8000) ? 2 : 1;
        return seen != 3;
    }

    bool point_sampled_ = false;  // gutterless glyph cells: the step holds it while a font's pictures are put
};

// The image step of a TIM read; `key` names the queue slot (with the ticket, the step to resume).
bool image_loaded(std::int32_t ticket, const QueueEntry& e) {
    GameImageHost host;
    host.file = e.file;
    if (!read_tim(e.dest, &host.tim)) return true;

    ImagePlace d;
    std::memcpy(&d, e.extra.data(), sizeof(d));
    ImageRequest r;
    r.key = static_cast<std::uintptr_t>(ticket & (kSlots - 1)) + 1;
    r.ticket = queue().after_ticket();
    r.valid_file = file_valid(e.file);
    r.font16 = r.valid_file && e.file == table().find("1ST/FONT16.TIM");
    r.font24 = r.valid_file && e.file == table().find("TIM/FONT24.TIM");
    r.target.place = d.u != kPlaceFromFile;
    r.target.u = d.u;
    r.target.v = d.v;
    r.target.page_high = d.page[1];
    r.target.palette_x = d.palette_x;
    r.target.palette_y = d.palette_y;
    const std::int16_t* pr = host.tim.pixel_rect;
    r.pixels = {pr[0], pr[1], pr[2], pr[3]};
    if (host.tim.palette != nullptr) {
        const std::int16_t* cr = host.tim.palette_rect;
        r.palette = VramRect{cr[0], cr[1], cr[2], cr[3]};
    }
    r.mode = static_cast<int>(host.tim.mode);
    return image_step().run(host, r);
}

bool anim_loaded(const QueueEntry& e) {
    AnimPlace a;
    std::memcpy(&a, e.extra.data(), sizeof(a));
    if (SH_LOG_ON(ANIM, SH_LOGLV_DEBUG)) {
        char name[48];
        SH_LOG_DEBUG(ANIM, "%s loaded for chara %d (anim slot %d)", DbgTrace_FileName(e.file, name, sizeof(name)),
                     static_cast<int>(a.chara_id), static_cast<int>(a.slot));
    }
    view::anim_file_loaded(static_cast<int>(a.slot), static_cast<int>(a.chara_id), e.dest, a.bones);
    return true;
}

// The extra bytes of a paced read (queue_pace): no after-load step reads them otherwise.
constexpr std::array<char, 8> kPaceMark = {'P', 'A', 'C', 'E', 'O', 'N', 'L', 'Y'};

bool paced(const QueueEntry& e) {
    return e.after == AfterLoad::None && std::memcmp(e.extra.data(), kPaceMark.data(), kPaceMark.size()) == 0;
}

class GameHost final : public FileQueueHost {
public:
    bool file_valid(std::int32_t file) override { return table().valid(file); }
    std::uint32_t file_bytes(std::int32_t file) override { return table().bytes(file); }
    // Reads address the file by sector directly; there is no drive head to position.
    bool locate(std::int32_t, const QueueEntry&) override { return true; }
    ReadStart start_read(std::int32_t, const QueueEntry& e) override {
        if (paced(e)) {
            return ReadStart::Issued;  // as a disc read; nothing is transferred
        }
        GameSourceHost host;
        SourceRequest r;
        r.file = e.file;
        r.path = table().path(e.file);  // the path mods replace files at
        r.table_bytes = static_cast<std::int32_t>(table().bytes(e.file));
        r.first_sector = table().first_sector(e.file);
        r.image = e.after == AfterLoad::Image;
        r.dest = e.data;
        switch (source().read(host, r)) {
            case Source::Cutscene: return ReadStart::Served;
            case Source::Mod: return ReadStart::Finished;
            case Source::Disc: break;
        }
        return ReadStart::Issued;
    }
    // Reads complete inside the start call.
    ReadPoll poll_read() override { return ReadPoll::Done; }
    bool reset_drive() override { return true; }
    // A paced read also tells the animation bank: what it held at the destination counts as overwritten, as before.
    void after_load_begin(std::int32_t, const QueueEntry& e) override { view::entity_file_loaded(e.file, e.data); }
    bool after_load(std::int32_t ticket, const QueueEntry& e) override {
        image_step().refill();
        switch (e.after) {
            case AfterLoad::Image: return image_loaded(ticket, e);
            case AfterLoad::Animation: return anim_loaded(e);
            case AfterLoad::None: break;
        }
        return true;
    }
};

GameHost s_host;

// A blocking wait: the fast drain first, then one vertical blank per update.
class GameWait final : public WaitHooks {
public:
    void before_wait() override { queue_drain_now(); }
    void wait_vblank() override { sh::timing::wait_vblank(); }
    void stuck(int waited, int limit) override {
        SH_DBG("[FSQ] WaitForEmpty STUCK after %d vsyncs (limit %d) - forcing queue empty (len=%d step=%d)", waited,
               limit, queue().length(), static_cast<int>(queue().read_step()));
        ShLog_Flush();
    }
};

std::int32_t enqueue(std::int32_t file, FileOp op, AfterLoad after, void* dest, const void* extra, std::size_t bytes) {
    FileRequest r;
    r.file = file;
    r.op = op;
    r.after = after;
    r.dest = dest;
    if (extra != nullptr) {
        RequestExtra x{};
        std::memcpy(x.data(), extra, bytes);
        r.extra = x;
    }
    const std::optional<std::int32_t> t = queue().enqueue(s_host, r);
    if (!t) {
        SH_DBG("[FSQ] REJECT enqueue fileIdx=%d op=%d postLoad=%d data=%p", static_cast<int>(file),
               static_cast<int>(op), static_cast<int>(after), dest);
        return queue().last_ticket();
    }
    return *t;
}

}  // namespace

FileQueue& game_file_queue() { return queue(); }

std::int32_t queue_read(std::int32_t file, void* dest) {
    return enqueue(file, FileOp::Read, AfterLoad::None, dest, nullptr, 0);
}

std::int32_t queue_read_image(std::int32_t file, void* dest, const ImagePlace* place) {
    // No place: the image goes where the file says.
    ImagePlace p{{0, 0}, kPlaceFromFile, kPlaceFromFile, -1, -1};
    if (place != nullptr) p = *place;
    return enqueue(file, FileOp::Read, AfterLoad::Image, dest, &p, sizeof(p));
}

std::int32_t queue_read_anim(int slot, int chara_id, void* dest, void* bones) {
    const AnimPlace a{static_cast<std::uint32_t>(slot), static_cast<std::uint32_t>(chara_id), bones};
    return enqueue(view::chara_anim_file(chara_id), FileOp::Read, AfterLoad::Animation, dest, &a, sizeof(a));
}

std::int32_t queue_pace(std::int32_t file, void* dest) {
    return enqueue(file, FileOp::Read, AfterLoad::None, dest, kPaceMark.data(), kPaceMark.size());
}

std::int32_t queue_seek(std::int32_t file) { return enqueue(file, FileOp::Seek, AfterLoad::None, nullptr, nullptr, 0); }

bool queue_loaded(std::int32_t ticket) { return queue().loaded(ticket); }
int queue_length() { return queue().length(); }
void queue_update() { queue().update(s_host); }
void queue_clear() { queue().clear(); }
void queue_reset() { queue().reset(); }

bool queue_load_chunks() {
    view::chunk_load_mark();
    // Reads finish within the update on PC, so a bounded drain empties the queue here: waiting for an already empty
    // queue softlocked whenever streaming kept requeuing reads every frame.
    sh::disc::drive_pacing().instant = g_PcFastBlockingLoads != 0;
    queue().drain(s_host, 500);
    sh::disc::drive_pacing().instant = false;
    return queue().length() == 0;
}

void queue_drain_now() {
    if (!g_PcFastBlockingLoads) return;
    sh::disc::drive_pacing().instant = true;
    queue().drain(s_host, 4096);
    sh::disc::drive_pacing().instant = false;
}

void queue_wait_empty() {
    sh::app::view::vibration_play(0);
    sh::app::view::vibration_update();
    GameWait wait;
    queue().wait_until_empty(s_host, wait);
    sh::app::view::vibration_play(1);
    sh::screens::wait_frame_done(false);
    sh::timing::wait_vblank();
}

}  // namespace sh::loader
