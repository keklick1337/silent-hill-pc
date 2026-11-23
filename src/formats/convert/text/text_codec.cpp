#include "formats/convert/text/text_codec.h"

#include <cstddef>

namespace sh::convert::text {

namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

void append_hex_escape(std::string& out, std::uint8_t byte) {
    out += "\\x";
    out += kHexDigits[byte >> 4];
    out += kHexDigits[byte & 0xF];
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool is_printable(std::uint8_t c) {
    return c >= 0x20 && c <= 0x7E;
}

bool is_digit(std::uint8_t c) {
    return c >= '0' && c <= '9';
}

// The engine skips these between tokens of a map message, and consumes one of them as the argument of a command
// that has none (`~N `).
bool is_layout(std::uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n';
}

std::string plain_text(std::span<const std::uint8_t> bytes, bool underscore_is_space) {
    std::string out;
    out.reserve(bytes.size());
    for (const std::uint8_t c : bytes) {
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == ' ') {
            out += underscore_is_space ? "\\s" : " ";
        } else if (c == '_' && underscore_is_space) {
            out += ' ';
        } else if (is_printable(c)) {
            out += static_cast<char>(c);
        } else {
            append_hex_escape(out, c);
        }
    }
    return out;
}

std::optional<std::string> markup_text(std::span<const std::uint8_t> bytes) {
    std::string out;
    const std::size_t size = bytes.size();
    std::size_t i = 0;
    while (i < size) {
        const std::uint8_t c = bytes[i];
        if (c == '~') {
            if (i + 1 >= size || bytes[i + 1] < 'A' || bytes[i + 1] > 'Z') {
                return std::nullopt;
            }
            const std::uint8_t letter = bytes[i + 1];
            out += '{';
            out += static_cast<char>(letter);
            if (letter == 'J') {
                std::size_t j = i + 2;
                // The game skips to the next space or tab. A newline before it (`~J1(2.4)\n\t`) is skipped with the
                // rest, which is the same as ending the argument there as long as a space or tab follows it.
                while (j < size && !is_layout(bytes[j])) {
                    if (bytes[j] < 0x21 || bytes[j] > 0x7E || bytes[j] == '{' || bytes[j] == '}' || bytes[j] == '\\') {
                        return std::nullopt;
                    }
                    out += static_cast<char>(bytes[j]);
                    ++j;
                }
                if (j == i + 2 || (j < size && bytes[j] == '\n' && j + 1 < size && bytes[j + 1] != ' ' &&
                                   bytes[j + 1] != '\t')) {
                    return std::nullopt;
                }
                out += '}';
                i = j;
                continue;
            }
            if (i + 2 >= size) {
                out += '}';
                i = size;
                continue;
            }
            const std::uint8_t arg = bytes[i + 2];
            if (is_digit(arg)) {
                out += static_cast<char>(arg);
            } else if (!is_layout(arg)) {
                return std::nullopt;
            }
            out += '}';
            i += 3;
            continue;
        }
        ++i;
        if (is_layout(c)) {
            continue;
        }
        if (c == '_') {
            out += ' ';
        } else if (c == '\\' || c == '{' || c == '}') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (is_printable(c)) {
            out += static_cast<char>(c);
        } else {
            append_hex_escape(out, c);
        }
    }
    return out;
}

// Reads the escape after a backslash at text[i] (the backslash itself). Returns the byte and advances i past it.
Result<std::uint8_t> read_escape(std::string_view text, std::size_t& i) {
    if (i + 1 >= text.size()) {
        return Result<std::uint8_t>::fail("text ends after a backslash");
    }
    const char e = text[i + 1];
    switch (e) {
        case '\\':
        case '{':
        case '}':
            i += 2;
            return Result<std::uint8_t>::ok(static_cast<std::uint8_t>(e));
        case 't':
            i += 2;
            return Result<std::uint8_t>::ok('\t');
        case 'n':
            i += 2;
            return Result<std::uint8_t>::ok('\n');
        case 's':
            i += 2;
            return Result<std::uint8_t>::ok(' ');
        case 'x': {
            if (i + 3 >= text.size()) {
                return Result<std::uint8_t>::fail("\\x needs two hex digits");
            }
            const int hi = hex_value(text[i + 2]);
            const int lo = hex_value(text[i + 3]);
            if (hi < 0 || lo < 0) {
                return Result<std::uint8_t>::fail("\\x needs two hex digits");
            }
            i += 4;
            return Result<std::uint8_t>::ok(static_cast<std::uint8_t>(hi * 16 + lo));
        }
        default:
            return Result<std::uint8_t>::fail(std::string("unknown escape \\") + e);
    }
}

Result<std::vector<std::uint8_t>> plain_bytes(std::string_view text, bool underscore_is_space) {
    std::vector<std::uint8_t> out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<std::uint8_t>(text[i]);
        if (c == '\\') {
            const auto escaped = read_escape(text, i);
            if (!escaped) {
                return Result<std::vector<std::uint8_t>>::fail(escaped.error());
            }
            out.push_back(escaped.value());
            continue;
        }
        if (!is_printable(c)) {
            return Result<std::vector<std::uint8_t>>::fail("text has a character outside printable ASCII; use \\xHH");
        }
        out.push_back(c == ' ' && underscore_is_space ? '_' : c);
        ++i;
    }
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

Result<std::vector<std::uint8_t>> markup_bytes(std::string_view text) {
    using R = Result<std::vector<std::uint8_t>>;
    std::vector<std::uint8_t> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<std::uint8_t>(text[i]);
        if (c == '{') {
            const std::size_t close = text.find('}', i);
            if (close == std::string_view::npos) {
                return R::fail("unclosed {");
            }
            const std::string_view body = text.substr(i + 1, close - i - 1);
            if (body.empty() || body[0] < 'A' || body[0] > 'Z') {
                return R::fail("a {command} starts with a capital letter");
            }
            const std::string_view arg = body.substr(1);
            out.push_back('~');
            out.push_back(static_cast<std::uint8_t>(body[0]));
            if (body[0] == 'J') {
                if (arg.empty()) {
                    return R::fail("{J} needs an argument, e.g. {J0(2.0)}");
                }
                for (const char a : arg) {
                    if (a < 0x21 || a > 0x7E || a == '{' || a == '\\') {
                        return R::fail("bad character in a {J} argument");
                    }
                    out.push_back(static_cast<std::uint8_t>(a));
                }
                out.push_back(' ');
            } else if (arg.empty()) {
                out.push_back(' ');
            } else if (arg.size() == 1 && is_digit(static_cast<std::uint8_t>(arg[0]))) {
                out.push_back(static_cast<std::uint8_t>(arg[0]));
                out.push_back(' ');
            } else {
                return R::fail("a {command} takes no argument or one digit");
            }
            i = close + 1;
            continue;
        }
        if (c == '}') {
            return R::fail("unmatched }; write \\}");
        }
        if (c == '~') {
            return R::fail("a literal ~ starts a command in the game; write {X} for commands");
        }
        if (c == '\\') {
            const auto escaped = read_escape(text, i);
            if (!escaped) {
                return R::fail(escaped.error());
            }
            out.push_back(escaped.value());
            continue;
        }
        if (!is_printable(c)) {
            return R::fail("text has a character outside printable ASCII; use \\xHH");
        }
        out.push_back(c == ' ' ? '_' : c);
        ++i;
    }
    return R::ok(std::move(out));
}

}  // namespace

