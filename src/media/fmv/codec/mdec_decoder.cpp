// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/fmv/codec/mdec_decoder.h"

#include <array>

namespace sh::fmv {

namespace {

// One coefficient code of ISO/IEC 11172-2 table B.14 without its sign bit: the code, its length in bits, the run
// of zero coefficients before the value and the magnitude of the value.
struct CoefficientCode {
    std::uint16_t code;
    std::uint8_t length;
    std::uint8_t run;
    std::uint8_t level;
};

constexpr CoefficientCode kCoefficientCodes[] = {
    {0b11, 2, 0, 1},
    {0b011, 3, 1, 1},
    {0b0100, 4, 0, 2}, {0b0101, 4, 2, 1},
    {0b00101, 5, 0, 3}, {0b00110, 5, 4, 1}, {0b00111, 5, 3, 1},
    {0b000100, 6, 7, 1}, {0b000101, 6, 6, 1}, {0b000110, 6, 1, 2},
    {0b000111, 6, 5, 1},
    {0b0000100, 7, 2, 2}, {0b0000101, 7, 9, 1}, {0b0000110, 7, 0, 4},
    {0b0000111, 7, 8, 1},
    {0b00100000, 8, 13, 1}, {0b00100001, 8, 0, 6}, {0b00100010, 8, 12, 1},
    {0b00100011, 8, 11, 1}, {0b00100100, 8, 3, 2}, {0b00100101, 8, 1, 3},
    {0b00100110, 8, 0, 5}, {0b00100111, 8, 10, 1},
    {0b0000001000, 10, 16, 1}, {0b0000001001, 10, 5, 2}, {0b0000001010, 10, 0, 7},
    {0b0000001011, 10, 2, 3}, {0b0000001100, 10, 1, 4}, {0b0000001101, 10, 15, 1},
    {0b0000001110, 10, 14, 1}, {0b0000001111, 10, 4, 2},
    {0b000000010000, 12, 0, 11}, {0b000000010001, 12, 8, 2}, {0b000000010010, 12, 4, 3},
    {0b000000010011, 12, 0, 10}, {0b000000010100, 12, 2, 4}, {0b000000010101, 12, 7, 2},
    {0b000000010110, 12, 21, 1}, {0b000000010111, 12, 20, 1}, {0b000000011000, 12, 0, 9},
    {0b000000011001, 12, 19, 1}, {0b000000011010, 12, 18, 1}, {0b000000011011, 12, 1, 5},
    {0b000000011100, 12, 3, 3}, {0b000000011101, 12, 0, 8}, {0b000000011110, 12, 6, 2},
    {0b000000011111, 12, 17, 1},
    {0b0000000010000, 13, 10, 2}, {0b0000000010001, 13, 9, 2}, {0b0000000010010, 13, 5, 3},
    {0b0000000010011, 13, 3, 4}, {0b0000000010100, 13, 2, 5}, {0b0000000010101, 13, 1, 7},
    {0b0000000010110, 13, 1, 6}, {0b0000000010111, 13, 0, 15}, {0b0000000011000, 13, 0, 14},
    {0b0000000011001, 13, 0, 13}, {0b0000000011010, 13, 0, 12}, {0b0000000011011, 13, 26, 1},
    {0b0000000011100, 13, 25, 1}, {0b0000000011101, 13, 24, 1}, {0b0000000011110, 13, 23, 1},
    {0b0000000011111, 13, 22, 1},
    {0b00000000010000, 14, 0, 31}, {0b00000000010001, 14, 0, 30}, {0b00000000010010, 14, 0, 29},
    {0b00000000010011, 14, 0, 28}, {0b00000000010100, 14, 0, 27}, {0b00000000010101, 14, 0, 26},
    {0b00000000010110, 14, 0, 25}, {0b00000000010111, 14, 0, 24}, {0b00000000011000, 14, 0, 23},
    {0b00000000011001, 14, 0, 22}, {0b00000000011010, 14, 0, 21}, {0b00000000011011, 14, 0, 20},
    {0b00000000011100, 14, 0, 19}, {0b00000000011101, 14, 0, 18}, {0b00000000011110, 14, 0, 17},
    {0b00000000011111, 14, 0, 16},
    {0b000000000010000, 15, 0, 40}, {0b000000000010001, 15, 0, 39}, {0b000000000010010, 15, 0, 38},
    {0b000000000010011, 15, 0, 37}, {0b000000000010100, 15, 0, 36}, {0b000000000010101, 15, 0, 35},
    {0b000000000010110, 15, 0, 34}, {0b000000000010111, 15, 0, 33}, {0b000000000011000, 15, 0, 32},
    {0b000000000011001, 15, 1, 14}, {0b000000000011010, 15, 1, 13}, {0b000000000011011, 15, 1, 12},
    {0b000000000011100, 15, 1, 11}, {0b000000000011101, 15, 1, 10}, {0b000000000011110, 15, 1, 9},
    {0b000000000011111, 15, 1, 8},
    {0b0000000000010000, 16, 1, 18}, {0b0000000000010001, 16, 1, 17}, {0b0000000000010010, 16, 1, 16},
    {0b0000000000010011, 16, 1, 15}, {0b0000000000010100, 16, 6, 3}, {0b0000000000010101, 16, 16, 2},
    {0b0000000000010110, 16, 15, 2}, {0b0000000000010111, 16, 14, 2}, {0b0000000000011000, 16, 13, 2},
    {0b0000000000011001, 16, 12, 2}, {0b0000000000011010, 16, 11, 2}, {0b0000000000011011, 16, 31, 1},
    {0b0000000000011100, 16, 30, 1}, {0b0000000000011101, 16, 29, 1}, {0b0000000000011110, 16, 28, 1},
    {0b0000000000011111, 16, 27, 1},
};

constexpr CoefficientCode kEndOfBlock{0b10, 2, 0, 0};
constexpr CoefficientCode kEscape{0b000001, 6, 0, 0};

// What the next 16 bits of the stream start with. length == 0: no code starts like this.
struct Symbol {
    enum Kind : std::uint8_t { Coefficient, Escape, EndOfBlock };
    std::uint8_t length = 0;
    std::uint8_t run = 0;
    std::uint8_t level = 0;
    Kind kind = Coefficient;
};

class SymbolTable {
public:
    SymbolTable() {
        for (const CoefficientCode& c : kCoefficientCodes) fill(c, Symbol::Coefficient);
        fill(kEscape, Symbol::Escape);
        fill(kEndOfBlock, Symbol::EndOfBlock);
    }
    const Symbol& at(std::uint32_t next16) const { return symbols_[next16]; }

private:
    // The codes are prefix-free, so every 16-bit window that starts with the code belongs to it alone.
    void fill(const CoefficientCode& c, Symbol::Kind kind) {
        const unsigned free_bits = 16u - c.length;
        const std::uint32_t first = static_cast<std::uint32_t>(c.code) << free_bits;
        for (std::uint32_t i = 0; i < (1u << free_bits); ++i) symbols_[first + i] = {c.length, c.run, c.level, kind};
    }
    std::array<Symbol, 65536> symbols_{};
};

const SymbolTable& symbol_table() {
    static const SymbolTable table;
    return table;
}

// Coefficient k of the zigzag scan sits at kScan[k] of the block (row * 8 + column).
constexpr std::array<std::uint8_t, 64> make_scan() {
    std::array<std::uint8_t, 64> scan{};
    int row = 0, col = 0;
    for (std::size_t k = 0; k < 64; ++k) {
        scan[k] = static_cast<std::uint8_t>(row * 8 + col);
        if (((row + col) & 1) == 0) {  // even diagonals climb to the right
            if (col == 7) {
                ++row;
            } else if (row == 0) {
                ++col;
            } else {
                --row;
                ++col;
            }
        } else {
            if (row == 7) {
                ++col;
            } else if (col == 0) {
                ++row;
            } else {
                ++row;
                --col;
            }
        }
    }
    return scan;
}
constexpr std::array<std::uint8_t, 64> kScan = make_scan();

// The MPEG-1 default intra quantiser matrix, by block position. The movies carry no matrix of their own and the
// same one serves luma and chroma.
constexpr std::uint8_t kQuantiser[64] = {
    8,  16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34, 34, 38, 22, 22, 26, 27, 29, 34, 37, 40,
    22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58, 26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83};

// kBasis[n][u]: frequency u of the inverse cosine transform at sample n, a(u) * cos((2n + 1) * u * pi / 16)
// scaled by 2^15, a(0) = 1 / sqrt(2).
constexpr std::array<std::array<std::int32_t, 8>, 8> make_basis() {
    // round(32768 * cos(k * pi / 16)) for k = 0..8
    constexpr std::int32_t quarter[9] = {32768, 32138, 30274, 27246, 23170, 18205, 12540, 6393, 0};
    std::array<std::array<std::int32_t, 8>, 8> basis{};
    for (std::size_t n = 0; n < 8; ++n) {
        basis[n][0] = quarter[4];
        for (std::size_t u = 1; u < 8; ++u) {
            std::size_t angle = ((2 * n + 1) * u) % 32;  // in units of pi / 16
            if (angle > 16) angle = 32 - angle;
            basis[n][u] = angle > 8 ? -quarter[16 - angle] : quarter[angle];
        }
    }
    return basis;
}
constexpr std::array<std::array<std::int32_t, 8>, 8> kBasis = make_basis();

// The stream as bits: 16-bit little-endian words, most significant bit first. Past its end it reads as zeros.
class WordBits {
public:
    explicit WordBits(std::span<const std::uint8_t> bytes) : bytes_(bytes.data()), words_(bytes.size() / 2) {}

