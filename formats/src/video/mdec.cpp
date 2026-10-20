#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/video/mdec.hpp>

#include <algorithm>
#include <format>
#include <string_view>

namespace dwe::formats::video {

namespace {

constexpr std::uint16_t HeaderMarker = 0x3800;
constexpr std::uint16_t MaxQuantizationScale = 63;
constexpr std::size_t BlocksPerMacroblock = 6; // Cr, Cb, Y0, Y1, Y2, Y3
constexpr std::size_t CoefficientCount = 64;
constexpr int MinDc = -512;
constexpr int MaxDc = 511;

// Reads a bitstream as libpress does: 16-bit little-endian words, most significant bit first. The words are read
// once up front: every code peeks at two of them.
class BitReader {
public:
    explicit BitReader(std::span<const std::byte> bytes) : bits_(bytes.size() * 8) {
        const ByteReader reader{bytes};
        words_.reserve((bytes.size() + 1) / 2);
        for (std::size_t offset = 0; offset < bytes.size(); offset += 2) {
            words_.push_back(reader.u16(offset).value_or(reader.u8(offset).value_or(0)));
        }
    }

    // Returns the next count bits (1 to 17) without taking them. Bits past the end read as 0; overrun() tells.
    [[nodiscard]] std::uint32_t peek(unsigned count) const {
        const std::size_t word = position_ / 16;
        const std::uint32_t window = (wordAt(word) << 16U) | wordAt(word + 1);
        return (window << (position_ % 16)) >> (32U - count);
    }

    std::uint32_t read(unsigned count) {
        const std::uint32_t value = peek(count);
        position_ += count;
        return value;
    }

    void skip(unsigned count) { position_ += count; }

    [[nodiscard]] bool overrun() const { return position_ > bits_; }

private:
    [[nodiscard]] std::uint32_t wordAt(std::size_t word) const { return word < words_.size() ? words_[word] : 0; }

