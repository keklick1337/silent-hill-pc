#include "render/gpu/page_resolve.h"

#include "engine/log/sh_log.h"
#include "render/gpu/renderer.h"
#include "render/gpu/targets.h"

namespace sh::gpu {

namespace {

PageHooks g_hooks;

int page_format(std::uint16_t tpage) { return (tpage >> 7) & 3; }
int page_x(std::uint16_t tpage) { return (tpage & 0xF) * 64; }
int page_y(std::uint16_t tpage) { return ((tpage >> 4) & 1) * 256; }

// A palette row past 511 is no palette video memory can hold. Sampling it would read an unrelated row (the signed
// clut attribute wraps) and draw wrong colours every frame. Such words are legitimate only as the keys of hi-res
// override slots, which never sample video memory, so the override lookup decides, not the bits. Direct pages
// read no palette at all (feedback overlays leave the word uninitialised).
bool clut_has_no_palette(std::uint16_t tpage, std::uint16_t clut) {
    if (page_format(tpage) >= 2) return false;
    if ((clut >> 6) <= 511) return false;
    return g_hooks.lookup_override == nullptr ||
           g_hooks.lookup_override(tpage, clut, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == 0;
}

bool scratch_covers(int x, int y) {
    if (g_hooks.scratch_covers != nullptr) return g_hooks.scratch_covers(x, y);
    Renderer* r = renderer();
    return r != nullptr && r->targets().scene_redirect_covers(x, y);
}

}  // namespace

PageHooks& page_hooks() { return g_hooks; }

bool clut_drops(std::uint16_t tpage, std::uint16_t clut, const char* type) {
    if (!clut_has_no_palette(tpage, clut)) return false;
    // One counter for every primitive type: a bad frame cannot flood the log through several of them.
    static int s_reported = 0;
    if (s_reported < 32) {
        ++s_reported;
        SH_LOG_INFO(RENDER, "[CLUTDROP] type=%s tpage=0x%04X clut=0x%04X clutY=%d reason=clutY_oob",
                    type != nullptr ? type : "?", static_cast<unsigned>(tpage), static_cast<unsigned>(clut),
                    clut >> 6);
    }
    return true;
}

bool resolve_page(SplitBuilder& out, std::uint16_t tpage, std::uint16_t clut, const char* type) {
    if (clut_drops(tpage, clut, type)) return false;
    PrimState& st = out.state();
    st.glyph = g_hooks.glyph_test != nullptr && g_hooks.glyph_test(tpage, clut) != 0;

    // 16-bit pages over the left 320 columns are the display-buffer band: every frame-feedback effect samples it
    // and no game texture lives there (direct textures sit at x >= 512).
    if (page_format(tpage) >= 2 && page_x(tpage) < 320) {
        st.feedback = true;
        if (g_hooks.note_feedback_reader != nullptr) g_hooks.note_feedback_reader(st.semi, st.r0);
    } else if (page_format(tpage) >= 2 && scratch_covers(page_x(tpage), page_y(tpage))) {
        st.scratch = true;
    }

    int w = 0, h = 0, off_x = 0, off_y = 0, hires_w = 0, hires_h = 0;
    const std::uint32_t id = g_hooks.lookup_override != nullptr
                                 ? g_hooks.lookup_override(tpage, clut, &w, &h, &off_x, &off_y, &hires_w, &hires_h)
                                 : 0;
    st.override_info = OverrideInfo{};
    if (id != 0) {
        st.override_texture = TextureRef{TextureRef::Kind::Override, id, 0};
        st.override_info.width = static_cast<std::int16_t>(w);
        st.override_info.height = static_cast<std::int16_t>(h);
        st.override_info.offset_x = static_cast<std::int16_t>(off_x);
        st.override_info.offset_y = static_cast<std::int16_t>(off_y);
        st.override_info.hires_w = hires_w;
        st.override_info.hires_h = hires_h;
    } else {
        st.override_texture = TextureRef{};
    }
    return true;
}

}  // namespace sh::gpu
