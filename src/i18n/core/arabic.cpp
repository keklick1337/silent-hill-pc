// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n/core/arabic.h"

#include "i18n/core/unicode.h"

namespace sh::i18n {
namespace {

constexpr char32_t kLam = 0x0644;
constexpr char32_t kTatweel = 0x0640;
constexpr char32_t kZeroWidthJoiner = 0x200D;
constexpr char32_t kZeroWidthNonJoiner = 0x200C;

// How a character takes part in joining.
enum class Joining { None, Right, Dual, Causing, Transparent };

Joining joining_of(char32_t cp) {
    if (const ArabicLetter* letter = arabic_letter(cp)) {
        return letter->dual() ? Joining::Dual : Joining::Right;
    }
    if (cp == kTatweel || cp == kZeroWidthJoiner) {
        return Joining::Causing;
    }
    if (cp == kZeroWidthNonJoiner) {
        return Joining::None;
    }
    // Marks and invisible format characters do not interrupt the letters around them.
    if (is_mark(cp) || bidi_class(cp) == BidiClass::BN) {
        return Joining::Transparent;
    }
    return Joining::None;
}

}  // namespace

bool has_arabic(std::u32string_view text) {
    for (const char32_t cp : text) {
        if (arabic_letter(cp) != nullptr) {
            return true;
        }
    }
    return false;
}

ShapedText shape_arabic(std::u32string_view logical) {
    const std::size_t n = logical.size();
    std::vector<Joining> joining(n);
    for (std::size_t i = 0; i < n; ++i) {
        joining[i] = joining_of(logical[i]);
    }
    // The nearest character on either side that is not transparent; n when there is none.
    const auto before = [&](std::size_t i) {
        while (i-- > 0) {
            if (joining[i] != Joining::Transparent) {
                return i;
            }
        }
        return n;
    };
    const auto after = [&](std::size_t i) {
        for (++i; i < n; ++i) {
            if (joining[i] != Joining::Transparent) {
                return i;
            }
        }
        return n;
    };

    ShapedText out;
    out.text.reserve(n);
    out.source.reserve(n);
    std::vector<bool> consumed(n, false);
    for (std::size_t i = 0; i < n; ++i) {
        if (consumed[i]) {
            continue;
        }
        const char32_t cp = logical[i];
        const ArabicLetter* letter = arabic_letter(cp);
        if (letter == nullptr) {
            out.text += cp;
            out.source.push_back(i);
            continue;
        }
        const std::size_t prev = before(i);
        const std::size_t next = after(i);
        // The letter before reaches this one when it joins forward; every letter accepts that.
        const bool joins_prev =
            prev != n && (joining[prev] == Joining::Dual || joining[prev] == Joining::Causing);
        const bool joins_next = letter->dual() && next != n &&
                          (joining[next] == Joining::Dual || joining[next] == Joining::Right ||
                           joining[next] == Joining::Causing);

        char32_t ligature_isolated = 0;
        char32_t ligature_final = 0;
        if (cp == kLam && next != n && lam_alef(logical[next], ligature_isolated, ligature_final)) {
            out.text += joins_prev ? ligature_final : ligature_isolated;
            out.source.push_back(i);
            // The alef is drawn by the ligature; it still stands before the next letter and joins nothing after it.
            consumed[next] = true;
            continue;
        }

        char32_t form = 0;
        if (joins_prev && joins_next) {
            form = letter->medial;
        } else if (joins_prev) {
            form = letter->final_form;
        } else if (joins_next) {
            form = letter->initial;
        }
        // The letter on its own is its isolated shape in every font; a missing form falls back to it too.
        out.text += form != 0 ? form : cp;
        out.source.push_back(i);
    }
    return out;
}

}  // namespace sh::i18n
