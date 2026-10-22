#pragma once

#include <dwe/core/error.hpp>
#include <dwe/rhi/bindless.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/rhi/types.hpp>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace dwe::rhi {

// The type of one vertex attribute, as the shader reads it.
enum class VertexFormat : std::uint8_t {
  Float1,
  Float2,
  Float3,
  Float4,
  // Four bytes read as floats from 0 to 1 (colors).
  Unorm8x4,
  // Four bytes read as unsigned integers (joint indices).
  Uint8x4,
  // Four 16-bit values read as unsigned integers (joint indices).
  Uint16x4,
  // Four 16-bit values read as floats from 0 to 1 (joint weights).
  Unorm16x4,
  // One or two 32-bit unsigned integers (texture indices, flags).
  Uint1,
  Uint2,
};

// Bytes one attribute of format takes in a vertex.
constexpr std::uint32_t vertexFormatBytes(VertexFormat format) {
  switch (format) {
  case VertexFormat::Float1:
  case VertexFormat::Unorm8x4:
  case VertexFormat::Uint8x4:
  case VertexFormat::Uint1:
    return 4;
  case VertexFormat::Float2:
  case VertexFormat::Uint16x4:
  case VertexFormat::Unorm16x4:
  case VertexFormat::Uint2:
    return 8;
  case VertexFormat::Float3:
    return 12;
  case VertexFormat::Float4:
    return 16;
  }
  return 0;
}

struct VertexAttribute {
  // The shader's input location: the order of the fields of its vertex input struct, from 0.
  std::uint32_t location = 0;
  VertexFormat format = VertexFormat::Float3;
  // Bytes from the start of the vertex.
  std::uint32_t offset = 0;
};

// Interleaved vertices from one vertex buffer. A stride of 0 means no vertex buffer: the shader
// makes its vertices from SV_VertexID.
struct VertexLayout {
  std::uint32_t stride = 0;
  std::span<const VertexAttribute> attributes;
};

enum class Topology : std::uint8_t {
  TriangleList,
  TriangleStrip,
  LineList,
};

enum class CullMode : std::uint8_t {
  None,
  Front,
  Back,
};

// Which winding is the front, as Vulkan defines it. With render::perspective (which flips Y), faces
// that are counter-clockwise seen from the front, as in glTF, are CounterClockwise.
enum class FrontFace : std::uint8_t {
  CounterClockwise,
  Clockwise,
};

enum class CompareOp : std::uint8_t {
  Never,
  Less,
  Equal,
  LessOrEqual,
  Greater,
  NotEqual,
  GreaterOrEqual,
  Always,
};

// How a fragment's color combines with the target's. The source is the shader's output, the
// destination the target.
enum class BlendMode : std::uint8_t {
  // Replaces the destination.
  Opaque,
  // src * srcAlpha + dst * (1 - srcAlpha). With alpha 0.5 it is the original's average mode (B/2 +
  // F/2).
  Alpha,
  // src + dst * (1 - srcAlpha), for colors already multiplied by their alpha (RmlUi's output).
  PremultipliedAlpha,
  // dst + src * srcAlpha: the original's additive mode (B + F) with alpha 1, and B + F/4 with alpha
  // 0.25.
  Additive,
  // dst - src * srcAlpha: the original's subtractive mode (B - F) with alpha 1.
  Subtractive,
};

struct DepthState {
  bool test = false;
  bool write = false;
  CompareOp compare = CompareOp::LessOrEqual;
  // Whether CommandList::setDepthBias applies: a dynamic depth bias, for decals and layers drawn
  // over equal depth.
  bool bias = false;
};

// Everything a graphics pipeline fixes. The shaders come from one SPIR-V module holding both entry
// points, as dwe_add_shaders builds them (docs/architecture/render.md). Every pipeline uses the
// bindless table's layout, and draws with dynamic rendering into targets of colorFormat and
// depthFormat (Undefined: no depth target).
struct GraphicsPipelineDesc {
  std::span<const std::uint32_t> spirv;
  std::string_view vertexEntry = "vertexMain";
  std::string_view fragmentEntry = "fragmentMain";
  VertexLayout vertex{};
  Topology topology = Topology::TriangleList;
  CullMode cull = CullMode::None;
  FrontFace frontFace = FrontFace::CounterClockwise;
  DepthState depth{};
  BlendMode blend = BlendMode::Opaque;
  Format colorFormat = Format::Undefined;
  Format depthFormat = Format::Undefined;
  // For error messages and debuggers.
  std::string_view name = "a pipeline";
};

// Checks a description without the GPU: SPIR-V, vertex layout, formats and depth state.
core::Result<void> validate(const GraphicsPipelineDesc& desc);

// A graphics pipeline built from a GraphicsPipelineDesc. Viewport and scissor are dynamic. It may
// be dropped while frames that use it are in flight, but it must not outlive the device.
class GraphicsPipeline {
public:
  static core::Result<GraphicsPipeline> create(Device& device, const BindlessTable& table,
                                               const GraphicsPipelineDesc& desc);

  GraphicsPipeline(const GraphicsPipeline&) = delete;
  GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
  GraphicsPipeline(GraphicsPipeline&& other) noexcept;
  GraphicsPipeline& operator=(GraphicsPipeline&& other) noexcept;
  ~GraphicsPipeline();

  [[nodiscard]] VkPipeline native() const { return pipeline_; }
  [[nodiscard]] VkPipelineLayout layout() const { return layout_; }
  [[nodiscard]] bool hasDepthBias() const { return depthBias_; }

private:
  explicit GraphicsPipeline(Device& device) : device_(&device) {}

  void release();

  Device* device_ = nullptr;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  bool depthBias_ = false;
};

} // namespace dwe::rhi
