// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/staff_roll_screen.h"

#include "engine/screens/staff_roll_data.h"

namespace sh::screens {
namespace {

namespace d2 = sh::draw2d;

// The ending index of the UFO ending's tilted roll (kEndingNames).
constexpr std::int32_t kUfoEnding = 5;
// More lines are dropped (the roll's row table of the original had this room).
constexpr std::size_t kLineCapacity = 1024;


// Packet code bytes of the original primitives (the shading and size flags are read back from them).
constexpr std::uint32_t kSpriteCode = 0x64;
constexpr std::uint32_t kQuadCode = 0x2C;

struct Texel {
    d2::TexCoord uv;
    std::uint16_t extra;
};
Texel texel(std::uint32_t word) {
    return {{static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8)},
            static_cast<std::uint16_t>(word >> 16)};
}
d2::Rgb rgb(std::uint32_t word) {
    return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8),
            static_cast<std::uint8_t>(word >> 16)};
}
d2::Shade shade(std::uint32_t code) { return {(code & 2) != 0, (code & 1) != 0}; }
std::uint32_t color_word(std::uint32_t code, const RollPrim& r) {
    return r.rgb | (code << 24) | ((r.semi_transparent ? 1u : 0u) << 25);
}

class Sink final : public RollSink {
public:
    Sink(const StaffRollScreen& screen, StaffRollScreenHost& host, bool bars) : screen_(screen), host_(host), bars_(bars) {}
    void prim(const RollPrim& r) override {
        if ((r.kind == RollPrim::Kind::Bar) != bars_) {
            return;
        }
        for (const d2::Command& c : screen_.commands(r)) {
            host_.submit(bars_ ? RollLayer::NextFrameBack : RollLayer::Text, c);
        }
    }

private:
    const StaffRollScreen& screen_;
    StaffRollScreenHost& host_;
    bool bars_;
};

}  // namespace

EndingTiming StaffRollScreen::ending_timing(std::int32_t ending) const {
    if (!roll_ || ending < 0 || static_cast<std::size_t>(ending) >= kEndingCount) {
        return {};
    }
    return roll_->data.endings[static_cast<std::size_t>(ending)].timing;
}

std::int32_t StaffRollScreen::ending_music(std::int32_t ending) const {
    if (!roll_ || ending < 0 || static_cast<std::size_t>(ending) >= kEndingCount) {
        return -1;
    }
    return music_ids_[static_cast<std::size_t>(ending)];
}

void StaffRollScreen::set_font_image(std::uint8_t page_byte, std::int16_t clut_x, std::int16_t clut_y) {
    font_page_ = page_byte;
    font_tpage_ = static_cast<std::uint16_t>((page_byte & 0x10) | (page_byte & 0xF));
    font_clut_ = static_cast<std::uint16_t>((clut_y << 6) | ((clut_x >> 4) & 0x3F));
}

void StaffRollScreen::set_flat_blend_mode(std::uint32_t mode) {
    const std::uint32_t page = font_page_;
    font_tpage_ = static_cast<std::uint16_t>(((mode & 3) << 5) | (((page << 4) & (1u << 8)) >> 4) |
                                             static_cast<std::uint8_t>(page & 0xF));
}

void StaffRollScreen::plane_changed() {
    plane_tpage_ = font_tpage_;
    plane_clut_ = font_clut_;
}

