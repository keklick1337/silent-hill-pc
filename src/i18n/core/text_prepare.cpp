// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n/core/text_prepare.h"

#include "i18n/core/arabic.h"
#include "i18n/core/unicode.h"

#include <algorithm>
#include <vector>

namespace sh::i18n {
namespace {

constexpr int kNoColor = -1;       // the colour the string started with, which the string does not name
constexpr unsigned char kKern = 0x01;
constexpr unsigned char kFirstGlyphByte = '!';
constexpr char32_t kZeroWidthSpace = 0x200B;

// One drawn character or space of a line, with what travels with it.
struct Item {
    char32_t cp = 0;        // what it is for ordering, shaping and breaking (a space is U+0020)
    unsigned char raw = 0;  // the byte it was written with when that byte is kept; 0 = written from `cp`
    bool space = false;
    int color = kNoColor;
    std::string tail;       // zero-width bytes that follow it (kerning)
};

struct Line {
    std::string prefix;      // codes before the first character
    std::vector<Item> items;
    std::string suffix;      // codes after the first character that are not colours
    std::string terminator;  // the line break, page break or end code that closed the line
    int end_color = kNoColor;  // colour in force after the line, as written
    bool keeps_color = false;  // a piece of a wrapped line: the colour runs on into the next piece
    bool inset = false;
};

bool invisible(char32_t cp) {
    return (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x2069) ||
           cp == 0xFEFF || cp == 0x00AD;
}

// The byte a mirrored ASCII bracket is written with; 0 when the byte draws nothing that mirrors. In the game's
// font '[' and ']' are quotation marks; in plain text they are brackets.
unsigned char mirrored_byte(unsigned char b, bool plain) {
    switch (b) {
        case '(': return ')';
        case ')': return '(';
        case '<': return '>';
        case '>': return '<';
        case '{': return '}';
        case '}': return '{';
        case '[': return plain ? ']' : 0;
        case ']': return plain ? '[' : 0;
        default: return 0;
    }
}

class Parser {
public:
    Parser(std::string_view text, TextDialect dialect, int initial_color)
        : text_(text), dialect_(dialect), color_(initial_color) {}

    std::vector<Line> lines() {
        std::vector<Line> out(1);
        while (pos_ < text_.size() && text_[pos_] != '\0') {
            const bool more = dialect_ == TextDialect::Message  ? message_step(out)
                              : dialect_ == TextDialect::Plain ? plain_step(out)
                                                               : string_step(out);
            if (!more) {
                break;
            }
        }
        out.back().end_color = color_;
        return out;
    }

private:
    unsigned char at(std::size_t i) const { return i < text_.size() ? static_cast<unsigned char>(text_[i]) : 0; }

    void close(std::vector<Line>& out, std::string terminator) {
        out.back().terminator = std::move(terminator);
        out.back().end_color = color_;
        out.emplace_back();
        out.back().inset = inset_;
    }

    void add_code(Line& line, std::string raw) {
        (line.items.empty() ? line.prefix : line.suffix) += raw;
    }

    void add_glyph(Line& line) {
        Item item;
        item.color = color_;
        const unsigned char b = at(pos_);
        if (b < 0x80) {
            item.cp = dialect_ == TextDialect::Plain ? b : drawn_character(b);
            item.raw = b;
            ++pos_;
        } else {
            item.cp = decode_utf8(text_, pos_);
        }
        line.items.push_back(std::move(item));
    }

    void add_space(Line& line, unsigned char raw) {
        Item item;
        item.cp = U' ';
        item.raw = raw;
        item.space = true;
        item.color = color_;
        line.items.push_back(std::move(item));
        ++pos_;
    }

    bool plain_step(std::vector<Line>& out) {
        const unsigned char b = at(pos_);
        if (b == '\n') {
            ++pos_;
            close(out, "\n");
        } else if (b == ' ') {
            add_space(out.back(), b);
        } else if (b < ' ') {
            ++pos_;
        } else {
            add_glyph(out.back());
        }
        return true;
    }

    bool string_step(std::vector<Line>& out) {
        Line& line = out.back();
        const unsigned char b = at(pos_);
        if (b == '\n') {
            ++pos_;
            close(out, "\n");
        } else if (b == '_' || b == '\v') {
            add_space(line, b);
        } else if (b == kKern) {
            ++pos_;
            if (line.items.empty()) {
                line.prefix += static_cast<char>(kKern);
            } else {
                line.items.back().tail += static_cast<char>(kKern);
            }
        } else if (b >= 2 && b <= 7) {
            color_ = b;
            ++pos_;
        } else if (b < kFirstGlyphByte) {
            ++pos_;  // drawn as nothing
        } else {
            add_glyph(line);
        }
        return true;
    }

