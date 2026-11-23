#pragma once

// The project's JSON reader and writer (the one JSON implementation, in sh_common). No dependencies.

#include "formats/common/status.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace sh::json {

// A parsed JSON value. Strict RFC 8259 syntax, nesting limited to 64 levels. Integers that fit in int64 are kept
// exactly; other numbers are doubles. Object members keep the file order. Every value remembers the line and
// column where it starts, so callers can point at the place in the file when a value is wrong.
class Value {
public:
    enum class Type : std::uint8_t { Null, Bool, Integer, Real, String, Array, Object };
    using Member = std::pair<std::string, Value>;

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_integer() const { return type_ == Type::Integer; }
    bool is_number() const { return type_ == Type::Integer || type_ == Type::Real; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool boolean() const { return bool_; }
    std::int64_t integer() const { return integer_; }
    // Integers converted, reals as parsed.
    double number() const { return type_ == Type::Integer ? static_cast<double>(integer_) : real_; }
    const std::string& string() const { return string_; }
    const std::vector<Value>& array() const { return array_; }
    const std::vector<Member>& members() const { return members_; }

    // Null when this is not an object or the key is absent. A repeated key: the last one wins.
    const Value* find(std::string_view key) const;

    // 1-based position of the first character of the value in the parsed text; 0 for built values.
    std::uint32_t line() const { return line_; }
    std::uint32_t column() const { return column_; }

    // "integer", "string", ... for messages.
    static const char* type_name(Type type);

    // Built values (merging, generated data). They have no position (line 0).
    static Value null_value() { return Value(); }
    static Value from_bool(bool flag);
    static Value from_integer(std::int64_t number);
    static Value from_real(double number);
    static Value from_string(std::string text);
    static Value empty_array();
    static Value empty_object();

    // Mutable access for building and merging. A repeated key in a parsed object stays repeated: set() replaces
    // the last one (the one find() answers) and erase() removes every copy.
    std::vector<Value>& array_mut() { return array_; }
    std::vector<Member>& members_mut() { return members_; }
    Value* find_mut(std::string_view key);
    // Turns a non-object into an empty object first. Returns the stored value.
    Value& set(std::string_view key, Value value);
    bool erase(std::string_view key);

    // Same type and content; positions and member order of objects do not count, repeated keys compare by the
    // last copy.
    friend bool operator==(const Value& a, const Value& b);

private:
    friend class Parser;

    Type type_ = Type::Null;
    bool bool_ = false;
    std::int64_t integer_ = 0;
    double real_ = 0;
    std::string string_;
    std::vector<Value> array_;
    std::vector<Member> members_;
    std::uint32_t line_ = 0;
    std::uint32_t column_ = 0;
};

// Parses a whole document. The error names the line and column: "line 3, column 14: expected ',' or '}'".
// A UTF-8 byte order mark at the start is skipped.
Result<Value> parse(std::string_view text);

// The text as a JSON string literal, with quotes. Bytes >= 0x80 are copied through (UTF-8 in, UTF-8 out).
std::string quote(std::string_view text);

// Shortest decimal text that reads back as the same double ("15.83984375", "64", "-0.5"). Non-finite values are
// not JSON and give "0".
std::string format_number(double number);

// The value as JSON text (members in their order, reals as format_number). parse(write(v)) == v.
std::string write(const Value& value, bool pretty = false);

// Streaming JSON text builder. Commas and the "key": prefix are inserted by the writer; the caller only states the
// structure. Used for the glTF JSON chunk, glTF "extras", the *.model.json sidecars and gamedata/tables.
//
// Misuse (a value without a key inside an object, a key outside an object, unbalanced end_*) is a programming error:
// it is caught by assert in debug builds and produces unspecified text in release builds.
//
// Numbers: integers are written exactly; floating point is written with enough digits to round-trip a float
// (9 significant digits) or a double (17), or with value_shortest() as the shortest round-trip text.
// Non-finite values are not JSON and are written as 0.
class Writer {
public:
    // pretty: two-space indentation and one entry per line (sidecars); otherwise compact (glTF chunk).
    explicit Writer(bool pretty = false) : pretty_(pretty) {}

    Writer& begin_object();
    Writer& end_object();
    Writer& begin_array();
    Writer& end_array();

    // Pretty mode only: the next container is written on one line ({"a": 1, "b": 2}). Its children stay inline.
    Writer& inline_next();

    // Object member name; the next call must supply the value.
    Writer& key(std::string_view name);

    Writer& value(std::string_view text);
    Writer& value(const char* text) { return value(std::string_view(text)); }
    Writer& value(bool flag);
    Writer& value(std::int64_t number);
    Writer& value(std::uint64_t number);
    // Every other integer type; int64_t is long on some platforms and long long on others.
    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, std::int64_t> &&
                 !std::is_same_v<T, std::uint64_t>)
    Writer& value(T number) {
        if constexpr (std::is_signed_v<T>) {
            return value(static_cast<std::int64_t>(number));
        } else {
            return value(static_cast<std::uint64_t>(number));
        }
    }
    Writer& value_float(float number);
    Writer& value_double(double number);
    Writer& value_shortest(double number);
    Writer& value(float number) { return value_float(number); }
    Writer& value(double number) { return value_double(number); }
    Writer& null();

    // Already serialised JSON (for example another Writer's output). Not validated.
    Writer& raw(std::string_view json_text);

    // Shorthand for key(name).value(...).
    template <typename T>
    Writer& member(std::string_view name, const T& v) {
        key(name);
        return value(v);
    }
    Writer& member_float(std::string_view name, float v) {
        key(name);
        return value_float(v);
    }
    Writer& member_double(std::string_view name, double v) {
        key(name);
        return value_double(v);
    }

    // The text written so far. Complete once every begin_* has its end_*.
    const std::string& str() const { return out_; }
    std::string take() { return std::move(out_); }

private:
    struct Level {
        bool is_object;
        bool has_entries;
        bool is_inline;
    };

    void before_value();
    void separator();
    void open(char bracket, bool is_object);
    void close(char bracket);
    void newline_indent();
    void append_escaped(std::string_view text);
    bool inline_now() const { return !stack_.empty() && stack_.back().is_inline; }

    bool pretty_;
    bool key_pending_ = false;
    bool inline_pending_ = false;
    std::string out_;
    std::vector<Level> stack_;
};

}  // namespace sh::json
