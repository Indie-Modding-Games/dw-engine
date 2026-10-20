#include "asset_viewer.hpp"

#include <dwe/assets/image.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/texture.hpp>
#include <dwe/core/log.hpp>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace dwe::debug {
namespace {

constexpr float ListWidth = 320.0F;
constexpr int MaxZoom = 16;
// A new preview starts at the largest whole zoom that keeps it within this many pixels.
constexpr std::uint32_t PreviewSide = 512;
constexpr std::string_view TopLevelGroup = "(top level)";
constexpr ImVec4 ErrorColor{1.0F, 0.4F, 0.4F, 1.0F};

// A preview texture on the GPU and how its file stores it.
struct Upload {
    rhi::Texture texture;
    std::string_view stored;
};

core::Result<Upload> uploadPng(rhi::Device& device, std::span<const std::byte> bytes) {
    const auto image = assets::decodePng(bytes);
    if (!image) {
        return std::unexpected{image.error()};
    }
    // UNORM, like the swapchain: the pixels already hold sRGB values and the debug UI draws them as they are.
    const std::array levels{std::span<const std::byte>{image->pixels}};
    auto texture = rhi::Texture::create(
        device,
        {.format = rhi::TextureFormat::Rgba8Unorm, .width = image->width, .height = image->height, .levels = levels});
    if (!texture) {
        return std::unexpected{texture.error()};
    }
    return Upload{.texture = std::move(*texture), .stored = "PNG"};
}

// Transcodes a cooked texture to BC7 if the GPU samples it, and to RGBA8 if not.
core::Result<Upload> uploadKtx2(rhi::Device& device, std::span<const std::byte> bytes) {
    const bool bc7 = device.supportsBc7();
    const auto data = assets::decodeKtx2(bytes, bc7 ? assets::TexelEncoding::Bc7 : assets::TexelEncoding::Rgba8);
    if (!data) {
        return std::unexpected{data.error()};
    }
    // UNORM even for sRGB colour, as with PNGs: the debug UI draws the stored values as they are.
    const std::vector<std::span<const std::byte>> levels{data->levels.begin(), data->levels.end()};
    auto texture =
        rhi::Texture::create(device, {.format = bc7 ? rhi::TextureFormat::Bc7Unorm : rhi::TextureFormat::Rgba8Unorm,
                                      .width = data->width,
                                      .height = data->height,
                                      .levels = levels});
    if (!texture) {
        return std::unexpected{texture.error()};
    }
    return Upload{.texture = std::move(*texture),
                  .stored = bc7 ? "KTX2 UASTC, transcoded to BC7" : "KTX2 UASTC, transcoded to RGBA8"};
}

std::string_view groupOf(std::string_view id) {
    const std::size_t slash = id.find('/');
    return slash == std::string_view::npos ? TopLevelGroup : id.substr(0, slash);
}

void textWrapped(const std::string& text) {
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
}

} // namespace

AssetViewer::~AssetViewer() {
    releasePreview();
}

