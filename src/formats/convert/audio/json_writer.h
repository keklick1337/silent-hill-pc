#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace sh::convert::audio {

// Writes indented JSON. A container opened with `inline_` stays on one line, which keeps one tone or one stream per
// line in the output. Keys keep the order they are written in.
class JsonWriter {
public:
    void begin_object(bool inline_ = false) { open('{', inline_); }
    void end_object() { close('}'); }
    void begin_array(bool inline_ = false) { open('[', inline_); }
    void end_array() { close(']'); }

    void key(std::string_view name);

    void value(std::string_view text);
    void value(const char* text) { value(std::string_view(text)); }
    void value(bool flag);
    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
    void value(T number) {
        if constexpr (std::is_signed_v<T>) {
            write_signed(static_cast<std::int64_t>(number));
        } else {
            write_unsigned(static_cast<std::uint64_t>(number));
        }
    }
    // Fixed number of decimals, for durations.
    void value_fixed(double number, int decimals);
    void null();

    // Hex string "0x%04X" for register values.
    void value_hex16(std::uint16_t number);

    template <typename T>
    void field(std::string_view name, const T& v) {
        key(name);
        value(v);
    }

    // The finished document, newline-terminated.
    std::string take();

private:
    struct Level {
        bool is_inline;
        bool has_items;
    };

    void write_signed(std::int64_t number);
    void write_unsigned(std::uint64_t number);
    void open(char bracket, bool inline_);
    void close(char bracket);
    void before_value();
    void newline_indent(std::size_t depth);

    std::string out_;
    std::vector<Level> stack_;
    bool after_key_ = false;
};

}  // namespace sh::convert::audio
