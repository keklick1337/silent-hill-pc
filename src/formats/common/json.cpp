#include "formats/common/json.h"

#include <cassert>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace sh::json {

const Value* Value::find(std::string_view key) const {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (std::size_t i = members_.size(); i-- > 0;) {
        if (members_[i].first == key) {
            return &members_[i].second;
        }
    }
    return nullptr;
}

const char* Value::type_name(Type type) {
    switch (type) {
        case Type::Null: return "null";
        case Type::Bool: return "boolean";
        case Type::Integer: return "integer";
        case Type::Real: return "number";
        case Type::String: return "string";
        case Type::Array: return "array";
        case Type::Object: return "object";
    }
    return "?";
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {
        if (text_.starts_with("\xEF\xBB\xBF")) {
            pos_ = 3;
            line_start_ = 3;
        }
    }

    Result<Value> run() {
        Value value;
        if (!parse_value(value, 0)) {
            return Result<Value>::fail(error_);
        }
        skip_space();
        if (pos_ != text_.size()) {
            return Result<Value>::fail(where("unexpected text after the value"));
        }
        return Result<Value>::ok(std::move(value));
    }

private:
    static constexpr int kMaxDepth = 64;

    std::uint32_t column() const { return static_cast<std::uint32_t>(pos_ - line_start_ + 1); }

    std::string where(const char* what) const {
        return "line " + std::to_string(line_) + ", column " + std::to_string(column()) + ": " + what;
    }

    bool fail(const char* what) {
        error_ = where(what);
        return false;
    }

    void skip_space() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '\n') {
                ++line_;
                line_start_ = pos_ + 1;
            } else if (c != ' ' && c != '\t' && c != '\r') {
                return;
            }
            ++pos_;
        }
    }

    bool literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) {
            return false;
        }
        pos_ += word.size();
        return true;
    }

    bool parse_value(Value& out, int depth) {
        if (depth > kMaxDepth) {
            return fail("nesting is too deep");
        }
        skip_space();
        if (pos_ >= text_.size()) {
            return fail("unexpected end of the text");
        }
        out.line_ = line_;
        out.column_ = column();
        const char c = text_[pos_];
        if (c == '{') {
            return parse_object(out, depth);
        }
        if (c == '[') {
            return parse_array(out, depth);
        }
        if (c == '"') {
            out.type_ = Value::Type::String;
            return parse_string(out.string_);
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parse_number(out);
        }
        if (literal("true")) {
            out.type_ = Value::Type::Bool;
            out.bool_ = true;
            return true;
        }
        if (literal("false")) {
            out.type_ = Value::Type::Bool;
            return true;
        }
        if (literal("null")) {
            return true;
        }
        return fail("unexpected character");
    }

    bool parse_object(Value& out, int depth) {
        out.type_ = Value::Type::Object;
        ++pos_;
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skip_space();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                return fail("expected a member name in quotes");
            }
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            skip_space();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                return fail("expected ':' after the member name");
            }
            ++pos_;
            Value value;
            if (!parse_value(value, depth + 1)) {
                return false;
            }
            out.members_.emplace_back(std::move(key), std::move(value));
            skip_space();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool parse_array(Value& out, int depth) {
        out.type_ = Value::Type::Array;
        ++pos_;
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            Value value;
            if (!parse_value(value, depth + 1)) {
                return false;
            }
            out.array_.push_back(std::move(value));
            skip_space();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or ']'");
        }
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

    bool hex4(std::uint32_t& value) {
        if (text_.size() - pos_ < 4) {
            return fail("short \\u escape");
        }
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                return fail("bad \\u escape");
            }
        }
        return true;
    }

    bool parse_string(std::string& out) {
        ++pos_;  // the opening quote
        while (true) {
            if (pos_ >= text_.size()) {
                return fail("unterminated string");
            }
            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') {
                return true;
            }
            if (c < 0x20) {
                --pos_;
                return fail("control character in a string (a missing closing quote?)");
            }
            if (c != '\\') {
                out += static_cast<char>(c);
                continue;
            }
            if (pos_ >= text_.size()) {
                return fail("unterminated escape");
            }
            const char e = text_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!hex4(cp)) {
                        return false;
                    }
                    if (cp >= 0xD800 && cp < 0xDC00) {
                        std::uint32_t low = 0;
                        if (!literal("\\u") || !hex4(low) || low < 0xDC00 || low > 0xDFFF) {
                            return fail("unpaired surrogate");
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp < 0xE000) {
                        return fail("unpaired surrogate");
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: return fail("unknown escape");
            }
        }
    }

    bool parse_number(Value& out) {
        const std::size_t start = pos_;
        if (text_[pos_] == '-') {
            ++pos_;
        }
        auto digits = [this]() {
            const std::size_t before = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
            return pos_ - before;
        };
        if (pos_ < text_.size() && text_[pos_] == '0') {
            ++pos_;
        } else if (digits() == 0) {
            return fail("bad number");
        }
        bool integral = true;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            integral = false;
            ++pos_;
            if (digits() == 0) {
                return fail("bad number: no digits after '.'");
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            integral = false;
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            if (digits() == 0) {
                return fail("bad number: no digits in the exponent");
            }
        }
        const char* first = text_.data() + start;
        const char* last = text_.data() + pos_;
        if (integral) {
            std::int64_t v = 0;
            const auto [ptr, ec] = std::from_chars(first, last, v);
            if (ec == std::errc() && ptr == last) {
                out.type_ = Value::Type::Integer;
                out.integer_ = v;
                return true;
            }
        }
        // from_chars does not depend on the C locale, unlike strtod.
        double d = 0;
        const auto [ptr, ec] = std::from_chars(first, last, d);
        if (ptr != last || (ec != std::errc() && ec != std::errc::result_out_of_range)) {
            return fail("bad number");
        }
        out.type_ = Value::Type::Real;
        out.real_ = d;
        return true;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::uint32_t line_ = 1;
    std::size_t line_start_ = 0;
    std::string error_;
};

