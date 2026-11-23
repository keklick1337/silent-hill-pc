// SPDX-License-Identifier: GPL-3.0-or-later
//
// The staff roll screen: the engine-owned roll with its data
// (gamedata/menu/staff_roll.json, loaded on first use, with the per-ending music and timing), the placement of the 24-pixel
// font in texture memory, and the roll's draw records turned into draw2d commands. The game supplies its state
// through StaffRollScreenHost.
#pragma once

#include "engine/mods/data_reload.h"
#include "engine/screens/staff_roll.h"
#include "render/draw2d/draw2d.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sh::screens {

// Where the roll's commands go: the text into the frame's 2D text layer, the covered top lines into the next
// frame's back layer.
enum class RollLayer : std::uint8_t { Text, NextFrameBack };

class StaffRollScreenHost : public StaffRollHost {
public:
    virtual std::filesystem::path gamedata_dir() = 0;
    virtual std::filesystem::path mods_dir() = 0;
    virtual void log(bool error, const std::string& line) = 0;
    // The sound id of a sound name (an ending's music), -1 when no sound has the name.
    virtual std::int32_t sound_number(const std::string& name) = 0;
    // Music of an ending, by its XA stream id.
    virtual void preload_stream(std::int32_t stream) = 0;
    virtual void play_stream(std::int32_t stream) = 0;
    virtual void submit(RollLayer layer, const draw2d::Command& command) = 0;
};

class StaffRollScreen {
public:
    // The loaded font image: the high byte of its texture page word and its palette position.
    void set_font_image(std::uint8_t page_byte, std::int16_t clut_x, std::int16_t clut_y);
    // The font's page with blend `mode`; the perspective text takes the page and palette when its plane changes.
    void set_flat_blend_mode(std::uint32_t mode);
    void plane_changed();

    // The roll's lines, NUL-terminated (empty until the data is loaded).
    char** lines() { return lines_.data(); }
    // The encoded logo line (StaffRollHost::logo), once the data is loaded.
    const std::uint8_t* logo() const {
        return roll_ ? reinterpret_cast<const std::uint8_t*>(roll_->data.logo.c_str()) : nullptr;
    }

    void start_flat(std::int32_t ending, StaffRollScreenHost& host);
    bool music_started(StaffRollScreenHost& host);
    bool finish(StaffRollScreenHost& host);
    void update(StaffRollScreenHost& host);
    void start_perspective(StaffRollScreenHost& host);
    bool update_perspective(StaffRollScreenHost& host);

    // Per ending (staff_roll.json "endings"): music start clock and roll length (StaffRollHost::ending), and the
    // sound id of its music (-1: the name did not resolve, nothing plays). Valid once the data is loaded.
    EndingTiming ending_timing(std::int32_t ending) const;
    std::int32_t ending_music(std::int32_t ending) const;
    // A logo glyph is the fixed 16x16 sprite (size bits flipped in the code byte), a text glyph the free one.
    std::vector<draw2d::Command> commands(const RollPrim& prim) const;

private:
    struct Roll {
        StaffRollData data;
        std::unique_ptr<StaffRoll> roll;
    };
    StaffRoll* roll(StaffRollScreenHost& host);

    std::optional<Roll> roll_;
    std::array<std::int32_t, kEndingCount> music_ids_{};
    bool load_tried_ = false;
    std::uint32_t epoch_ = mods::kNeverRead;  // `mods reload` reads the roll again
    std::vector<char*> lines_{nullptr};

    std::uint8_t font_page_ = 0;
    std::uint16_t font_tpage_ = 0;  // page word with the flat text's blend mode
    std::uint16_t font_clut_ = 0;
    std::uint16_t plane_tpage_ = 0;
    std::uint16_t plane_clut_ = 0;
};

// The game's one staff roll screen.
StaffRollScreen& staff_roll_screen();

}  // namespace sh::screens