    std::vector<std::uint16_t> words_;
    std::size_t bits_;
    std::size_t position_ = 0;
};

// What a code means. DC size codes are all Value.
enum class CodeKind : std::uint8_t {
    Value,      // AC: a run and a level, followed by a sign bit
    EndOfBlock, // AC
    Escape,     // AC: followed by a 6-bit run and a 10-bit level
};

// A variable-length code of a lookup table, decoded by peeking a fixed number of bits.
struct VlcEntry {
    std::uint8_t length = 0; // 0: no code starts with these bits
    CodeKind kind = CodeKind::Value;
    std::uint8_t run = 0;
    std::uint8_t value = 0; // AC level or DC size
};

// The codes of a table as written in the standard: bits, run and level (AC) or size (DC).
struct VlcCode {
    std::string_view bits;
    std::uint8_t run;
    std::uint8_t value;
};

// The DCT coefficient codes of MPEG-1 (ISO/IEC 11172-2 table B.14, without the sign bit that follows each one), the
// table libpress builds in DecDCTvlcBuild.
constexpr auto AcCodes = std::to_array<VlcCode>({
    {.bits = "11", .run = 0, .value = 1},
    {.bits = "011", .run = 1, .value = 1},
    {.bits = "0100", .run = 0, .value = 2},
    {.bits = "0101", .run = 2, .value = 1},
    {.bits = "00101", .run = 0, .value = 3},
    {.bits = "00110", .run = 4, .value = 1},
    {.bits = "00111", .run = 3, .value = 1},
    {.bits = "000100", .run = 7, .value = 1},
    {.bits = "000101", .run = 6, .value = 1},
    {.bits = "000110", .run = 1, .value = 2},
    {.bits = "000111", .run = 5, .value = 1},
    {.bits = "0000100", .run = 2, .value = 2},
    {.bits = "0000101", .run = 9, .value = 1},
    {.bits = "0000110", .run = 0, .value = 4},
    {.bits = "0000111", .run = 8, .value = 1},
    {.bits = "00100000", .run = 13, .value = 1},
    {.bits = "00100001", .run = 0, .value = 6},
    {.bits = "00100010", .run = 12, .value = 1},
    {.bits = "00100011", .run = 11, .value = 1},
    {.bits = "00100100", .run = 3, .value = 2},
    {.bits = "00100101", .run = 1, .value = 3},
    {.bits = "00100110", .run = 0, .value = 5},
    {.bits = "00100111", .run = 10, .value = 1},
    {.bits = "0000001000", .run = 16, .value = 1},
    {.bits = "0000001001", .run = 5, .value = 2},
    {.bits = "0000001010", .run = 0, .value = 7},
    {.bits = "0000001011", .run = 2, .value = 3},
    {.bits = "0000001100", .run = 1, .value = 4},
    {.bits = "0000001101", .run = 15, .value = 1},
    {.bits = "0000001110", .run = 14, .value = 1},
    {.bits = "0000001111", .run = 4, .value = 2},
    {.bits = "000000010000", .run = 0, .value = 11},
    {.bits = "000000010001", .run = 8, .value = 2},
    {.bits = "000000010010", .run = 4, .value = 3},
    {.bits = "000000010011", .run = 0, .value = 10},
    {.bits = "000000010100", .run = 2, .value = 4},
    {.bits = "000000010101", .run = 7, .value = 2},
    {.bits = "000000010110", .run = 21, .value = 1},
    {.bits = "000000010111", .run = 20, .value = 1},
    {.bits = "000000011000", .run = 0, .value = 9},
    {.bits = "000000011001", .run = 19, .value = 1},
    {.bits = "000000011010", .run = 18, .value = 1},
    {.bits = "000000011011", .run = 1, .value = 5},
    {.bits = "000000011100", .run = 3, .value = 3},
    {.bits = "000000011101", .run = 0, .value = 8},
    {.bits = "000000011110", .run = 6, .value = 2},
    {.bits = "000000011111", .run = 17, .value = 1},
    {.bits = "0000000010000", .run = 10, .value = 2},
    {.bits = "0000000010001", .run = 9, .value = 2},
    {.bits = "0000000010010", .run = 5, .value = 3},
    {.bits = "0000000010011", .run = 3, .value = 4},
    {.bits = "0000000010100", .run = 2, .value = 5},
    {.bits = "0000000010101", .run = 1, .value = 7},
    {.bits = "0000000010110", .run = 1, .value = 6},
    {.bits = "0000000010111", .run = 0, .value = 15},
    {.bits = "0000000011000", .run = 0, .value = 14},
    {.bits = "0000000011001", .run = 0, .value = 13},
    {.bits = "0000000011010", .run = 0, .value = 12},
    {.bits = "0000000011011", .run = 26, .value = 1},
    {.bits = "0000000011100", .run = 25, .value = 1},
    {.bits = "0000000011101", .run = 24, .value = 1},
    {.bits = "0000000011110", .run = 23, .value = 1},
    {.bits = "0000000011111", .run = 22, .value = 1},
    {.bits = "00000000010000", .run = 0, .value = 31},
    {.bits = "00000000010001", .run = 0, .value = 30},
    {.bits = "00000000010010", .run = 0, .value = 29},
    {.bits = "00000000010011", .run = 0, .value = 28},
    {.bits = "00000000010100", .run = 0, .value = 27},
    {.bits = "00000000010101", .run = 0, .value = 26},
    {.bits = "00000000010110", .run = 0, .value = 25},
    {.bits = "00000000010111", .run = 0, .value = 24},
    {.bits = "00000000011000", .run = 0, .value = 23},
    {.bits = "00000000011001", .run = 0, .value = 22},
    {.bits = "00000000011010", .run = 0, .value = 21},
    {.bits = "00000000011011", .run = 0, .value = 20},
    {.bits = "00000000011100", .run = 0, .value = 19},
    {.bits = "00000000011101", .run = 0, .value = 18},
    {.bits = "00000000011110", .run = 0, .value = 17},
    {.bits = "00000000011111", .run = 0, .value = 16},
    {.bits = "000000000010000", .run = 0, .value = 40},
    {.bits = "000000000010001", .run = 0, .value = 39},
    {.bits = "000000000010010", .run = 0, .value = 38},
    {.bits = "000000000010011", .run = 0, .value = 37},
    {.bits = "000000000010100", .run = 0, .value = 36},
    {.bits = "000000000010101", .run = 0, .value = 35},
    {.bits = "000000000010110", .run = 0, .value = 34},
    {.bits = "000000000010111", .run = 0, .value = 33},
    {.bits = "000000000011000", .run = 0, .value = 32},
    {.bits = "000000000011001", .run = 1, .value = 14},
    {.bits = "000000000011010", .run = 1, .value = 13},
    {.bits = "000000000011011", .run = 1, .value = 12},
    {.bits = "000000000011100", .run = 1, .value = 11},
    {.bits = "000000000011101", .run = 1, .value = 10},
    {.bits = "000000000011110", .run = 1, .value = 9},
    {.bits = "000000000011111", .run = 1, .value = 8},
    {.bits = "0000000000010000", .run = 1, .value = 18},
    {.bits = "0000000000010001", .run = 1, .value = 17},
    {.bits = "0000000000010010", .run = 1, .value = 16},
    {.bits = "0000000000010011", .run = 1, .value = 15},
    {.bits = "0000000000010100", .run = 6, .value = 3},
    {.bits = "0000000000010101", .run = 16, .value = 2},
    {.bits = "0000000000010110", .run = 15, .value = 2},
    {.bits = "0000000000010111", .run = 14, .value = 2},
    {.bits = "0000000000011000", .run = 13, .value = 2},
    {.bits = "0000000000011001", .run = 12, .value = 2},
    {.bits = "0000000000011010", .run = 11, .value = 2},
    {.bits = "0000000000011011", .run = 31, .value = 1},
    {.bits = "0000000000011100", .run = 30, .value = 1},
    {.bits = "0000000000011101", .run = 29, .value = 1},
    {.bits = "0000000000011110", .run = 28, .value = 1},
    {.bits = "0000000000011111", .run = 27, .value = 1},
});
constexpr std::string_view EndOfBlockBits = "10";
constexpr std::string_view EscapeBits = "000001";
constexpr unsigned AcPeekBits = 16;

// The DC size codes of version 3 (ISO/IEC 11172-2 tables B.12 and B.13): the size is the number of bits of the
// difference that follows.
constexpr auto LumaDcCodes = std::to_array<VlcCode>({
    {.bits = "100", .run = 0, .value = 0},
    {.bits = "00", .run = 0, .value = 1},
    {.bits = "01", .run = 0, .value = 2},
    {.bits = "101", .run = 0, .value = 3},
    {.bits = "110", .run = 0, .value = 4},
    {.bits = "1110", .run = 0, .value = 5},
    {.bits = "11110", .run = 0, .value = 6},
    {.bits = "111110", .run = 0, .value = 7},
    {.bits = "1111110", .run = 0, .value = 8},
});
constexpr auto ChromaDcCodes = std::to_array<VlcCode>({
    {.bits = "00", .run = 0, .value = 0},
    {.bits = "01", .run = 0, .value = 1},
    {.bits = "10", .run = 0, .value = 2},
    {.bits = "110", .run = 0, .value = 3},
    {.bits = "1110", .run = 0, .value = 4},
    {.bits = "11110", .run = 0, .value = 5},
    {.bits = "111110", .run = 0, .value = 6},
    {.bits = "1111110", .run = 0, .value = 7},
    {.bits = "11111110", .run = 0, .value = 8},
});
constexpr unsigned DcPeekBits = 8;

// Fills every entry of a table indexed by the next peekBits bits that starts with the code.
void addCode(std::vector<VlcEntry>& table, unsigned peekBits, std::string_view bits, VlcEntry entry) {
    std::uint32_t prefix = 0;
    for (const char bit : bits) {
        prefix = (prefix << 1U) | (bit == '1' ? 1U : 0U);
    }
    const unsigned free = peekBits - static_cast<unsigned>(bits.size());
    entry.length = static_cast<std::uint8_t>(bits.size());
    for (std::uint32_t rest = 0; rest < (1U << free); ++rest) {
        table[(prefix << free) | rest] = entry;
    }
}

std::vector<VlcEntry> buildTable(unsigned peekBits, std::span<const VlcCode> codes) {
    std::vector<VlcEntry> table(std::size_t{1} << peekBits);
    for (const auto& code : codes) {
        addCode(table, peekBits, code.bits,
                VlcEntry{.length = 0, .kind = CodeKind::Value, .run = code.run, .value = code.value});
    }
    return table;
}

const std::vector<VlcEntry>& acTable() {
    static const std::vector<VlcEntry> table = [] {
        auto built = buildTable(AcPeekBits, AcCodes);
        addCode(built, AcPeekBits, EndOfBlockBits,
                VlcEntry{.length = 0, .kind = CodeKind::EndOfBlock, .run = 0, .value = 0});
        addCode(built, AcPeekBits, EscapeBits, VlcEntry{.length = 0, .kind = CodeKind::Escape, .run = 0, .value = 0});
        return built;
    }();
    return table;
}

const std::vector<VlcEntry>& lumaDcTable() {
    static const std::vector<VlcEntry> table = buildTable(DcPeekBits, LumaDcCodes);
    return table;
}

const std::vector<VlcEntry>& chromaDcTable() {
    static const std::vector<VlcEntry> table = buildTable(DcPeekBits, ChromaDcCodes);
    return table;
}

std::uint16_t mdecCode(unsigned top6, int bottom10) {
    return static_cast<std::uint16_t>((top6 << 10U) | (static_cast<unsigned>(bottom10) & 0x3FFU));
}

int signed10(unsigned value) {
    return static_cast<int>(value & 0x3FFU) - static_cast<int>((value & 0x200U) << 1U);
}

// Reads a version 3 DC difference: a size code, then that many bits; a leading 0 bit means a negative value. The
// difference counts in steps of 4.
core::Result<int> readDcDifference(BitReader& bits, const std::vector<VlcEntry>& table) {
    const VlcEntry& entry = table[bits.peek(DcPeekBits)];
    if (entry.length == 0) {
        return core::fail(std::format("unknown DC size code {:08b}", bits.peek(DcPeekBits)));
    }
    bits.skip(entry.length);
    const unsigned size = entry.value;
    if (size == 0) {
        return 0;
    }
    const auto value = static_cast<int>(bits.read(size));
    const bool negative = (value >> (size - 1)) == 0;
    return (negative ? value - ((1 << size) - 1) : value) * 4;
}

// The quantisation matrix libpress gives the MDEC (DecDCTReset): the default intra matrix of MPEG-1 with a DC
// weight of 2, in natural order.
constexpr std::array<std::uint8_t, CoefficientCount> QuantizationMatrix = {
    2,  16, 19, 22, 26, 27, 29, 34, //
    16, 16, 22, 24, 27, 29, 34, 37, //
    19, 22, 26, 27, 29, 34, 34, 38, //
    22, 22, 26, 27, 29, 34, 37, 40, //
    22, 26, 27, 29, 32, 35, 40, 48, //
    26, 27, 29, 32, 35, 40, 48, 58, //
    26, 27, 29, 34, 38, 46, 56, 69, //
    27, 29, 35, 38, 46, 56, 69, 83, //
};

// The natural index of each coefficient in transmission order: the diagonals of the block, alternating direction.
constexpr std::array<std::uint8_t, CoefficientCount> makeZigZag() {
    std::array<std::uint8_t, CoefficientCount> order{};
    std::size_t next = 0;
    for (int diagonal = 0; diagonal < 15; ++diagonal) {
        for (int step = 0; step <= diagonal; ++step) {
            const int row = diagonal % 2 == 0 ? diagonal - step : step;
            const int column = diagonal - row;
            if (row < 8 && column < 8) {
                order[next++] = static_cast<std::uint8_t>((row * 8) + column);
            }
        }
    }
    return order;
}
constexpr std::array<std::uint8_t, CoefficientCount> ZigZag = makeZigZag();

// The IDCT matrix as the MDEC uses it, indexed [sample * 8 + frequency]: libpress's table (IdctTable, floor and not
// rounding, as in the library) transposed, and shifted right by 3 as the MDEC keeps it.
constexpr std::array<std::int32_t, CoefficientCount> makeIdctMatrix() {
    std::array<std::int32_t, CoefficientCount> matrix{};
    for (std::size_t x = 0; x < 8; ++x) {
        for (std::size_t u = 0; u < 8; ++u) {
            matrix[(x * 8) + u] = IdctTable[(u * 8) + x] >> 3;
        }
    }
    return matrix;
}
constexpr std::array<std::int32_t, CoefficientCount> IdctMatrix = makeIdctMatrix();

// The MDEC keeps 9 bits of a result and saturates them to a signed byte.
int wrapToByte(int value) {
    const auto nineBits = static_cast<int>(static_cast<unsigned>(value) & 0x1FFU);
    return std::clamp(nineBits >= 0x100 ? nineBits - 0x200 : nineBits, -128, 127);
}

// Stores a dequantised level as the MDEC does: times 16 with a bias of 8 towards zero, saturated to 15 bits. A zero
// quantisation (libpress never gives one) stores the level times 32.
std::int16_t storeCoefficient(int level, int dequantized, int quantization) {
    int value = level * 32;
    if (quantization != 0) {
        int bias = 0;
        if (level != 0) {
            bias = level < 0 ? 8 : -8;
        }
        value = (dequantized * 16) + bias;
    }
    return static_cast<std::int16_t>(std::clamp(value, -0x4000, 0x3FFF));
}

// Reads one block's codes from the MDEC's input and fills its coefficients in natural order. Padding codes before a
// block are skipped; a run that passes the last coefficient ends the block, as on the hardware.
core::Result<void> readBlock(std::span<const std::uint16_t> codes, std::size_t& position,
                             std::array<std::int16_t, CoefficientCount>& coefficients) {
    coefficients.fill(0);
    while (position < codes.size() && codes[position] == MdecEndOfBlock) {
        ++position;
    }
    if (position >= codes.size()) {
        return core::fail("the MDEC codes end before the last block");
    }
    const unsigned first = codes[position++];
    const auto scale = static_cast<int>(first >> 10U);
    // The DC takes the matrix's first weight but not the scale.
    const int dc = signed10(first);
    coefficients[ZigZag[0]] = storeCoefficient(dc, dc * QuantizationMatrix[0], QuantizationMatrix[0]);
    std::size_t index = 1;
    while (index < CoefficientCount) {
        if (position >= codes.size()) {
            return core::fail("the MDEC codes end inside a block");
        }
        const unsigned code = codes[position++];
        if (code == MdecEndOfBlock) {
            break;
        }
        index += code >> 10U;
        if (index < CoefficientCount) {
            const std::uint8_t natural = ZigZag[index];
            const int quantization = scale * QuantizationMatrix[natural];
            const int level = signed10(code);
            coefficients[natural] = storeCoefficient(level, (level * quantization) >> 3, quantization);
            ++index;
        }
    }
    return {};
}

void placeBlock(const std::array<std::int8_t, CoefficientCount>& samples, std::vector<std::uint8_t>& plane,
                std::size_t planeWidth, std::size_t left, std::size_t top) {
    for (std::size_t row = 0; row < 8; ++row) {
        for (std::size_t column = 0; column < 8; ++column) {
            plane[((top + row) * planeWidth) + left + column] =
                static_cast<std::uint8_t>(samples[(row * 8) + column] + 128);
        }
    }
}

int saturate9(int value) {
    return wrapToByte(value) + 128;
}

} // namespace

core::Result<BitstreamHeader> readBitstreamHeader(std::span<const std::byte> bitstream) {
    const ByteReader reader{bitstream};
    const auto count = reader.u16(0);
    const auto marker = reader.u16(2);
    const auto scale = reader.u16(4);
    const auto version = reader.u16(6);
    if (!count || !marker || !scale || !version) {
        return core::fail(std::format("a frame of {} bytes is shorter than its header", bitstream.size()));
    }
    if (*marker != HeaderMarker) {
        return core::fail(std::format("the frame header has {:#06x} where 0x3800 goes", *marker));
    }
    if (*scale == 0 || *scale > MaxQuantizationScale) {
        return core::fail(std::format("the frame's quantisation scale {} is outside 1..63", *scale));
    }
    if (*version != 2 && *version != 3) {
        return core::fail(std::format("bitstream version {} is not supported (only 2 and 3)", *version));
    }
    return BitstreamHeader{.halfCodeCount = *count, .quantizationScale = *scale, .version = *version};
}

core::Result<std::vector<std::uint16_t>> decodeBitstream(std::span<const std::byte> bitstream,
                                                         std::size_t macroblockCount) {
    const auto header = readBitstreamHeader(bitstream);
    if (!header) {
        return std::unexpected{header.error()};
    }
    const auto& ac = acTable();
    BitReader bits{bitstream.subspan(BitstreamHeaderSize)};
    std::vector<std::uint16_t> codes;
    codes.reserve(std::size_t{header->halfCodeCount} * 2);
    std::array<int, 3> previousDc{}; // Y, Cb, Cr
    for (std::size_t macroblock = 0; macroblock < macroblockCount; ++macroblock) {
        for (std::size_t block = 0; block < BlocksPerMacroblock; ++block) {
            int dc = 0;
            if (header->version == 2) {
                dc = signed10(bits.read(10));
            } else {
                const bool chroma = block < 2;
                const auto difference = readDcDifference(bits, chroma ? chromaDcTable() : lumaDcTable());
                if (!difference) {
                    return core::fail(
                        std::format("macroblock {} block {}: {}", macroblock, block, difference.error().message));
                }
                int& previous = previousDc[chroma ? 2 - block : 0];
                previous += *difference;
                if (previous < MinDc || previous > MaxDc) {
                    return core::fail(
                        std::format("macroblock {} block {}: the DC {} leaves -512..511", macroblock, block, previous));
                }
                dc = previous;
            }
            codes.push_back(mdecCode(header->quantizationScale, dc));
            std::size_t index = 0;
            for (;;) {
                const VlcEntry& entry = ac[bits.peek(AcPeekBits)];
                if (entry.length == 0) {
                    return core::fail(std::format("macroblock {} block {}: unknown AC code {:016b}", macroblock, block,
                                                  bits.peek(AcPeekBits)));
                }
                bits.skip(entry.length);
                if (entry.kind == CodeKind::EndOfBlock) {
                    codes.push_back(MdecEndOfBlock);
                    break;
                }
                std::uint16_t code = 0;
                if (entry.kind == CodeKind::Escape) {
                    code = static_cast<std::uint16_t>(bits.read(16));
                } else {
                    const bool negative = bits.read(1) != 0;
                    code = mdecCode(entry.run, negative ? -int{entry.value} : int{entry.value});
                }
                index += (code >> 10U) + 1U;
                if (index >= CoefficientCount) {
                    return core::fail(
                        std::format("macroblock {} block {}: the block passes 64 coefficients", macroblock, block));
                }
                codes.push_back(code);
            }
        }
        if (bits.overrun()) {
            return core::fail(std::format("the bitstream ends inside macroblock {}", macroblock));
        }
    }
    const std::size_t expected = (((codes.size() + 1) / 2) + 31) & ~std::size_t{31};
    if (expected != header->halfCodeCount) {
        return core::fail(std::format("the frame gives {} MDEC codes, but its header counts {} words", codes.size(),
                                      header->halfCodeCount));
    }
    return codes;
}

core::Result<std::size_t> countMacroblocks(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || width % 16 != 0 || height % 16 != 0) {
        return core::fail(std::format("a frame of {}x{} is not made of 16x16 macroblocks", width, height));
    }
    return std::size_t{width / 16} * (height / 16);
}

