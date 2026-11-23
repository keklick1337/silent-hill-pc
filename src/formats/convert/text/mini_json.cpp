#include "formats/convert/text/mini_json.h"

#include <cstddef>

namespace sh::convert::text::json {

namespace {

constexpr int kMaxDepth = 32;

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Result<Value> run() {
        Value value;
        if (auto r = parse_value(value, 0); !r) {
            return Result<Value>::fail(r.error());
        }
        skip_space();
        if (pos_ != text_.size()) {
            return Result<Value>::fail(where("unexpected data after the document"));
        }
        return Result<Value>::ok(std::move(value));
    }

private:
    std::string where(const std::string& message) const {
        return message + " at byte " + std::to_string(pos_);
    }

    void skip_space() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool consume(char c) {
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool consume_word(std::string_view word) {
        if (text_.substr(pos_, word.size()) == word) {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    Result<void> parse_value(Value& out, int depth) {
        if (depth > kMaxDepth) {
            return Result<void>::fail(where("nesting too deep"));
        }
        skip_space();
        if (pos_ >= text_.size()) {
            return Result<void>::fail(where("unexpected end"));
        }
        const char c = text_[pos_];
        if (c == '{') {
            return parse_object(out, depth);
        }
        if (c == '[') {
            return parse_array(out, depth);
        }
        if (c == '"') {
            out.kind = Value::Kind::String;
            return parse_string(out.string);
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parse_integer(out);
        }
        if (consume_word("true")) {
            out.kind = Value::Kind::Bool;
            out.boolean = true;
            return Result<void>::ok();
        }
        if (consume_word("false")) {
            out.kind = Value::Kind::Bool;
            out.boolean = false;
            return Result<void>::ok();
        }
        if (consume_word("null")) {
            out.kind = Value::Kind::Null;
            return Result<void>::ok();
        }
        return Result<void>::fail(where("unexpected character"));
    }

    Result<void> parse_object(Value& out, int depth) {
        out.kind = Value::Kind::Object;
        ++pos_;
        skip_space();
        if (consume('}')) {
            return Result<void>::ok();
        }
        while (true) {
            skip_space();
            std::string key;
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                return Result<void>::fail(where("object key expected"));
            }
            if (auto r = parse_string(key); !r) {
                return r;
            }
            skip_space();
            if (!consume(':')) {
                return Result<void>::fail(where("':' expected"));
            }
            Value member;
            if (auto r = parse_value(member, depth + 1); !r) {
                return r;
            }
            out.keys.push_back(std::move(key));
            out.items.push_back(std::move(member));
            skip_space();
            if (consume(',')) {
                continue;
            }
            if (consume('}')) {
                return Result<void>::ok();
            }
            return Result<void>::fail(where("',' or '}' expected"));
        }
    }

    Result<void> parse_array(Value& out, int depth) {
        out.kind = Value::Kind::Array;
        ++pos_;
        skip_space();
        if (consume(']')) {
            return Result<void>::ok();
        }
        while (true) {
            Value item;
            if (auto r = parse_value(item, depth + 1); !r) {
                return r;
            }
            out.items.push_back(std::move(item));
            skip_space();
            if (consume(',')) {
                continue;
            }
            if (consume(']')) {
                return Result<void>::ok();
            }
            return Result<void>::fail(where("',' or ']' expected"));
        }
    }

    Result<void> parse_integer(Value& out) {
        const bool negative = consume('-');
        if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
            return Result<void>::fail(where("digit expected"));
        }
        std::int64_t value = 0;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            if (value > (INT64_MAX - 9) / 10) {
                return Result<void>::fail(where("number too large"));
            }
            value = value * 10 + (text_[pos_] - '0');
            ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E')) {
            return Result<void>::fail(where("only integers are supported"));
        }
        out.kind = Value::Kind::Integer;
        out.integer = negative ? -value : value;
        return Result<void>::ok();
    }

    static void append_utf8(std::string& out, std::uint32_t cp) {
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

    bool read_hex4(std::uint32_t& value) {
        if (pos_ + 4 > text_.size()) {
            return false;
        }
        value = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = text_[pos_ + k];
            int digit;
            if (c >= '0' && c <= '9') {
                digit = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                digit = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                digit = c - 'A' + 10;
            } else {
                return false;
            }
            value = value * 16 + static_cast<std::uint32_t>(digit);
        }
        pos_ += 4;
        return true;
    }

    Result<void> parse_string(std::string& out) {
        ++pos_;
        out.clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return Result<void>::ok();
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return Result<void>::fail(where("control character in a string"));
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) {
                break;
            }
            const char e = text_[pos_++];
            switch (e) {
                case '"':
                case '\\':
                case '/':
                    out += e;
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!read_hex4(cp)) {
                        return Result<void>::fail(where("bad \\u escape"));
                    }
                    if (cp >= 0xD800 && cp < 0xDC00) {
                        std::uint32_t low = 0;
                        if (!consume('\\') || !consume('u') || !read_hex4(low) || low < 0xDC00 || low > 0xDFFF) {
                            return Result<void>::fail(where("bad surrogate pair"));
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    append_utf8(out, cp);
                    break;
                }
                default:
                    return Result<void>::fail(where("bad escape"));
            }
        }
        return Result<void>::fail(where("unterminated string"));
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

}  // namespace

const Value* Value::find(std::string_view key) const {
    if (kind != Kind::Object) {
        return nullptr;
    }
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == key) {
            return &items[i];
        }
    }
    return nullptr;
}

Result<Value> parse(std::string_view text) {
    return Parser(text).run();
}

std::string quote(std::string_view text) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out = "\"";
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (u < 0x20 || u == 0x7F) {
            out += "\\u00";
            out += kHex[u >> 4];
            out += kHex[u & 0xF];
        } else {
            out += c;
        }
    }
    out += '"';
    return out;
}

}  // namespace sh::convert::text::json