void AssetViewer::mount(const std::filesystem::path& folder) {
    const std::string text = assets::pathText(folder);
    text.copy(folder_.data(), folder_.size() - 1);
    folder_[std::min(text.size(), folder_.size() - 1)] = '\0';
    releasePreview();
    selected_.reset();
    vfs_.reset();
    archives_.clear();
    ids_.clear();

    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) {
        status_ = std::format("There is no folder '{}'.", text);
        core::logInfo("asset viewer: no content folder at '{}'", text);
    } else if (auto found = assets::findPacks(disk_, folder); !found) {
        status_ = found.error().message;
        core::logWarning("asset viewer: {}", status_);
    } else if (found->packs.empty()) {
        status_ = std::format("There are no packs in '{}'.", text);
        core::logInfo("asset viewer: no packs in '{}'", text);
    } else if (auto opened = assets::openPacks(disk_, found->packs); !opened) {
        status_ = opened.error().message;
        core::logWarning("asset viewer: {}", status_);
    } else if (auto vfs = assets::VirtualFileSystem::mount(opened->locations); !vfs) {
        status_ = vfs.error().message;
        core::logWarning("asset viewer: {}", status_);
    } else {
        archives_ = std::move(opened->archives);
        vfs_.emplace(std::move(*vfs));
        ids_ = vfs_->ids();
        std::string stack;
        for (const assets::MountedPack& pack : vfs_->packs()) {
            stack += std::format("{}{} {} ({})", stack.empty() ? "" : ", ", pack.manifest.id,
                                 assets::formatVersion(pack.manifest.version), assets::layerName(pack.manifest.layer));
        }
        status_ = std::format("{} assets. Packs from the bottom of the stack up: {}.", ids_.size(), stack);
        core::logInfo("asset viewer: mounted '{}': {} assets; packs {}", text, ids_.size(), stack);
        for (const std::filesystem::path& hidden : found->hidden) {
            status_ += std::format(" {} is left out: the folder with its name is used.", assets::pathText(hidden));
            core::logInfo("asset viewer: '{}' left out: the folder with its name is used", assets::pathText(hidden));
        }
    }
    applyFilter();
}