    bool message_step(std::vector<Line>& out) {
        Line& line = out.back();
        const unsigned char b = at(pos_);
        if (b == ' ' || b == '\t' || b == '\n') {
            ++pos_;  // layout of the source text, skipped by the drawer
            return true;
        }
        if (b == '_') {
            add_space(line, b);
            return true;
        }
        if (b != '~') {
            add_glyph(line);
            return true;
        }
        const unsigned char code = at(pos_ + 1);
        if (code == 0) {
            return false;
        }
        // Every code takes the byte after its letter, as an argument or as nothing.
        std::size_t end = std::min(pos_ + 3, text_.size());
        if (code == 'J') {
            // The timer's text runs to the first blank, which the drawer consumes with it.
            end = pos_ + 2;
            while (end < text_.size() && text_[end] != '\0' && text_[end] != ' ' && text_[end] != '\t') {
                ++end;
            }
            end = std::min(end + 1, text_.size());
        }
        std::string raw(text_.substr(pos_, end - pos_));
        const unsigned char arg = at(pos_ + 2);
        pos_ = end;
        if (code == 'C' && arg >= '0' && arg <= '7') {
            color_ = arg - '0';
        } else if (code == 'N' || code == 'P') {
            close(out, std::move(raw));
        } else if (code == 'E' || code == 'S') {
            close(out, std::move(raw));
        } else {
            if (code == 'T') {
                inset_ = true;
                line.inset = true;
            }
            add_code(line, std::move(raw));
        }
        return arg != 0;
    }

    std::string_view text_;
    TextDialect dialect_;
    std::size_t pos_ = 0;
    int color_ = kNoColor;
    bool inset_ = false;
};

class Preparer {
public:
    explicit Preparer(const PrepareOptions& options) : opt_(options), color_(options.initial_color) {}

    PreparedText run(std::string_view text) {
        std::vector<Line> lines = Parser(text, opt_.dialect, opt_.initial_color).lines();
        std::string out;
        for (Line& line : lines) {
            compose_marks(line);
            shape(line);
            for (Line& piece : wrap(std::move(line))) {
                emit(piece, out);
            }
        }
        PreparedText result;
        result.wrapped = wrapped_;
        result.changed = changed_;
        if (changed_) {
            result.text = std::move(out);
        }
        return result;
    }

private:
    void compose_marks(Line& line) {
        std::vector<Item>& items = line.items;
        for (std::size_t i = 1; i < items.size();) {
            const char32_t joined = !items[i].space && !items[i - 1].space && items[i - 1].tail.empty()
                                        ? compose(items[i - 1].cp, items[i].cp)
                                        : 0;
            if (joined == 0) {
                ++i;
                continue;
            }
            items[i - 1].cp = joined;
            items[i - 1].raw = 0;
            items[i - 1].tail = items[i].tail;
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
            changed_ = true;
        }
    }

    static std::u32string logical(const std::vector<Item>& items) {
        std::u32string s;
        s.reserve(items.size());
        for (const Item& item : items) {
            s += item.cp;
        }
        return s;
    }

    void shape(Line& line) {
        const std::u32string text = logical(line.items);
        if (!has_arabic(text)) {
            return;
        }
        const ShapedText shaped = shape_arabic(text);
        std::vector<Item> items;
        items.reserve(shaped.text.size());
        for (std::size_t i = 0; i < shaped.text.size(); ++i) {
            Item item = line.items[shaped.source[i]];
            if (item.cp != shaped.text[i]) {
                item.cp = shaped.text[i];
                item.raw = 0;
                changed_ = true;
            }
            items.push_back(std::move(item));
        }
        if (items.size() != line.items.size()) {
            changed_ = true;
        }
        line.items = std::move(items);
    }

    int advance(const Item& item) const {
        if (item.space) {
            return item.raw == '\v' ? opt_.wide_space_advance : opt_.space_advance;
        }
        if (invisible(item.cp) || !opt_.advance) {
            return 0;
        }
        char32_t code = item.cp;
        if (item.raw != 0) {
            code = item.raw == '!' ? U'\\' : item.raw == '&' ? U'^' : item.raw;
        }
        return opt_.advance(code) - static_cast<int>(item.tail.size());
    }

    // Whether a line may end after items[i] and the next begin with items[i + 1].
    static bool breaks_between(const Item& a, const Item& b) {
        if (a.space || b.space) {
            return false;  // spaces are handled by the caller: the break swallows them
        }
        if (a.cp == kZeroWidthSpace) {
            return true;
        }
        return (breaks_anywhere(a.cp) || breaks_anywhere(b.cp)) && !no_line_end(a.cp) && !no_line_start(b.cp);
    }