core::Result<YCbCrFrame> decodeMacroblocks(std::span<const std::uint16_t> codes, std::uint32_t width,
                                           std::uint32_t height) {
    if (const auto count = countMacroblocks(width, height); !count) {
        return std::unexpected{count.error()};
    }
    YCbCrFrame frame{.width = width,
                     .height = height,
                     .y = std::vector<std::uint8_t>(std::size_t{width} * height),
                     .cb = std::vector<std::uint8_t>(std::size_t{width} * height / 4),
                     .cr = std::vector<std::uint8_t>(std::size_t{width} * height / 4)};
    std::array<std::int16_t, CoefficientCount> coefficients{};
    std::size_t position = 0;
    for (std::size_t column = 0; column < width / 16; ++column) {
        for (std::size_t row = 0; row < height / 16; ++row) {
            for (std::size_t block = 0; block < BlocksPerMacroblock; ++block) {
                if (auto read = readBlock(codes, position, coefficients); !read) {
                    return std::unexpected{read.error()};
                }
                const auto samples = inverseDct(coefficients);
                if (block < 2) {
                    placeBlock(samples, block == 0 ? frame.cr : frame.cb, width / 2, column * 8, row * 8);
                } else {
                    const std::size_t luma = block - 2;
                    placeBlock(samples, frame.y, width, (column * 16) + ((luma % 2) * 8),
                               (row * 16) + ((luma / 2) * 8));
                }
            }
        }
    }
    return frame;
}

