// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n/core/unicode.h"

#include <algorithm>
#include <iterator>

namespace sh::i18n {
namespace {

struct BidiRange {
    char32_t first;
    char32_t last;
    BidiClass cls;
};
struct CodeRange {
    char32_t first;
    char32_t last;
};
struct ArabicForms {
    char32_t base;
    char32_t isolated;
    char32_t final_form;
    char32_t initial;
    char32_t medial;
};
struct LamAlef {
    char32_t alef;
    char32_t isolated;
    char32_t final_form;
};
struct Composition {
    char32_t base;
    char32_t mark;
    char32_t composed;
};
struct CodePair {
    char32_t from;
    char32_t to;
};

#include "i18n/core/unicode_data.inc"

// The run containing cp in a table sorted by `first`, or null.
template <typename Range, std::size_t N>
const Range* find_range(const Range (&table)[N], char32_t cp) {
    const Range* it = std::upper_bound(std::begin(table), std::end(table), cp,
                                       [](char32_t value, const Range& r) { return value < r.first; });
    if (it == std::begin(table)) {
        return nullptr;
    }
    --it;
    return cp <= it->last ? it : nullptr;
}

template <std::size_t N>
char32_t find_pair(const CodePair (&table)[N], char32_t cp) {
    const CodePair* it = std::lower_bound(std::begin(table), std::end(table), cp,
                                          [](const CodePair& p, char32_t value) { return p.from < value; });
    return it != std::end(table) && it->from == cp ? it->to : 0;
}

constexpr bool in(char32_t cp, char32_t first, char32_t last) { return cp >= first && cp <= last; }

// Closing punctuation, small kana and marks that hang on the character before them.
constexpr std::u32string_view kNoLineStart =
    U"、。，．・：；？！゛゜ヽヾゝゞ々ー’”）〕］｝〉》」』】〙〗〟｠»‥…"
    U"ぁぃぅぇぉっゃゅょゎゕゖァィゥェォッャュョヮヵヶ,.:;?!)]}%％‰℃";
constexpr std::u32string_view kNoLineEnd = U"‘“（〔［｛〈《「『【〘〖〝｟«([{$￥＄£";

}  // namespace

BidiClass bidi_class(char32_t cp) {
    const BidiRange* r = find_range(kBidiRanges, cp);
    return r != nullptr ? r->cls : BidiClass::L;
}

bool is_mark(char32_t cp) { return find_range(kMarkRanges, cp) != nullptr; }

char32_t mirrored(char32_t cp) {
    const char32_t m = find_pair(kMirrorPairs, cp);
    return m != 0 ? m : cp;
}

char32_t closing_bracket(char32_t open) { return find_pair(kBracketPairs, open); }

char32_t opening_bracket(char32_t close) {
    for (const CodePair& p : kBracketPairs) {
        if (p.to == close) {
            return p.from;
        }
    }
    return 0;
}

char32_t compose(char32_t base, char32_t mark) {
    const Composition* it = std::lower_bound(
        std::begin(kCompositions), std::end(kCompositions), std::pair{base, mark},
        [](const Composition& c, const std::pair<char32_t, char32_t>& key) {
            return c.base != key.first ? c.base < key.first : c.mark < key.second;
        });
    return it != std::end(kCompositions) && it->base == base && it->mark == mark ? it->composed : 0;
}

const ArabicLetter* arabic_letter(char32_t cp) {
    // The table is small and sorted by base; the letters keep their shapes in a parallel array built once.
    static const std::vector<ArabicLetter> letters = [] {
        std::vector<ArabicLetter> out;
        out.reserve(std::size(kArabicForms));
        for (const ArabicForms& f : kArabicForms) {
            out.push_back({f.isolated, f.final_form, f.initial, f.medial});
        }
        return out;
    }();
    const ArabicForms* it = std::lower_bound(std::begin(kArabicForms), std::end(kArabicForms), cp,
                                             [](const ArabicForms& f, char32_t value) { return f.base < value; });
    if (it == std::end(kArabicForms) || it->base != cp) {
        return nullptr;
    }
    return &letters[static_cast<std::size_t>(it - std::begin(kArabicForms))];
}

char32_t arabic_base(char32_t form) {
    if (!in(form, 0xFB50, 0xFDFF) && !in(form, 0xFE70, 0xFEFF)) {
        return form;
    }
    for (const ArabicForms& f : kArabicForms) {
        if (f.isolated == form || f.final_form == form || f.initial == form || f.medial == form) {
            return f.base;
        }
    }
    return form;
}

bool lam_alef(char32_t alef, char32_t& isolated, char32_t& final_form) {
    for (const LamAlef& l : kLamAlef) {
        if (l.alef == alef) {
            isolated = l.isolated;
            final_form = l.final_form;
            return true;
        }
    }
    return false;
}

bool is_wide(char32_t cp) {
    // The dash, the two-dot leader, the reference mark and the geometric marks of East Asian text: of ambiguous
    // width in Unicode, a full square where they are used.
    if (cp == 0x2015 || cp == 0x2025 || cp == 0x203B || in(cp, 0x25A0, 0x25EF)) {
        return true;
    }
    return in(cp, 0x1100, 0x115F) || in(cp, 0x2E80, 0x303E) || in(cp, 0x3041, 0x33FF) || in(cp, 0x3400, 0x4DBF) ||
           in(cp, 0x4E00, 0x9FFF) || in(cp, 0xA000, 0xA4CF) || in(cp, 0xAC00, 0xD7A3) || in(cp, 0xF900, 0xFAFF) ||
           in(cp, 0xFE30, 0xFE4F) || in(cp, 0xFF00, 0xFF60) || in(cp, 0xFFE0, 0xFFE6) || in(cp, 0x20000, 0x3FFFD);
}

bool breaks_anywhere(char32_t cp) {
    // Hangul is written with spaces, but a line of it may break between syllables too.
    return is_wide(cp) && !in(cp, 0xFF10, 0xFF19) && !in(cp, 0xFF21, 0xFF3A) && !in(cp, 0xFF41, 0xFF5A);
}

bool no_line_start(char32_t cp) { return kNoLineStart.find(cp) != std::u32string_view::npos; }
bool no_line_end(char32_t cp) { return kNoLineEnd.find(cp) != std::u32string_view::npos; }

char32_t decode_utf8(std::string_view text, std::size_t& pos) {
    const auto byte = [&](std::size_t i) { return static_cast<std::uint32_t>(static_cast<unsigned char>(text[i])); };
    const std::uint32_t c = byte(pos);
    if (c < 0xC2 || c > 0xF4) {
        ++pos;
        return c;
    }
    const std::size_t n = c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    if (pos + n > text.size()) {
        ++pos;
        return c;
    }
    std::uint32_t cp = c & (n == 2 ? 0x1Fu : n == 3 ? 0x0Fu : 0x07u);
    for (std::size_t i = 1; i < n; ++i) {
        if ((byte(pos + i) & 0xC0) != 0x80) {
            ++pos;
            return c;
        }
        cp = (cp << 6) | (byte(pos + i) & 0x3F);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || in(cp, 0xD800, 0xDFFF)) {
        ++pos;
        return c;
    }
    pos += n;
    return cp;
}

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::u32string to_utf32(std::string_view text) {
    std::u32string out;
    for (std::size_t pos = 0; pos < text.size();) {
        out += decode_utf8(text, pos);
    }
    return out;
}

std::string to_utf8(std::u32string_view text) {
    std::string out;
    for (const char32_t cp : text) {
        append_utf8(out, cp);
    }
    return out;
}

}  // namespace sh::i18n
