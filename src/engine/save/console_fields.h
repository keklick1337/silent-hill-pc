// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Field-by-field access to a little-endian byte image of the console's records: one visit function names every value
// and its byte offset, and runs with a reader (bytes -> value) or a writer (value -> bytes). The converters that need
// the console's layout (console_records.cpp, entities/console_character.cpp) describe it this way, once per record.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace sh::save::console {

template <class T>
using Unsigned = std::make_unsigned_t<std::conditional_t<std::is_same_v<T, bool>, std::uint8_t, T>>;

// Reads values out of `bytes`; a value past the end reads as zero.
class FieldReader {
public:
    explicit FieldReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    static constexpr bool kWrites = false;

    template <class T>
    void field(std::size_t at, T& value) const {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
        using U = Unsigned<T>;
        U v = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            if (at + i < bytes_.size()) {
                v = static_cast<U>(v | static_cast<U>(static_cast<U>(bytes_[at + i]) << (8 * i)));
            }
        }
        value = static_cast<T>(v);
    }

    // A value the engine keeps wider than the console's `Console` type: read in that type, sign- or zero-extended.
    template <class Console, class T>
    void narrow(std::size_t at, T& value) const {
        static_assert(sizeof(Console) < sizeof(T));
        Console c{};
        field(at, c);
        value = static_cast<T>(c);
    }

private:
    std::span<const std::uint8_t> bytes_;
};

// Writes values into `bytes`; a value past the end is dropped.
class FieldWriter {
public:
    explicit FieldWriter(std::span<std::uint8_t> bytes) : bytes_(bytes) {}
    static constexpr bool kWrites = true;

    template <class T>
    void field(std::size_t at, const T& value) const {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
        using U = Unsigned<T>;
        const U v = static_cast<U>(value);
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            if (at + i < bytes_.size()) {
                bytes_[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
            }
        }
    }

    // A value the engine keeps wider than the console's `Console` type (a character, item, map or attack number a mod
    // may take past the console's range). One that does not fit is written as all ones -- no item, no slot, no
    // character the console knows, a count saturated at its maximum -- and counted in narrowed().
    template <class Console, class T>
    void narrow(std::size_t at, const T& value) const {
        static_assert(sizeof(Console) < sizeof(T));
        if (std::in_range<Console>(value)) {
            field(at, static_cast<Console>(value));
        } else {
            field(at, static_cast<Console>(~Unsigned<Console>{0}));
            ++narrowed_;
        }
    }

    // How many values narrow() could not write as they are.
    std::size_t narrowed() const { return narrowed_; }

private:
    std::span<std::uint8_t> bytes_;
    mutable std::size_t narrowed_ = 0;
};

// The record type a visit takes: const for a writer (it only reads the record), mutable for a reader.
template <class Io, class T>
using Visited = std::conditional_t<Io::kWrites, const T, T>;

}  // namespace sh::save::console
