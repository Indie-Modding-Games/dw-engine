#include <dwe/rhi/pipeline.hpp>

#include <array>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "vulkan_util.hpp"

namespace dwe::rhi {
namespace {

constexpr std::uint32_t SpirvMagic = 0x07230203U;

VkFormat toVulkan(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float1:
        return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2:
        return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::Unorm8x4:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case VertexFormat::Uint8x4:
        return VK_FORMAT_R8G8B8A8_UINT;
    case VertexFormat::Uint16x4:
        return VK_FORMAT_R16G16B16A16_UINT;
    case VertexFormat::Unorm16x4:
        return VK_FORMAT_R16G16B16A16_UNORM;
    case VertexFormat::Uint1:
        return VK_FORMAT_R32_UINT;
    case VertexFormat::Uint2:
        return VK_FORMAT_R32G32_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

VkPrimitiveTopology toVulkan(Topology topology) {
    switch (topology) {
    case Topology::TriangleList:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case Topology::TriangleStrip:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case Topology::LineList:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCullModeFlags toVulkan(CullMode cull) {
    switch (cull) {
    case CullMode::None:
        return VK_CULL_MODE_NONE;
    case CullMode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back:
        return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

VkCompareOp toVulkan(CompareOp compare) {
    switch (compare) {
    case CompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case CompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case CompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessOrEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterOrEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always:
        return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

VkPipelineColorBlendAttachmentState toVulkan(BlendMode blend) {
    VkPipelineColorBlendAttachmentState state{};
    state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (blend == BlendMode::Opaque) {
        return state;
    }
    state.blendEnable = VK_TRUE;
    state.colorBlendOp = VK_BLEND_OP_ADD;
    state.alphaBlendOp = VK_BLEND_OP_ADD;
    state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    switch (blend) {
    case BlendMode::Opaque:
    case BlendMode::Alpha:
        state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case BlendMode::PremultipliedAlpha:
        state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case BlendMode::Additive:
    case BlendMode::Subtractive:
        state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        state.colorBlendOp = blend == BlendMode::Additive ? VK_BLEND_OP_ADD : VK_BLEND_OP_REVERSE_SUBTRACT;
        // The target's alpha stays as it is.
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    }
    return state;
}

} // namespace

core::Result<void> validate(const GraphicsPipelineDesc& desc) {
    const std::string_view name = desc.name;
    if (desc.spirv.size() < 5 || desc.spirv.front() != SpirvMagic) {
        return core::fail(std::format("{}: the shader is not SPIR-V", name));
    }
    if (desc.vertexEntry.empty() || desc.fragmentEntry.empty()) {
        return core::fail(std::format("{}: needs a vertex and a fragment entry point", name));
    }
    if (desc.colorFormat == Format::Undefined || isDepthFormat(desc.colorFormat)) {
        return core::fail(std::format("{}: needs a color target format", name));
    }
    if (desc.depthFormat != Format::Undefined && !isDepthFormat(desc.depthFormat)) {
        return core::fail(std::format("{}: the depth target format holds no depth", name));
    }
    if (desc.depthFormat == Format::Undefined && (desc.depth.test || desc.depth.write || desc.depth.bias)) {
        return core::fail(std::format("{}: depth testing, writing or bias needs a depth target", name));
    }
    if (desc.vertex.stride == 0 && !desc.vertex.attributes.empty()) {
        return core::fail(std::format("{}: vertex attributes need a vertex stride", name));
    }
    std::uint64_t locations = 0;
    for (const VertexAttribute& attribute : desc.vertex.attributes) {
        if (attribute.location >= 64 || (locations & (std::uint64_t{1} << attribute.location)) != 0) {
            return core::fail(std::format("{}: vertex location {} is repeated or too large", name, attribute.location));
        }
        locations |= std::uint64_t{1} << attribute.location;
        if (std::uint64_t{attribute.offset} + vertexFormatBytes(attribute.format) > desc.vertex.stride) {
            return core::fail(std::format("{}: vertex location {} ends past the stride of {} bytes", name,
                                          attribute.location, desc.vertex.stride));
        }
    }
    return {};
}

core::Result<GraphicsPipeline> GraphicsPipeline::create(Device& device, const BindlessTable& table,
                                                        const GraphicsPipelineDesc& desc) {
    if (auto valid = validate(desc); !valid) {
        return std::unexpected{valid.error()};
    }
    const VkDevice gpu = device.nativeHandles().device;

    const VkShaderModuleCreateInfo moduleInfo{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                              .pNext = nullptr,
                                              .flags = 0,
                                              .codeSize = desc.spirv.size_bytes(),
                                              .pCode = desc.spirv.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(gpu, &moduleInfo, nullptr, &module) != VK_SUCCESS) {
        return core::fail(std::format("{}: could not create the shader module", desc.name));
    }

    // Vulkan wants the entry point names null-terminated.
    const std::string vertexEntry{desc.vertexEntry};
    const std::string fragmentEntry{desc.fragmentEntry};
    const std::array stages{
        VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                        .pNext = nullptr,
                                        .flags = 0,
                                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                        .module = module,
                                        .pName = vertexEntry.c_str(),
                                        .pSpecializationInfo = nullptr},
        VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                        .pNext = nullptr,
                                        .flags = 0,
                                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                        .module = module,
                                        .pName = fragmentEntry.c_str(),
                                        .pSpecializationInfo = nullptr},
    };

    const VkVertexInputBindingDescription binding{
        .binding = 0, .stride = desc.vertex.stride, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> attributes;
    attributes.reserve(desc.vertex.attributes.size());
    for (const VertexAttribute& attribute : desc.vertex.attributes) {
        attributes.push_back({.location = attribute.location,
                              .binding = 0,
                              .format = toVulkan(attribute.format),
                              .offset = attribute.offset});
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (desc.vertex.stride != 0) {
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = toVulkan(desc.topology);

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = toVulkan(desc.cull);
    rasterization.frontFace =
        desc.frontFace == FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rasterization.depthBiasEnable = desc.depth.bias ? VK_TRUE : VK_FALSE;
    rasterization.lineWidth = 1.0F;

    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 0.0F,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.depth.test ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depth.write ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = toVulkan(desc.depth.compare);

    const VkPipelineColorBlendAttachmentState blendAttachment = toVulkan(desc.blend);
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    std::vector<VkDynamicState> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    if (desc.depth.bias) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
    }
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    const VkFormat colorFormat = toVulkan(desc.colorFormat);
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat = toVulkan(desc.depthFormat);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewport;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = table.pipelineLayout();

    GraphicsPipeline pipeline{device};
    pipeline.layout_ = table.pipelineLayout();
    pipeline.depthBias_ = desc.depth.bias;
    const VkResult result =
        vkCreateGraphicsPipelines(gpu, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline.pipeline_);
    vkDestroyShaderModule(gpu, module, nullptr);
    if (result != VK_SUCCESS) {
        return core::fail(
            std::format("{}: could not create the pipeline (Vulkan error {})", desc.name, static_cast<int>(result)));
    }
    return pipeline;
}

GraphicsPipeline::GraphicsPipeline(GraphicsPipeline&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)), pipeline_(std::exchange(other.pipeline_, VK_NULL_HANDLE)),
      layout_(std::exchange(other.layout_, VK_NULL_HANDLE)), depthBias_(std::exchange(other.depthBias_, false)) {}

GraphicsPipeline& GraphicsPipeline::operator=(GraphicsPipeline&& other) noexcept {
    if (this != &other) {
        release();
        device_ = std::exchange(other.device_, nullptr);
        pipeline_ = std::exchange(other.pipeline_, VK_NULL_HANDLE);
        layout_ = std::exchange(other.layout_, VK_NULL_HANDLE);
        depthBias_ = std::exchange(other.depthBias_, false);
    }
    return *this;
}

GraphicsPipeline::~GraphicsPipeline() {
    release();
}

void GraphicsPipeline::release() {
    if (device_ == nullptr) {
        return;
    }
    device_->destroyLater([device = device_->nativeHandles().device, pipeline = pipeline_] {
        vkDestroyPipeline(device, pipeline, nullptr);
    });
    device_ = nullptr;
}

} // namespace dwe::rhi
