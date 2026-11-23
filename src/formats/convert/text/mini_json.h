#pragma once

#include "formats/common/status.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sh::convert::text::json {

// A parsed JSON document. Only what the text files use: integers are kept exactly, other numbers are rejected.
struct Value {
    enum class Kind : std::uint8_t { Null, Bool, Integer, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    std::int64_t integer = 0;
    std::string string;
    std::vector<Value> items;          // Array elements, or Object values
    std::vector<std::string> keys;     // Object keys, in file order, parallel to items

    // The value of an object member, or null when this is not an object or has no such key.
    const Value* find(std::string_view key) const;
};

Result<Value> parse(std::string_view text);

// The text as a JSON string literal, with quotes. Bytes >= 0x80 are copied through (the files are UTF-8).
std::string quote(std::string_view text);

}  // namespace sh::convert::text::json
