#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

namespace script_fixtures {

// Builds a script: the section list, then code appended byte by byte.
class ScriptBuilder {
public:
    explicit ScriptBuilder(std::initializer_list<std::pair<std::uint16_t, std::uint16_t>> sections) {
        u16(static_cast<std::uint16_t>(2 + (4 * sections.size()) + 2));
        for (const auto& [id, offset] : sections) {
            u16(id);
            u16(offset);
        }
        u16(0xFFFF);
    }

    ScriptBuilder& u8(std::initializer_list<unsigned> values) {
        for (const unsigned value : values) {
            bytes_.push_back(static_cast<std::byte>(value));
        }
        return *this;
    }

    ScriptBuilder& u16(unsigned value) { return u8({value & 0xFFU, value >> 8U & 0xFFU}); }

    ScriptBuilder& u32(std::uint32_t value) { return u16(value & 0xFFFFU).u16(value >> 16U); }

    [[nodiscard]] std::size_t size() const { return bytes_.size(); }
    [[nodiscard]] const std::vector<std::byte>& bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

} // namespace script_fixtures
