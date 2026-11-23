#pragma once

#include <optional>
#include <string>
#include <utility>

namespace sh {

// Outcome of an operation that can fail. Used on module boundaries instead of exceptions.
template <typename T>
class Result {
public:
    static Result ok(T value) { return Result(std::move(value), std::string()); }
    static Result fail(std::string message) { return Result(std::nullopt, std::move(message)); }

    explicit operator bool() const { return value_.has_value(); }
    const T& value() const { return *value_; }
    T& value() { return *value_; }
    const std::string& error() const { return error_; }

private:
    Result(std::optional<T> value, std::string error) : value_(std::move(value)), error_(std::move(error)) {}

    std::optional<T> value_;
    std::string error_;
};

// Success or an error message, with no value.
template <>
class Result<void> {
public:
    static Result ok() { return Result(true, std::string()); }
    static Result fail(std::string message) { return Result(false, std::move(message)); }

    explicit operator bool() const { return success_; }
    const std::string& error() const { return error_; }

private:
    Result(bool success, std::string error) : success_(success), error_(std::move(error)) {}

    bool success_;
    std::string error_;
};

}  // namespace sh
