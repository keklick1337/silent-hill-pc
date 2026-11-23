// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/utf8/utf8.h"

#include <string>

namespace sh::utf8 {
namespace {

// Returns the byte length of the sequence at s[i], or 0 on error. Rejects
// overlong encodings, surrogates, codepoints beyond U+10FFFF, and truncated
// or non-continuation bytes.
int sequence_length(std::string_view s, std::size_t i) {
    const unsigned char b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) {
        return 1;
    }
    int len;
    char32_t cp;
    if ((b0 & 0xE0) == 0xC0) {
        len = 2;
        cp = b0 & 0x1Fu;
    } else if ((b0 & 0xF0) == 0xE0) {
        len = 3;
        cp = b0 & 0x0Fu;
    } else if ((b0 & 0xF8) == 0xF0) {
        len = 4;
        cp = b0 & 0x07u;
    } else {
        return 0;
    }
    if (i + static_cast<std::size_t>(len) > s.size()) {
        return 0;
    }
    for (int k = 1; k < len; ++k) {
        const unsigned char b = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
        if ((b & 0xC0) != 0x80) {
            return 0;
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)) {
        return 0;
    }
    if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
        return 0;
    }
    return len;
}

}  // namespace

Result<std::vector<char32_t>> decode(std::string_view s) {
    std::vector<char32_t> out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const int len = sequence_length(s, i);
        if (len == 0) {
            return Result<std::vector<char32_t>>::fail("invalid UTF-8 at byte offset " + std::to_string(i));
        }
        char32_t cp = static_cast<unsigned char>(s[i]);
        if (len > 1) {
            cp &= (len == 2 ? 0x1Fu : len == 3 ? 0x0Fu : 0x07u);
            for (int k = 1; k < len; ++k) {
                cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3F);
            }
        }
        out.push_back(cp);
        i += static_cast<std::size_t>(len);
    }
    return Result<std::vector<char32_t>>::ok(std::move(out));
}

bool valid(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const int len = sequence_length(s, i);
        if (len == 0) {
            return false;
        }
        i += static_cast<std::size_t>(len);
    }
    return true;
}

std::size_t codepoint_count(std::string_view s) {
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        const int len = sequence_length(s, i);
        if (len == 0) {
            return 0;
        }
        ++count;
        i += static_cast<std::size_t>(len);
    }
    return count;
}

bool advance(std::string_view& s, std::size_t codepoints) {
    std::size_t i = 0;
    for (std::size_t n = 0; n < codepoints; ++n) {
        if (i >= s.size()) {
            return false;
        }
        const int len = sequence_length(s, i);
        if (len == 0) {
            return false;
        }
        i += static_cast<std::size_t>(len);
    }
    s.remove_prefix(i);
    return true;
}

}  // namespace sh::utf8