std::vector<d2::Command> StaffRollScreen::commands(const RollPrim& r) const {
    switch (r.kind) {
        case RollPrim::Kind::Glyph:
        case RollPrim::Kind::LogoGlyph: {
            const bool logo = r.kind == RollPrim::Kind::LogoGlyph;
            const std::uint32_t color = color_word(kSpriteCode, r) ^ (logo ? (24u << 24) : 0u);
            const Texel tex = texel(static_cast<std::uint32_t>(r.uv[0]) + (static_cast<std::uint32_t>(font_clut_) << 16));
            d2::Sprite s;
            s.color = rgb(color);
            s.shade = shade(color >> 24);
            s.size = static_cast<d2::RectSize>((color >> 27) & 3);
            s.at = {static_cast<std::int16_t>(r.x[0]), static_cast<std::int16_t>(r.y[0])};
            s.uv = tex.uv;
            s.palette = d2::Palette::from_word(tex.extra);
            s.w = 24;
            s.h = 24;
            return {s};
        }
        case RollPrim::Kind::TexturePage: {
            d2::DrawMode mode;
            mode.page = d2::TexturePage::from_word(font_tpage_);
            mode.dither = true;
            return {mode};
        }
        case RollPrim::Kind::Quad: {
            const std::uint32_t color = color_word(kQuadCode, r);
            const Texel t0 = texel(static_cast<std::uint32_t>(r.uv[0]) + (static_cast<std::uint32_t>(plane_clut_) << 16));
            const Texel t1 = texel(static_cast<std::uint32_t>(r.uv[1]) + (static_cast<std::uint32_t>(plane_tpage_) << 16));
            d2::Polygon p;
            p.count = 4;
            p.textured = true;
            p.shade = shade(color >> 24);
            p.color[0] = rgb(color);
            p.uv[0] = t0.uv;
            p.uv[1] = t1.uv;
            p.uv[2] = texel(static_cast<std::uint32_t>(r.uv[2])).uv;
            p.uv[3] = texel(static_cast<std::uint32_t>(r.uv[3])).uv;
            p.palette = d2::Palette::from_word(t0.extra);
            p.page = d2::TexturePage::from_word(t1.extra);
            for (int i = 0; i < 4; ++i) {
                p.xy[i] = {static_cast<std::int16_t>(r.x[i]), static_cast<std::int16_t>(r.y[i])};
            }
            return {p};
        }
        case RollPrim::Kind::Bar: {
            d2::Fill f;
            f.at = {static_cast<std::int16_t>(r.x[0]), static_cast<std::int16_t>(r.y[0])};
            f.w = static_cast<std::uint16_t>(r.x[1]);
            f.h = static_cast<std::uint16_t>(r.y[1]);
            return {f};
        }
    }
    return {};
}

StaffRoll* StaffRollScreen::roll(StaffRollScreenHost& host) {
    if (mods::data_stale(epoch_)) {
        roll_.reset();
        load_tried_ = false;
    }
    if (roll_ || load_tried_) {
        return roll_ ? roll_->roll.get() : nullptr;
    }
    load_tried_ = true;
    std::string source;
    Result<StaffRollData> data = load_staff_roll(host.gamedata_dir(), host.mods_dir(), &source);
    if (!data) {
        host.log(true, "[CREDITS] " + data.error() + " - the staff roll draws nothing");
        return nullptr;
    }
    roll_.emplace();
    roll_->data = std::move(data.value());
    roll_->roll = std::make_unique<StaffRoll>(roll_->data.font, roll_->data.style);
    for (std::size_t i = 0; i < kEndingCount; i++) {
        const std::string& music = roll_->data.endings[i].music;
        music_ids_[i] = host.sound_number(music);
        if (music_ids_[i] < 0) {
            host.log(true, "[CREDITS] " + source + ": ending \"" + std::string(kEndingNames[i]) + "\" music \"" +
                               music + "\" is no sound - that roll plays silent");
        }
    }

    lines_.clear();
    for (std::string& line : roll_->data.lines) {
        if (lines_.size() == kLineCapacity) {
            host.log(false, "[CREDITS] " + source + ": more than " + std::to_string(kLineCapacity) +
                                " lines, the rest is dropped");
            break;
        }
        lines_.push_back(line.data());
    }
    const std::size_t n = lines_.size();
    lines_.push_back(nullptr);
    host.log(false, "[CREDITS] " + source + ": " + std::to_string(n) + " lines");
    return roll_->roll.get();
}

void StaffRollScreen::start_flat(std::int32_t ending, StaffRollScreenHost& host) {
    if (StaffRoll* r = roll(host)) {
        r->start_flat(ending, host);
    }
}

bool StaffRollScreen::music_started(StaffRollScreenHost& host) {
    StaffRoll* r = roll(host);
    return r != nullptr && r->music_started(host);
}

bool StaffRollScreen::finish(StaffRollScreenHost& host) {
    StaffRoll* r = roll(host);
    return r != nullptr && r->finish(host);
}

void StaffRollScreen::update(StaffRollScreenHost& host) {
    if (StaffRoll* r = roll(host)) {
        Sink text(*this, host, false);
        Sink bars(*this, host, true);
        r->update(host, text, bars);
    }
}

void StaffRollScreen::start_perspective(StaffRollScreenHost& host) {
    if (StaffRoll* r = roll(host)) {
        r->start_perspective(kUfoEnding, host);
    }
}

bool StaffRollScreen::update_perspective(StaffRollScreenHost& host) {
    StaffRoll* r = roll(host);
    if (r == nullptr) {
        return false;
    }
    Sink text(*this, host, false);
    return r->update_perspective(host, text);
}

}  // namespace sh::screens