    std::uint32_t peek(int count) {
        refill();
        return static_cast<std::uint32_t>(window_ >> (64 - count));
    }
    void skip(int count) {
        refill();
        window_ <<= count;
        held_ -= count;
    }
    std::uint32_t take(int count) {
        const std::uint32_t bits = peek(count);
        skip(count);
        return bits;
    }
    std::int32_t take_signed(int count) {
        const std::int32_t bits = static_cast<std::int32_t>(take(count));
        const std::int32_t sign = 1 << (count - 1);
        return (bits ^ sign) - sign;
    }

private:
    void refill() {
        while (held_ <= 48) {
            std::uint64_t word = 0;
            if (next_ < words_) {
                word = static_cast<std::uint64_t>(bytes_[next_ * 2]) | static_cast<std::uint64_t>(bytes_[next_ * 2 + 1]) << 8;
            }
            ++next_;
            window_ |= word << (48 - held_);
            held_ += 16;
        }
    }

    const std::uint8_t* bytes_;
    std::size_t words_;
    std::size_t next_ = 0;
    std::uint64_t window_ = 0;  // the next bits, from bit 63 down
    int held_ = 0;
};

using Block = std::array<std::int16_t, 64>;

// Reads one block as dequantised coefficients. The block ends at its end code, at a coefficient past position 63
// or at bits that are no code.
void read_block(WordBits& bits, int scale, Block& block) {
    block.fill(0);
    block[0] = static_cast<std::int16_t>(2 * bits.take_signed(10));
    const SymbolTable& table = symbol_table();
    int position = 0;
    for (;;) {
        const Symbol& symbol = table.at(bits.peek(16));
        if (symbol.length == 0) return;
        bits.skip(symbol.length);
        if (symbol.kind == Symbol::EndOfBlock) return;

        const bool escape = symbol.kind == Symbol::Escape;
        int run = symbol.run;
        int level = symbol.level;
        if (escape) {
            run = static_cast<int>(bits.take(6));
            level = bits.take_signed(10);
        } else if (bits.take(1) != 0) {
            level = -level;
        }
        position += run + 1;
        if (position > 63) return;

        const std::size_t at = kScan[static_cast<std::size_t>(position)];
        // The magnitude is scaled and the sign put back after the shift; an escaped value is forced odd.
        std::int64_t value = (static_cast<std::int64_t>(level < 0 ? -level : level) * scale * kQuantiser[at]) >> 3;
        if (escape) value = (value - 1) | 1;
        if (level < 0) value = -value;
        if (value < -1024) value = -1024;
        if (value > 1023) value = 1023;
        block[at] = static_cast<std::int16_t>(value);
    }
}

// Coefficients to samples, in place: columns, then rows. A sample wraps at 9 bits like the console's decoder and
// is then held to -128..127.
void inverse_transform(Block& block) {
    std::int64_t columns[64];
    for (std::size_t x = 0; x < 8; ++x) {
        for (std::size_t y = 0; y < 8; ++y) {
            std::int64_t sum = 0;
            for (std::size_t u = 0; u < 8; ++u) sum += static_cast<std::int32_t>(block[u * 8 + x]) * kBasis[y][u];
            columns[y * 8 + x] = sum;
        }
    }
    for (std::size_t y = 0; y < 8; ++y) {
        for (std::size_t x = 0; x < 8; ++x) {
            std::int64_t sum = 0;
            for (std::size_t u = 0; u < 8; ++u) sum += columns[y * 8 + u] * kBasis[x][u];
            const std::int32_t rounded = static_cast<std::int32_t>((sum >> 32) + ((sum >> 31) & 1));
            std::int32_t sample = ((rounded & 0x1FF) ^ 0x100) - 0x100;
            if (sample < -128) sample = -128;
            if (sample > 127) sample = 127;
            block[y * 8 + x] = static_cast<std::int16_t>(sample);
        }
    }
}

std::uint8_t to_byte(int centred) {
    if (centred < -128) centred = -128;
    if (centred > 127) centred = 127;
    return static_cast<std::uint8_t>(centred + 128);
}

// One macroblock to RGB at `dst` (its top left pixel; rows are `stride` bytes apart). Each chroma sample covers
// 2x2 pixels.
void write_macroblock(const Block& cr, const Block& cb, const Block luma[4], std::uint8_t* dst, std::size_t stride) {
    for (std::size_t cy = 0; cy < 8; ++cy) {
        for (std::size_t cx = 0; cx < 8; ++cx) {
            const float red_chroma = static_cast<float>(cr[cy * 8 + cx]);
            const float blue_chroma = static_cast<float>(cb[cy * 8 + cx]);
            const int red = static_cast<int>(1.402f * red_chroma);
            const int green = static_cast<int>(-0.3437f * blue_chroma + -0.7143f * red_chroma);
            const int blue = static_cast<int>(1.772f * blue_chroma);
            for (std::size_t py = cy * 2; py < cy * 2 + 2; ++py) {
                for (std::size_t px = cx * 2; px < cx * 2 + 2; ++px) {
                    const Block& y_block = luma[(py / 8) * 2 + px / 8];
                    const int y = y_block[(py % 8) * 8 + px % 8];
                    std::uint8_t* pixel = dst + py * stride + px * 3;
                    pixel[0] = to_byte(y + red);
                    pixel[1] = to_byte(y + green);
                    pixel[2] = to_byte(y + blue);
                }
            }
        }
    }
}

}  // namespace

bool MdecDecoder::decode(std::span<const std::uint8_t> bitstream, int width, int height, std::uint8_t* rgb) const {
    if (bitstream.size() < 8 || rgb == nullptr) return false;
    WordBits bits(bitstream);
    bits.skip(32);  // the preamble: the count of run-length words and the 0x3800 mark
    const int scale = static_cast<int>(bits.take(16));
    const int version = static_cast<int>(bits.take(16));
    if (scale == 0 || version > 2) return false;

    const int columns = width / 16, rows = height / 16;
    const std::size_t stride = static_cast<std::size_t>(width) * 3;
    Block cr, cb, luma[4];
    for (int mx = 0; mx < columns; ++mx) {
        for (int my = 0; my < rows; ++my) {
            read_block(bits, scale, cr);
            inverse_transform(cr);
            read_block(bits, scale, cb);
            inverse_transform(cb);
            for (Block& y : luma) {
                read_block(bits, scale, y);
                inverse_transform(y);
            }
            write_macroblock(cr, cb, luma,
                             rgb + static_cast<std::size_t>(my) * 16 * stride + static_cast<std::size_t>(mx) * 48, stride);
        }
    }
    return true;
}

}  // namespace sh::fmv
