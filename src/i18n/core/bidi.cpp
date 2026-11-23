// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n/core/bidi.h"

#include "i18n/core/unicode.h"

#include <algorithm>

namespace sh::i18n {
namespace {

using C = BidiClass;

constexpr std::size_t kBracketDepth = 63;  // the annex's limit on open brackets tracked at once

bool is_neutral(C c) { return c == C::B || c == C::S || c == C::WS || c == C::ON; }

// The strong direction a class counts as for the neutral rules: numbers are right-to-left there.
C strong_of(C c) {
    if (c == C::L) {
        return C::L;
    }
    if (c == C::R || c == C::EN || c == C::AN) {
        return C::R;
    }
    return C::ON;
}

// The rules run over the characters that are not boundary neutrals; `t` holds their classes.
void resolve_weak(std::vector<C>& t, C sos) {
    const std::size_t n = t.size();
    // W1: a mark takes the class of what it sits on.
    for (std::size_t i = 0; i < n; ++i) {
        if (t[i] == C::NSM) {
            t[i] = i == 0 ? sos : t[i - 1];
        }
    }
    // W2: European digits after an Arabic letter are Arabic digits.
    C strong = sos;
    for (std::size_t i = 0; i < n; ++i) {
        if (t[i] == C::L || t[i] == C::R || t[i] == C::AL) {
            strong = t[i];
        } else if (t[i] == C::EN && strong == C::AL) {
            t[i] = C::AN;
        }
    }
    // W3.
    for (C& c : t) {
        if (c == C::AL) {
            c = C::R;
        }
    }
    // W4: one separator between two numbers of the same kind joins them.
    for (std::size_t i = 1; i + 1 < n; ++i) {
        if (t[i] == C::ES && t[i - 1] == C::EN && t[i + 1] == C::EN) {
            t[i] = C::EN;
        } else if (t[i] == C::CS && t[i - 1] == t[i + 1] && (t[i - 1] == C::EN || t[i - 1] == C::AN)) {
            t[i] = t[i - 1];
        }
    }
    // W5: terminators next to European digits belong to the number.
    for (std::size_t i = 0; i < n;) {
        if (t[i] != C::ET) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < n && t[end] == C::ET) {
            ++end;
        }
        if ((i > 0 && t[i - 1] == C::EN) || (end < n && t[end] == C::EN)) {
            std::fill(t.begin() + static_cast<std::ptrdiff_t>(i), t.begin() + static_cast<std::ptrdiff_t>(end), C::EN);
        }
        i = end;
    }
    // W6.
    std::vector<bool> sign(n, false);
    for (std::size_t i = 0; i < n; ++i) {
        sign[i] = t[i] == C::ES;
        if (t[i] == C::ES || t[i] == C::ET || t[i] == C::CS) {
            t[i] = C::ON;
        }
    }
    // W7: European digits in left-to-right text are part of it.
    strong = sos;
    for (std::size_t i = 0; i < n; ++i) {
        if (t[i] == C::L || t[i] == C::R) {
            strong = t[i];
        } else if (t[i] == C::EN && strong == C::L) {
            t[i] = C::L;
        }
    }
    // Beyond the annex: a plus or minus sign written straight after a left-to-right word is part of it ("Hor+",
    // "C++"). The annex leaves it to the paragraph, which in right-to-left text puts it before the word; texts
    // of the game carry no direction marks to say otherwise.
    for (std::size_t i = 1; i < n; ++i) {
        if (sign[i] && t[i] == C::ON && t[i - 1] == C::L) {
            t[i] = C::L;
        }
    }
}

// N0: a pair of brackets takes one direction, so that "(abc)" inside right-to-left text keeps both of its brackets
// on the outside of the word.
void resolve_brackets(std::vector<C>& t, std::u32string_view line, const std::vector<std::size_t>& index, C sos,
                      C embedding) {
    struct Open {
        char32_t closing;
        std::size_t at;
    };
    std::vector<Open> stack;
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] != C::ON) {
            continue;
        }
        const char32_t c = line[index[i]];
        if (const char32_t closing = closing_bracket(c)) {
            if (stack.size() >= kBracketDepth) {
                break;
            }
            stack.push_back({closing, i});
        } else if (opening_bracket(c) != 0) {
            for (std::size_t k = stack.size(); k-- > 0;) {
                if (stack[k].closing == c) {
                    pairs.emplace_back(stack[k].at, i);
                    stack.resize(k);
                    break;
                }
            }
        }
    }
    std::sort(pairs.begin(), pairs.end());
    for (const auto& [open, close] : pairs) {
        bool with = false;     // a strong character of the embedding direction inside
        bool against = false;  // one of the other direction
        for (std::size_t i = open + 1; i < close; ++i) {
            const C s = strong_of(t[i]);
            with = with || s == embedding;
            against = against || (s != C::ON && s != embedding);
        }
        C set = C::ON;
        if (with) {
            set = embedding;
        } else if (against) {
            // What stands before the pair decides.
            C before = sos;
            for (std::size_t i = open; i-- > 0;) {
                if (strong_of(t[i]) != C::ON) {
                    before = strong_of(t[i]);
                    break;
                }
            }
            set = before != embedding ? before : embedding;
        }
        if (set != C::ON) {
            t[open] = set;
            t[close] = set;
        }
    }
}

