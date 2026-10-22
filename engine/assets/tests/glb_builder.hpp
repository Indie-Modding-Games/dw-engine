#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets::testing {

// Builds a small binary glTF by hand for the model tests: the caller writes the JSON of nodes,
// meshes, materials and so on, and adds the binary data through accessors, which this class lays
// out in one buffer.
class GlbBuilder {
public:
  // Component types and accessor types of glTF.
  static constexpr int Float = 5126;
  static constexpr int UnsignedShort = 5123;
  static constexpr int UnsignedByte = 5121;

  // Adds an accessor over these values and returns its index.
  template <typename T>
  int addAccessor(std::span<const T> values, int componentType, std::string_view type,
                  std::size_t count, std::string_view extra = "") {
    align();
    const std::size_t offset = bin_.size();
    const std::size_t bytes = values.size_bytes();
    bin_.resize(offset + bytes);
    std::memcpy(bin_.data() + offset, values.data(), bytes);
    const int view = addView(offset, bytes);
    accessors_.push_back(
        std::format(R"({{"bufferView":{},"componentType":{},"type":"{}","count":{}{}}})", view,
                    componentType, type, count, extra));
    return static_cast<int>(accessors_.size()) - 1;
  }

  int addFloats(const std::vector<float>& values, std::string_view type, std::size_t components,
                std::string_view extra = "") {
    return addAccessor(std::span<const float>{values}, Float, type, values.size() / components,
                       extra);
  }

  // Adds an accessor with no buffer view, which glTF reads as `count` zeros, and returns its index.
  int addViewlessAccessor(int componentType, std::string_view type, std::size_t count) {
    accessors_.push_back(std::format(R"({{"componentType":{},"type":"{}","count":{}}})",
                                     componentType, type, count));
    return static_cast<int>(accessors_.size()) - 1;
  }

  // Adds raw bytes, such as a PNG, as a buffer view and returns its index.
  int addBytes(std::span<const unsigned char> bytes) {
    align();
    const std::size_t offset = bin_.size();
    bin_.insert(bin_.end(), bytes.begin(), bytes.end());
    return addView(offset, bytes.size());
  }

  // The whole file: `json` is the glTF document without "asset", "buffers", "bufferViews" and
  // "accessors", which this class adds; it must start with '{' and contain at least one property.
  [[nodiscard]] std::vector<std::byte> build(std::string_view json) {
    align();
    std::string views;
    for (const std::string& view : views_) {
      views += (views.empty() ? "" : ",") + view;
    }
    std::string accessors;
    for (const std::string& accessor : accessors_) {
      accessors += (accessors.empty() ? "" : ",") + accessor;
    }
    std::string document =
        std::format(R"({{"asset":{{"version":"2.0"}},"buffers":[{{"byteLength":{}}}],)"
                    R"("bufferViews":[{}],"accessors":[{}],{})",
                    bin_.size(), views, accessors, json.substr(1));
    while (document.size() % 4 != 0) {
      document += ' ';
    }
    std::vector<std::byte> out;
    const auto put32 = [&out](std::uint32_t value) {
      for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> static_cast<unsigned>(shift)) & 0xFFU));
      }
    };
    constexpr std::uint32_t Magic = 0x46546C67;     // "glTF"
    constexpr std::uint32_t JsonChunk = 0x4E4F534A; // "JSON"
    constexpr std::uint32_t BinChunk = 0x004E4942;  // "BIN\0"
    constexpr std::uint32_t HeaderSize = 12;
    constexpr std::uint32_t ChunkHeaderSize = 8;
    const auto total = static_cast<std::uint32_t>(HeaderSize + ChunkHeaderSize + document.size() +
                                                  ChunkHeaderSize + bin_.size());
    put32(Magic);
    put32(2);
    put32(total);
    put32(static_cast<std::uint32_t>(document.size()));
    put32(JsonChunk);
    for (const char c : document) {
      out.push_back(static_cast<std::byte>(c));
    }
    put32(static_cast<std::uint32_t>(bin_.size()));
    put32(BinChunk);
    for (const unsigned char c : bin_) {
      out.push_back(static_cast<std::byte>(c));
    }
    return out;
  }

private:
  void align() {
    while (bin_.size() % 4 != 0) {
      bin_.push_back(0);
    }
  }

  int addView(std::size_t offset, std::size_t length) {
    views_.push_back(
        std::format(R"({{"buffer":0,"byteOffset":{},"byteLength":{}}})", offset, length));
    return static_cast<int>(views_.size()) - 1;
  }

  std::vector<unsigned char> bin_;
  std::vector<std::string> views_;
  std::vector<std::string> accessors_;
};

} // namespace dwe::assets::testing