    std::vector<Line> wrap(Line line) {
        std::vector<Line> out;
        const int limit = line.inset && opt_.inset_wrap_width > 0 ? opt_.inset_wrap_width : opt_.wrap_width;
        if (opt_.dialect != TextDialect::Message || limit <= 0 || !opt_.advance) {
            out.push_back(std::move(line));
            return out;
        }
        constexpr std::size_t kNone = static_cast<std::size_t>(-1);
        const std::vector<Item>& items = line.items;
        const std::size_t n = items.size();
        std::vector<std::pair<std::size_t, std::size_t>> pieces;  // [first, last) of every output line but the last
        std::size_t start = 0;
        std::size_t break_end = kNone;  // the line may end here ...
        std::size_t break_next = kNone; // ... and the next one start here
        int width = 0;
        for (std::size_t i = 0; i < n;) {
            if (items[i].space) {
                // A break swallows the whole run of spaces.
                std::size_t run_end = i;
                while (run_end < n && items[run_end].space) {
                    width += advance(items[run_end]);
                    ++run_end;
                }
                if (i > start) {
                    break_end = i;
                    break_next = run_end;
                }
                i = run_end;
                continue;
            }
            if (i > start && breaks_between(items[i - 1], items[i])) {
                break_end = i;
                break_next = i;
            }
            const int w = advance(items[i]);
            if (width + w > limit && i > start) {
                if (break_end == kNone || break_end <= start) {
                    break_end = i;  // one word wider than the line: cut it where it overflows
                    break_next = i;
                }
                pieces.emplace_back(start, break_end);
                start = break_next;
                break_end = kNone;
                width = 0;
                for (std::size_t k = start; k < i; ++k) {
                    width += advance(items[k]);
                }
                continue;  // the same character again, against the new line
            }
            width += w;
            ++i;
        }
        if (pieces.empty()) {
            out.push_back(std::move(line));
            return out;
        }
        pieces.emplace_back(start, n);
        wrapped_ += static_cast<int>(pieces.size()) - 1;
        changed_ = true;
        for (std::size_t p = 0; p < pieces.size(); ++p) {
            Line piece;
            piece.inset = line.inset;
            piece.items.assign(items.begin() + static_cast<std::ptrdiff_t>(pieces[p].first),
                               items.begin() + static_cast<std::ptrdiff_t>(pieces[p].second));
            piece.terminator = "~N\n";
            piece.keeps_color = true;
            if (p == 0) {
                piece.prefix = line.prefix;
            }
            if (p + 1 == pieces.size()) {
                piece.suffix = line.suffix;
                piece.terminator = line.terminator;
                piece.end_color = line.end_color;
                piece.keeps_color = false;
            }
            out.push_back(std::move(piece));
        }
        return out;
    }

    void emit_color(int color, std::string& out) const {
        if (color == kNoColor) {
            return;  // the string never named its first colour, so there is nothing to write for it
        }
        if (opt_.dialect == TextDialect::Message) {
            out += "~C";
            out += static_cast<char>('0' + color);
        } else {
            out += static_cast<char>(color);
        }
    }

    void emit(Line& line, std::string& out) {
        std::vector<Item>& items = line.items;
        const std::u32string text = logical(items);
        std::vector<std::size_t> order(items.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::vector<std::uint8_t> levels(items.size(), 0);
        if (opt_.base == Direction::Rtl || has_rtl(text)) {
            BidiLine bidi = bidi_line(text, opt_.base);
            if (!std::is_sorted(bidi.order.begin(), bidi.order.end()) || bidi.order.size() != items.size()) {
                changed_ = true;
            }
            order = std::move(bidi.order);
            levels = std::move(bidi.levels);
        }

        out += line.prefix;
        for (const std::size_t i : order) {
            const Item& item = items[i];
            if (invisible(item.cp)) {
                changed_ = true;
                continue;
            }
            if (item.color != color_) {
                emit_color(item.color, out);
                color_ = item.color;
            }
            const bool reversed = (levels[i] & 1) != 0;
            if (item.raw != 0) {
                const unsigned char flipped =
                    reversed ? mirrored_byte(item.raw, opt_.dialect == TextDialect::Plain) : 0;
                if (flipped != 0) {
                    changed_ = true;
                }
                out += static_cast<char>(flipped != 0 ? flipped : item.raw);
            } else {
                const char32_t cp = reversed ? mirrored(item.cp) : item.cp;
                if (cp != item.cp) {
                    changed_ = true;
                }
                append_utf8(out, cp);
            }
            out += item.tail;
        }
        // The drawer leaves the line in the colour its last written code set, whatever was drawn last.
        if (!line.keeps_color && color_ != line.end_color) {
            emit_color(line.end_color, out);
            color_ = line.end_color;
        }
        out += line.suffix;
        out += line.terminator;
    }

    const PrepareOptions& opt_;
    int color_ = kNoColor;  // the colour the drawer is in at this point of the output
    bool changed_ = false;
    int wrapped_ = 0;
};

}  // namespace

char32_t drawn_character(unsigned char byte) {
    switch (byte) {
        case '*': return U'=';
        case '=': return U'—';
        case '@': return U'★';
        case '[': return U'“';
        case ']': return U'”';
        case '\\': return U'!';
        case '^': return U'&';
        case '`': return U'’';
        default: return byte;
    }
}

PreparedText prepare_text(std::string_view text, const PrepareOptions& options) {
    // Nothing to compose, shape or reorder in plain ASCII of a left-to-right language; only wrapping could apply.
    const bool wraps = options.dialect == TextDialect::Message && options.wrap_width > 0 && options.advance;
    if (options.base == Direction::Ltr && !wraps &&
        std::none_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x80; })) {
        return {};
    }
    return Preparer(options).run(text);
}

}  // namespace sh::i18n
