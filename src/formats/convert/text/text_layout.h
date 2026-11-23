#pragma once

#include "formats/common/status.h"
#include "formats/convert/text/text_codec.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::convert::text {

// One distinct NUL-terminated string in the file. Several table entries can point at the same slot (the game's
// compiler merges equal literals, e.g. the empty line of the credits).
struct Slot {
    std::uint32_t offset;    // file offset (of the decrypted image) of the first byte
    std::uint32_t length;    // bytes before the NUL
    std::uint32_t capacity;  // bytes the slot may use, NUL included: the string, its NUL and the zero padding
                             // up to the next 4-byte boundary
};

struct Entry {
    bool null = false;        // the pointer is 0
    std::uint32_t slot = 0;   // index into Layout::slots when not null
    bool alias = false;       // another, earlier entry owns the slot
    std::uint32_t owner_table = 0;
    std::uint32_t owner_id = 0;
};

struct Table {
    std::string name;
    std::uint32_t vram = 0;
    std::uint32_t file_offset = 0;
    Dialect dialect = Dialect::Ascii;
    std::vector<Entry> entries;
};

// Where the text of one game file is. `image` is the file as the CPU sees it: BODYPROG.BIN is stored XOR-encrypted
// on the disc and is decrypted here.
struct Layout {
    std::string game_path;      // normalised: upper case, '/' separators
    std::uint32_t base = 0;     // memory address of file offset 0
    bool encrypted = false;
    std::vector<std::uint8_t> image;
    std::vector<Slot> slots;    // in order of first reference
    std::vector<Table> tables;
};

// Finds the string tables of the file. Fails when the path is not a known text file, or when a table does not
// fit the bytes (pointer out of the file, string without NUL, strings that overlap each other or a table).
Result<Layout> analyze(std::span<const std::uint8_t> raw, std::string_view game_path);

// The game's XOR gamma over whole 32-bit words (Fs_DecryptOverlay). It is its own inverse.
// The size must be a multiple of 4.
std::vector<std::uint8_t> xor_gamma(std::span<const std::uint8_t> data);

}  // namespace sh::convert::text