Result<Value> parse(std::string_view text) {
    return Parser(text).run();
}

std::string quote(std::string_view text) {
    Writer writer;
    writer.value(text);
    return writer.take();
}

std::string format_number(double number) {
    if (!std::isfinite(number)) {
        return "0";
    }
    char buf[64];
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof buf, number);
    if (ec != std::errc()) {
        return "0";
    }
    return std::string(buf, ptr);
}

void Writer::newline_indent() {
    if (!pretty_) {
        return;
    }
    out_ += '\n';
    out_.append(stack_.size() * 2, ' ');
}

// The comma before an entry and, outside inline containers, the line break.
void Writer::separator() {
    Level& level = stack_.back();
    if (level.has_entries) {
        out_ += ',';
        if (level.is_inline && pretty_) {
            out_ += ' ';
        }
    }
    level.has_entries = true;
    if (!level.is_inline) {
        newline_indent();
    }
}

// Called before every value, container start and raw insert: emits the separating comma and the layout.
void Writer::before_value() {
    if (key_pending_) {
        key_pending_ = false;
        return;
    }
    if (stack_.empty()) {
        return;
    }
    assert(!stack_.back().is_object && "value inside an object needs a key");
    separator();
}

void Writer::append_escaped(std::string_view text) {
    out_ += '"';
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out_ += "\\\""; break;
            case '\\': out_ += "\\\\"; break;
            case '\n': out_ += "\\n"; break;
            case '\r': out_ += "\\r"; break;
            case '\t': out_ += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out_ += buf;
                } else {
                    out_ += ch;
                }
        }
    }
    out_ += '"';
}

void Writer::open(char bracket, bool is_object) {
    before_value();
    out_ += bracket;
    const bool is_inline = pretty_ && (inline_pending_ || inline_now());
    inline_pending_ = false;
    stack_.push_back({is_object, false, is_inline});
}

void Writer::close(char bracket) {
    const Level level = stack_.back();
    stack_.pop_back();
    if (level.has_entries && !level.is_inline) {
        newline_indent();
    }
    out_ += bracket;
}

Writer& Writer::begin_object() {
    open('{', true);
    return *this;
}

Writer& Writer::end_object() {
    assert(!stack_.empty() && stack_.back().is_object && !key_pending_);
    close('}');
    return *this;
}

Writer& Writer::begin_array() {
    open('[', false);
    return *this;
}

Writer& Writer::end_array() {
    assert(!stack_.empty() && !stack_.back().is_object);
    close(']');
    return *this;
}

Writer& Writer::inline_next() {
    inline_pending_ = true;
    return *this;
}

Writer& Writer::key(std::string_view name) {
    assert(!stack_.empty() && stack_.back().is_object && !key_pending_);
    separator();
    append_escaped(name);
    out_ += pretty_ ? ": " : ":";
    key_pending_ = true;
    return *this;
}

Writer& Writer::value(std::string_view text) {
    before_value();
    append_escaped(text);
    return *this;
}

Writer& Writer::value(bool flag) {
    before_value();
    out_ += flag ? "true" : "false";
    return *this;
}