std::string_view dialect_name(Dialect dialect) {
    switch (dialect) {
        case Dialect::Ascii:
            return "ascii";
        case Dialect::Game:
            return "game";
        case Dialect::Markup:
            return "markup";
    }
    return "ascii";
}

std::optional<Dialect> dialect_from_name(std::string_view name) {
    if (name == "ascii") {
        return Dialect::Ascii;
    }
    if (name == "game") {
        return Dialect::Game;
    }
    if (name == "markup") {
        return Dialect::Markup;
    }
    return std::nullopt;
}

std::optional<std::string> bytes_to_text(std::span<const std::uint8_t> bytes, Dialect dialect) {
    switch (dialect) {
        case Dialect::Ascii:
            return plain_text(bytes, false);
        case Dialect::Game:
            return plain_text(bytes, true);
        case Dialect::Markup: {
            auto text = markup_text(bytes);
            // The text must name the same commands and characters when it is encoded and read back.
            if (text) {
                const auto again = markup_bytes(*text);
                if (!again) {
                    return std::nullopt;
                }
                const auto back = markup_text(again.value());
                if (!back || *back != *text) {
                    return std::nullopt;
                }
            }
            return text;
        }
    }
    return std::nullopt;
}

Result<std::vector<std::uint8_t>> text_to_bytes(std::string_view text, Dialect dialect) {
    switch (dialect) {
        case Dialect::Ascii:
            return plain_bytes(text, false);
        case Dialect::Game:
            return plain_bytes(text, true);
        case Dialect::Markup:
            return markup_bytes(text);
    }
    return Result<std::vector<std::uint8_t>>::fail("unknown dialect");
}

std::string to_hex(std::span<const std::uint8_t> bytes) {
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        out += kHexDigits[b >> 4];
        out += kHexDigits[b & 0xF];
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> from_hex(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hex_value(hex[i]);
        const int lo = hex_value(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        out.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
    }
    return out;
}

}  // namespace sh::convert::text
