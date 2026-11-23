// SPDX-License-Identifier: GPL-3.0-or-later
// The boot logo screens: a logo image that fades in, holds until a button or a frame
// count, fades out and hands over to the next state, while the game loads in the
// background. A screen may check the save cards after its fade-in; the outcome picks
// what is loaded next and where the boot goes. Images, layout, timings and the
// skip rules come from gamedata/menu/boot_logos.json; loads, cards, fades and the
// frame itself are the game's (BootLogoHost).

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "engine/screens/staff_roll.h"
#include "formats/common/status.h"

namespace sh::screens {

// One rectangle of a logo image: its place in the image (texels) and on screen.
struct LogoSegment {
    std::int32_t image_x = 0;
    std::int32_t image_y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
};

struct LogoScreen {
    std::string name;       // the host's name of the screen (its loads and image)
    std::string image;      // the host's name of the logo image
    std::vector<LogoSegment> segments;
    bool card_check = false;  // check the save cards once faded in
    std::int32_t fade_in_step = 4096;   // Q12 fade step per frame
    std::int32_t fade_out_step = 4096;
    std::int32_t hold_frames = 0;       // the hold ends when the frame counter passes this
    // Skip (a confirm key or the pad's skip button): without a card check it starts the
    // fade-out from the fade-in or the hold; with one it is latched for the rest of the
    // run, speeds the fade-in up (the cards are still checked) and cuts the hold.
    std::int32_t skip_fade_step = 4096;
    bool skip_latched = false;
    std::string next_state;  // where a screen without a card check goes
};

struct BootLogos {
    std::vector<LogoScreen> screens;
    std::uint32_t backdrop_rgb = 0;  // the flat colour behind the logo
    std::int32_t backdrop[4] = {0, 0, 0, 0};  // x, y, w, h
    std::int32_t order = 0;  // the draw slot of the logo and the backdrop

    const LogoScreen* find(std::string_view name) const;
};

// Result of the save check (the screen step it leads to). NoCard and NoFreeSpace are never produced (the
// saves are files, not a card); the numbers stay because the steps show in the replay dumps.
enum class CardOutcome : std::int32_t {
    Pending = 1,      // not ready, check again next frame
    NoCard = 2,
    NoFreeSpace = 3,
    NoSave = 4,
    HasSave = 5,
};

class BootLogoHost {
public:
    virtual ~BootLogoHost() = default;

    virtual bool active() = 0;  // the game state is still this screen's
    virtual void poll_input() = 0;
    virtual bool skip_pressed() = 0;
    virtual bool any_button_held() = 0;
    virtual std::int32_t frame_counter() = 0;

    virtual std::int32_t step(int level) = 0;
    virtual void set_step(int level, std::int32_t value) = 0;
    virtual void reset_counters() = 0;  // the second frame counter and steps 1 and 2

    virtual Fade fade() = 0;
    virtual void set_fade(Fade fade) = 0;
    virtual void set_fade_step(std::int32_t step_q12) = 0;

    // The screen's own work: before the fade-in (display mode, settings), after it is
    // started (background loads), the card outcome's loads, the finish of a screen with a
    // card check (display, loads, the next state).
    virtual void prepare(const std::string& screen) = 0;
    virtual void start_loads(const std::string& screen) = 0;
    virtual void wait_loads() = 0;
    virtual CardOutcome check_cards() = 0;
    virtual void wait_vblank() = 0;
    virtual void card_outcome(CardOutcome outcome) = 0;
    virtual void finish_card_check() = 0;
    virtual void set_next_state(const std::string& name) = 0;

    virtual void draw_segment(const std::string& image, const LogoSegment& segment, std::int32_t order) = 0;
    virtual void draw_backdrop(std::uint32_t rgb, const std::int32_t (&rect)[4], std::int32_t order) = 0;
    virtual void end_frame() = 0;  // fade, loads, cards, present
};

class BootLogoScreen {
public:
    BootLogoScreen(const BootLogos& logos, const LogoScreen& screen) : logos_(logos), screen_(screen) {}

    // Screen steps (gameStateSteps[0]).
    static constexpr std::int32_t kStepInit = 0;
    static constexpr std::int32_t kStepWaitFade = 1;
    std::int32_t hold_step() const { return screen_.card_check ? 6 : 2; }
    std::int32_t finish_step() const { return hold_step() + 1; }

    // Frames until the game state changes.
    void run(BootLogoHost& host);
    void frame(BootLogoHost& host);

    bool skip_latched() const { return skipped_; }

private:
    void skip(BootLogoHost& host);
    void draw(BootLogoHost& host) const;

    const BootLogos& logos_;
    const LogoScreen& screen_;
    bool skipped_ = false;
};

Result<BootLogos> parse_boot_logos(std::string_view json_text);
Result<BootLogos> load_boot_logos(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                  std::string* source = nullptr);

}  // namespace sh::screens