Writer& Writer::value(std::int64_t number) {
    before_value();
    out_ += std::to_string(number);
    return *this;
}

Writer& Writer::value(std::uint64_t number) {
    before_value();
    out_ += std::to_string(number);
    return *this;
}

Writer& Writer::value_float(float number) {
    before_value();
    if (!std::isfinite(number)) {
        out_ += '0';
        return *this;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(number));
    out_ += buf;
    return *this;
}

Writer& Writer::value_double(double number) {
    before_value();
    if (!std::isfinite(number)) {
        out_ += '0';
        return *this;
    }
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", number);
    out_ += buf;
    return *this;
}

Writer& Writer::value_shortest(double number) {
    before_value();
    out_ += format_number(number);
    return *this;
}

Writer& Writer::null() {
    before_value();
    out_ += "null";
    return *this;
}

Writer& Writer::raw(std::string_view json_text) {
    before_value();
    out_ += json_text;
    return *this;
}

Value Value::from_bool(bool flag) {
    Value v;
    v.type_ = Type::Bool;
    v.bool_ = flag;
    return v;
}

Value Value::from_integer(std::int64_t number) {
    Value v;
    v.type_ = Type::Integer;
    v.integer_ = number;
    return v;
}

Value Value::from_real(double number) {
    Value v;
    v.type_ = Type::Real;
    v.real_ = number;
    return v;
}

Value Value::from_string(std::string text) {
    Value v;
    v.type_ = Type::String;
    v.string_ = std::move(text);
    return v;
}

Value Value::empty_array() {
    Value v;
    v.type_ = Type::Array;
    return v;
}

Value Value::empty_object() {
    Value v;
    v.type_ = Type::Object;
    return v;
}

Value* Value::find_mut(std::string_view key) {
    return const_cast<Value*>(std::as_const(*this).find(key));
}

Value& Value::set(std::string_view key, Value value) {
    if (type_ != Type::Object) {
        *this = empty_object();
    }
    if (Value* existing = find_mut(key)) {
        *existing = std::move(value);
        return *existing;
    }
    members_.emplace_back(std::string(key), std::move(value));
    return members_.back().second;
}

bool Value::erase(std::string_view key) {
    const std::size_t before = members_.size();
    std::erase_if(members_, [&](const Member& m) { return m.first == key; });
    return members_.size() != before;
}

bool operator==(const Value& a, const Value& b) {
    if (a.type_ != b.type_) {
        return false;
    }
    switch (a.type_) {
        case Value::Type::Null: return true;
        case Value::Type::Bool: return a.bool_ == b.bool_;
        case Value::Type::Integer: return a.integer_ == b.integer_;
        case Value::Type::Real: return a.real_ == b.real_;
        case Value::Type::String: return a.string_ == b.string_;
        case Value::Type::Array: return a.array_ == b.array_;
        case Value::Type::Object: {
            // Repeated keys: only the copy find() answers with counts.
            auto distinct = [](const Value& v) {
                std::size_t n = 0;
                for (const Value::Member& m : v.members_) {
                    if (v.find(m.first) == &m.second) {
                        ++n;
                    }
                }
                return n;
            };
            if (distinct(a) != distinct(b)) {
                return false;
            }
            for (const Value::Member& m : a.members_) {
                const Value* other = b.find(m.first);
                if (other == nullptr || !(*a.find(m.first) == *other)) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

namespace {

void write_value(Writer& w, const Value& v) {
    switch (v.type()) {
        case Value::Type::Null: w.null(); break;
        case Value::Type::Bool: w.value(v.boolean()); break;
        case Value::Type::Integer: w.value(v.integer()); break;
        case Value::Type::Real: {
            std::string text = format_number(v.number());
            // A real must read back as a real: "1" would come back as an integer.
            if (text.find_first_of(".eE") == std::string::npos) {
                text += ".0";
            }
            w.raw(text);
            break;
        }
        case Value::Type::String: w.value(std::string_view(v.string())); break;
        case Value::Type::Array:
            w.begin_array();
            for (const Value& e : v.array()) {
                write_value(w, e);
            }
            w.end_array();
            break;
        case Value::Type::Object:
            w.begin_object();
            for (const Value::Member& m : v.members()) {
                w.key(m.first);
                write_value(w, m.second);
            }
            w.end_object();
            break;
    }
}

}  // namespace

std::string write(const Value& value, bool pretty) {
    Writer w(pretty);
    write_value(w, value);
    return w.take();
}

}  // namespace sh::json
