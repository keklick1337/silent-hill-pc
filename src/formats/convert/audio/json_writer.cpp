#include "formats/convert/audio/json_writer.h"

#include <cstdio>
#include <utility>

namespace sh::convert::audio {

void JsonWriter::newline_indent(std::size_t depth) {
    out_ += '\n';
    out_.append(depth * 2, ' ');
}

void JsonWriter::before_value() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    if (stack_.empty()) {
        return;
    }
    Level& top = stack_.back();
    if (top.has_items) {
        out_ += top.is_inline ? ", " : ",";
    } else if (top.is_inline) {
        out_ += ' ';
    }
    if (!top.is_inline) {
        newline_indent(stack_.size());
    }
    top.has_items = true;
}

void JsonWriter::open(char bracket, bool inline_) {
    before_value();
    out_ += bracket;
    const bool nested_inline = inline_ || (!stack_.empty() && stack_.back().is_inline);
    stack_.push_back({nested_inline, false});
}

void JsonWriter::close(char bracket) {
    const Level top = stack_.back();
    stack_.pop_back();
    if (top.has_items) {
        if (top.is_inline) {
            out_ += ' ';
        } else {
            newline_indent(stack_.size());
        }
    }
    out_ += bracket;
    if (stack_.empty()) {
        out_ += '\n';
    }
}

void JsonWriter::key(std::string_view name) {
    value(name);  // places the separator and indent like any other item
    out_ += ": ";
    after_key_ = true;
}

void JsonWriter::value(std::string_view text) {
    before_value();
    out_ += '"';
    for (const char c : text) {
        switch (c) {
            case '"': out_ += "\\\""; break;
            case '\\': out_ += "\\\\"; break;
            case '\n': out_ += "\\n"; break;
            case '\t': out_ += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out_ += buf;
                } else {
                    out_ += c;
                }
        }
    }
    out_ += '"';
}

void JsonWriter::value(bool flag) {
    before_value();
    out_ += flag ? "true" : "false";
}

void JsonWriter::write_signed(std::int64_t number) {
    before_value();
    out_ += std::to_string(number);
}

void JsonWriter::write_unsigned(std::uint64_t number) {
    before_value();
    out_ += std::to_string(number);
}

void JsonWriter::value_fixed(double number, int decimals) {
    before_value();
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, number);
    out_ += buf;
}

void JsonWriter::value_hex16(std::uint16_t number) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%04X", number);
    value(std::string_view(buf));
}

void JsonWriter::null() {
    before_value();
    out_ += "null";
}

std::string JsonWriter::take() {
    std::string result = std::move(out_);
    out_.clear();
    stack_.clear();
    after_key_ = false;
    return result;
}

}  // namespace sh::convert::audio