void AssetViewer::applyFilter() {
    std::string filter{filter_.data()};
    std::ranges::transform(filter, filter.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    visible_.clear();
    groups_.clear();
    // Ids are sorted and '/' sorts before every other character they may have, so the ids of a folder are together.
    // Ids without a folder may fall between them, so they go last, in a group of their own.
    for (const bool topLevel : {false, true}) {
        for (std::size_t index = 0; index < ids_.size(); ++index) {
            const std::string& id = ids_[index].str();
            const std::string_view group = groupOf(id);
            if ((group == TopLevelGroup) != topLevel || !id.contains(filter)) {
                continue;
            }
            if (groups_.empty() || groups_.back().name != group) {
                groups_.push_back(Group{.name = std::string{group}, .begin = visible_.size(), .end = visible_.size()});
            }
            visible_.push_back(index);
            groups_.back().end = visible_.size();
        }
    }
    expandGroups_ = !filter.empty();
}

void AssetViewer::select(const assets::VirtualFileSystem& vfs, const assets::AssetId& id) {
    releasePreview();
    previewError_.clear();
    selected_ = id;
    const assets::AssetSource* source = vfs.resolve(id);
    if (source == nullptr || (source->file.extension() != ".png" && source->file.extension() != ".ktx2")) {
        return;
    }
    auto bytes = vfs.read(id);
    if (!bytes) {
        previewError_ = bytes.error().message;
        return;
    }
    auto upload = source->file.extension() == ".png" ? uploadPng(*device_, *bytes) : uploadKtx2(*device_, *bytes);
    if (!upload) {
        previewError_ = upload.error().message;
        return;
    }
    auto& texture = upload->texture;
    const VkDescriptorSet descriptor =
        ImGui_ImplVulkan_AddTexture(texture.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    zoom_ = static_cast<int>(
        std::clamp<std::uint32_t>(PreviewSide / std::max(texture.width(), texture.height()), 1, MaxZoom));
    preview_.emplace(Preview{
        .texture = std::move(texture), .descriptor = descriptor, .fileBytes = bytes->size(), .stored = upload->stored});
}

void AssetViewer::releasePreview() {
    if (preview_) {
        // The frame being recorded may already draw the old preview.
        device_->destroyLater([descriptor = preview_->descriptor] { ImGui_ImplVulkan_RemoveTexture(descriptor); });
        preview_.reset();
    }
}

void AssetViewer::draw(bool& open) {
    ImGui::SetNextWindowPos({270, 40}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({990, 660}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Assets", &open)) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Mount").x - (ImGui::GetStyle().FramePadding.x * 2) -
                            ImGui::GetStyle().ItemSpacing.x);
    const bool entered = ImGui::InputTextWithHint("##folder", "content folder", folder_.data(), folder_.size(),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Mount") || entered) {
        mount(std::filesystem::path{folder_.data()});
    }
    textWrapped(status_);
    if (!vfs_) {
        ImGui::Separator();
        textWrapped("Packs come from the disc: `dwx extract <disc image>` writes the base pack to content/base. Then "
                    "mount that folder, or a folder of packs, here or with `dwe-game --content <folder>`.");
        ImGui::End();
        return;
    }
    const assets::VirtualFileSystem& vfs = *vfs_;
    ImGui::BeginChild("ids", {ListWidth, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    drawList(vfs);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("details", {0, 0}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    drawDetails(vfs);
    ImGui::EndChild();
    ImGui::End();
}

void AssetViewer::drawList(const assets::VirtualFileSystem& vfs) {
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##filter", "filter", filter_.data(), filter_.size())) {
        applyFilter();
    }
    ImGui::TextUnformatted(std::format("{} of {} ids", visible_.size(), ids_.size()).c_str());
    ImGui::BeginChild("tree");
    for (const Group& group : groups_) {
        if (expandGroups_) {
            ImGui::SetNextItemOpen(true);
        }
        const std::string label = std::format("{} ({})##{}", group.name, group.end - group.begin, group.name);
        if (!ImGui::TreeNode(label.c_str())) {
            continue;
        }
        // Only the rows on screen are built, so a pack with thousands of assets stays fast.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(group.end - group.begin));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const assets::AssetId& id = ids_[visible_[group.begin + static_cast<std::size_t>(row)]];
                const std::string_view name =
                    group.name == TopLevelGroup ? id.str() : std::string_view{id.str()}.substr(group.name.size() + 1);
                if (ImGui::Selectable(std::string{name}.c_str(), selected_ == id) && selected_ != id) {
                    select(vfs, id);
                }
            }
        }
        ImGui::TreePop();
    }
    expandGroups_ = false;
    ImGui::EndChild();
}

void AssetViewer::drawDetails(const assets::VirtualFileSystem& vfs) {
    if (!selected_) {
        ImGui::TextUnformatted("Select an asset.");
        return;
    }
    ImGui::TextUnformatted(selected_->str().c_str());
    const auto sources = vfs.sources(*selected_);
    if (sources.empty()) {
        return;
    }
    const assets::AssetSource& winner = sources.back();
    textWrapped(std::format("from pack '{}': {}", winner.packId, assets::pathText(winner.file)));
    for (auto source = sources.rbegin() + 1; source != sources.rend(); ++source) {
        textWrapped(std::format("overrides pack '{}': {}", source->packId, assets::pathText(source->file)));
    }
    ImGui::Separator();

    if (!previewError_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ErrorColor);
        textWrapped(previewError_);
        ImGui::PopStyleColor();
        return;
    }
    if (!preview_) {
        textWrapped("No preview: the viewer shows PNG and KTX2 textures only. Models need the 3D render path.");
        return;
    }
    const rhi::Texture& texture = preview_->texture;
    ImGui::TextUnformatted(std::format("{}x{} pixels, {} bytes, {}", texture.width(), texture.height(),
                                       preview_->fileBytes, preview_->stored)
                               .c_str());
    ImGui::SetNextItemWidth(200);
    ImGui::SliderInt("zoom", &zoom_, 1, MaxZoom, "%dx");
    // The backend samples with linear filtering unless told otherwise; texels stay sharp squares with nearest.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
    const auto zoom = static_cast<float>(zoom_);
    ImGui::Image(ImTextureRef{reinterpret_cast<ImTextureID>(preview_->descriptor)},
                 {static_cast<float>(texture.width()) * zoom, static_cast<float>(texture.height()) * zoom});
    drawList->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear, nullptr);
}

} // namespace dwe::debug