core::Result<YCbCrFrame> decodeFrame(std::span<const std::byte> bitstream, std::uint32_t width, std::uint32_t height) {
    const auto count = countMacroblocks(width, height);
    if (!count) {
        return std::unexpected{count.error()};
    }
    const auto codes = decodeBitstream(bitstream, *count);
    if (!codes) {
        return std::unexpected{codes.error()};
    }
    return decodeMacroblocks(*codes, width, height);
}

std::array<std::int8_t, 64> inverseDct(const std::array<std::int16_t, 64>& coefficients) {
    // First pass: each column of frequencies becomes a column of samples. A column without coefficients stays 0 and
    // adds nothing to the second pass, so only the others are kept.
    std::array<std::int32_t, CoefficientCount> columns{};
    std::array<std::size_t, 8> used{};
    std::size_t usedCount = 0;
    for (std::size_t u = 0; u < 8; ++u) {
        bool empty = true;
        for (std::size_t v = 0; v < 8 && empty; ++v) {
            empty = coefficients[(v * 8) + u] == 0;
        }
        if (empty) {
            continue;
        }
        used[usedCount++] = u;
        for (std::size_t y = 0; y < 8; ++y) {
            std::int32_t sum = 0;
            for (std::size_t v = 0; v < 8; ++v) {
                sum += coefficients[(v * 8) + u] * IdctMatrix[(y * 8) + v];
            }
            // The pass's result is kept in 16 bits, as in the 16-bit buffer between the two passes of Mednafen's
            // MDEC, which this IDCT follows. It never actually wraps: the matrix's values are below 4096, so eight
            // products of 16-bit coefficients stay within 2^30 and the rounded result within +-32144.
            columns[(y * 8) + u] = static_cast<std::int16_t>((sum + 0x4000) >> 15);
        }
    }
    std::array<std::int8_t, CoefficientCount> samples{};
    for (std::size_t y = 0; y < 8; ++y) {
        for (std::size_t x = 0; x < 8; ++x) {
            std::int32_t sum = 0;
            for (std::size_t i = 0; i < usedCount; ++i) {
                const std::size_t u = used[i];
                sum += columns[(y * 8) + u] * IdctMatrix[(x * 8) + u];
            }
            samples[(y * 8) + x] = static_cast<std::int8_t>(wrapToByte((sum + 0x4000) >> 15));
        }
    }
    return samples;
}

Rgb8 toRgb(std::uint8_t y, std::uint8_t cb, std::uint8_t cr) {
    const int luma = int{y} - 128;
    const int blue = int{cb} - 128;
    const int red = int{cr} - 128;
    // The green term drops low bits of each product, as the hardware does.
    const int green = (static_cast<int>(static_cast<unsigned>(-88 * blue) & ~0x1FU) +
                       static_cast<int>(static_cast<unsigned>(-183 * red) & ~0x07U) + 0x80) >>
                      8;
    return Rgb8{.r = static_cast<std::uint8_t>(saturate9(luma + (((359 * red) + 0x80) >> 8))),
                .g = static_cast<std::uint8_t>(saturate9(luma + green)),
                .b = static_cast<std::uint8_t>(saturate9(luma + (((454 * blue) + 0x80) >> 8)))};
}

} // namespace dwe::formats::video