// N1, N2: neutrals between two characters of one direction take it, the others take the paragraph's.
void resolve_neutrals(std::vector<C>& t, C sos, C embedding) {
    const std::size_t n = t.size();
    for (std::size_t i = 0; i < n;) {
        if (!is_neutral(t[i])) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < n && is_neutral(t[end])) {
            ++end;
        }
        const C before = i == 0 ? sos : strong_of(t[i - 1]);
        const C after = end == n ? sos : strong_of(t[end]);
        const C set = before == after && before != C::ON ? before : embedding;
        std::fill(t.begin() + static_cast<std::ptrdiff_t>(i), t.begin() + static_cast<std::ptrdiff_t>(end), set);
        i = end;
    }
}

}  // namespace

BidiLine bidi_line(std::u32string_view line, Direction base) {
    const std::size_t count = line.size();
    const std::uint8_t base_level = base == Direction::Rtl ? 1 : 0;
    const C embedding = base == Direction::Rtl ? C::R : C::L;

    std::vector<C> original(count);
    std::vector<std::size_t> index;  // the characters the rules see
    std::vector<C> t;
    index.reserve(count);
    t.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        original[i] = bidi_class(line[i]);
        if (original[i] != C::BN) {
            index.push_back(i);
            t.push_back(original[i]);
        }
    }

    resolve_weak(t, embedding);
    resolve_brackets(t, line, index, embedding, embedding);
    resolve_neutrals(t, embedding, embedding);

    BidiLine out;
    out.levels.assign(count, base_level);
    // I1, I2.
    for (std::size_t k = 0; k < t.size(); ++k) {
        std::uint8_t level = base_level;
        if (base_level == 0) {
            level = t[k] == C::R ? 1 : (t[k] == C::AN || t[k] == C::EN) ? 2 : 0;
        } else {
            level = t[k] == C::R ? 1 : 2;
        }
        out.levels[index[k]] = level;
    }
    // L1: spaces at the end of the line stay at the paragraph's side.
    for (std::size_t i = count; i-- > 0;) {
        if (original[i] == C::WS || original[i] == C::S || original[i] == C::B || original[i] == C::BN) {
            out.levels[i] = base_level;
        } else {
            break;
        }
    }
    // A boundary neutral inside the line follows the character before it.
    for (std::size_t i = 0; i < count; ++i) {
        if (original[i] == C::BN && i > 0) {
            out.levels[i] = out.levels[i - 1];
        }
    }

    // L2: reverse every run at or above each level, from the highest down to the lowest odd one.
    std::vector<std::size_t> order = index;
    std::uint8_t highest = 0;
    std::uint8_t lowest_odd = 255;
    for (const std::size_t i : order) {
        highest = std::max(highest, out.levels[i]);
        if ((out.levels[i] & 1) != 0) {
            lowest_odd = std::min(lowest_odd, out.levels[i]);
        }
    }
    for (std::uint8_t level = highest; lowest_odd != 255 && level >= lowest_odd; --level) {
        for (std::size_t i = 0; i < order.size();) {
            if (out.levels[order[i]] < level) {
                ++i;
                continue;
            }
            std::size_t end = i;
            while (end < order.size() && out.levels[order[end]] >= level) {
                ++end;
            }
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i), order.begin() + static_cast<std::ptrdiff_t>(end));
            i = end;
        }
        if (level == 0) {
            break;
        }
    }
    // L3: the drawer puts a mark over the character drawn before it, so in a reversed run the marks go back
    // behind their base.
    for (std::size_t i = 0; i < order.size();) {
        if (original[order[i]] != C::NSM || (out.levels[order[i]] & 1) == 0) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < order.size() && original[order[end]] == C::NSM && (out.levels[order[end]] & 1) != 0) {
            ++end;
        }
        if (end < order.size() && (out.levels[order[end]] & 1) != 0) {
            // [marks (reversed)] [base] -> [base] [marks in their own order]
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i), order.begin() + static_cast<std::ptrdiff_t>(end));
            std::rotate(order.begin() + static_cast<std::ptrdiff_t>(i), order.begin() + static_cast<std::ptrdiff_t>(end),
                        order.begin() + static_cast<std::ptrdiff_t>(end) + 1);
            ++end;
        }
        i = end;
    }
    out.order = std::move(order);
    return out;
}

std::u32string bidi_visual(std::u32string_view line, Direction base) {
    const BidiLine b = bidi_line(line, base);
    std::u32string out;
    out.reserve(b.order.size());
    for (const std::size_t i : b.order) {
        out += (b.levels[i] & 1) != 0 ? mirrored(line[i]) : line[i];
    }
    return out;
}

bool has_rtl(std::u32string_view text) {
    for (const char32_t cp : text) {
        const C c = bidi_class(cp);
        if (c == C::R || c == C::AL || c == C::AN) {
            return true;
        }
    }
    return false;
}

Direction first_strong(std::u32string_view text, Direction fallback) {
    for (const char32_t cp : text) {
        const C c = bidi_class(cp);
        if (c == C::L) {
            return Direction::Ltr;
        }
        if (c == C::R || c == C::AL) {
            return Direction::Rtl;
        }
    }
    return fallback;
}

}  // namespace sh::i18n
