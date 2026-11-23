// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/quick_setup/text_raster.h"

#include "i18n/core/text_prepare.h"

#include <algorithm>
#include <cmath>

namespace sh::quick_setup {

std::u32string decode_utf8(std::string_view text) {
    std::u32string out;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (extra < 0 || (extra > 0 && i + static_cast<std::size_t>(extra) >= text.size())) {
            out.push_back(U'�');
            ++i;
            continue;
        }
        char32_t cp = extra == 0 ? c : c & (0x3F >> extra);
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char n = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
            if ((n & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (n & 0x3F);
        }
        out.push_back(ok ? cp : U'�');
        i += ok ? static_cast<std::size_t>(extra) + 1 : 1;
    }
    return out;
}

TextRaster::TextRaster(const std::filesystem::path& gamedata_fonts) {
    catalog_.add_directory(gamedata_fonts, sh::i18n::FontTier::GameData);
}

const sh::i18n::OutlineFont* TextRaster::font(const sh::i18n::FontFace& face) {
    const std::string key = face.file.string() + "#" + std::to_string(face.index);
    auto it = fonts_.find(key);
    if (it == fonts_.end()) it = fonts_.emplace(key, sh::i18n::OutlineFont::load(face)).first;
    return it->second.get();
}

TextBitmap TextRaster::render(std::string_view utf8, float px, std::span<const std::string> families, bool rtl) {
    sh::i18n::PrepareOptions options;
    options.dialect = sh::i18n::TextDialect::Plain;
    options.base = rtl ? sh::i18n::Direction::Rtl : sh::i18n::Direction::Ltr;
    const sh::i18n::PreparedText prepared = sh::i18n::prepare_text(utf8, options);
    const std::u32string text = decode_utf8(prepared.changed ? std::string_view(prepared.text) : utf8);

    // The system's fonts are walked only once a character needs them: Latin and Cyrillic never do.
    if (!system_added_) {
        const bool missing = std::any_of(text.begin(), text.end(), [&](char32_t cp) {
            return cp > U' ' && catalog_.choose(cp, families) == nullptr;
        });
        if (missing) {
            system_added_ = true;
            for (const auto& dir : sh::i18n::system_font_directories())
                catalog_.add_directory(dir, sh::i18n::FontTier::System);
        }
    }

    struct Placed {
        char32_t cp;
        const sh::i18n::OutlineFont* font;
        float scale;
        float x;
    };
    std::vector<Placed> glyphs;
    std::vector<const sh::i18n::FontFace*> in_use;
    const sh::i18n::OutlineFont* last = nullptr;
    float last_scale = 0.0f;
    float pen = std::ceil(px * 0.1f);
    // One face for the whole line when one of the faces it needs has every character: a Cyrillic label otherwise
    // mixes the face of its letters with the Latin face of its digits.
    const sh::i18n::FontFace* whole = nullptr;
    {
        std::vector<const sh::i18n::FontFace*> chosen;
        for (char32_t cp : text) {
            if (cp <= U' ') continue;
            const sh::i18n::FontFace* face = catalog_.choose(cp, families);
            if (face && std::find(chosen.begin(), chosen.end(), face) == chosen.end()) chosen.push_back(face);
        }
        for (auto it = chosen.rbegin(); it != chosen.rend() && chosen.size() > 1; ++it) {
            if (std::all_of(text.begin(), text.end(), [&](char32_t cp) { return cp <= U' ' || (*it)->has(cp); })) {
                whole = *it;
                break;
            }
        }
    }
    for (char32_t cp : text) {
        const sh::i18n::FontFace* face = whole ? whole : catalog_.choose(cp, families, in_use);
        const sh::i18n::OutlineFont* f = face ? font(*face) : nullptr;
        if (f == nullptr) {
            // A space or a character no font has: the step of a space of the last font, so words stay apart.
            if (last != nullptr)
                pen += static_cast<float>(last->advance(U' ')) * last_scale;
            else
                pen += px * 0.3f;
            continue;
        }
        if (std::find(in_use.begin(), in_use.end(), face) == in_use.end()) in_use.push_back(face);
        const float scale = px / static_cast<float>(std::max(1, f->units_per_em()));
        glyphs.push_back({cp, f, scale, pen});
        pen += static_cast<float>(f->advance(cp)) * scale;
        last = f;
        last_scale = scale;
    }

    TextBitmap out;
    out.width = std::max(1, static_cast<int>(std::ceil(pen + px * 0.1f)));
    out.height = std::max(1, static_cast<int>(std::ceil(px * 1.45f)));
    out.baseline = static_cast<int>(std::lround(px * 1.05f));
    out.alpha.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height), 0);
    for (const Placed& g : glyphs) {
        g.font->draw(g.cp, g.scale, g.scale, g.x, static_cast<float>(out.baseline), out.alpha.data(), out.width,
                     out.height);
    }
    return out;
}

}  // namespace sh::quick_setup
