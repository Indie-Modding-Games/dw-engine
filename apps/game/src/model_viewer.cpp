#include "model_viewer.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/math.hpp>
#include <dwe/render/camera.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>
#include <utility>

namespace dwe::app {

namespace {

// Room between actors, in world units (100 = one collision cell).
constexpr float Spacing = 350.0F;
constexpr float CameraTurnSeconds = 40.0F;
constexpr float FieldOfView = 0.6F;
constexpr float NearPlane = 10.0F;
constexpr float FarPlane = 20000.0F;
constexpr float BaseDistance = 2200.0F;
constexpr float BaseHeight = 900.0F;
constexpr float WideAspect = 16.0F / 9.0F;

} // namespace

core::Result<std::unique_ptr<ModelViewer>> ModelViewer::create(render::Renderer& renderer,
                                                               const std::filesystem::path& content,
                                                               const std::filesystem::path& gamePack,
                                                               std::span<const std::string> characters) {
    auto models = render::ModelRenderer::create(renderer);
    if (!models) {
        return std::unexpected{models.error()};
    }
    std::unique_ptr<ModelViewer> viewer{new ModelViewer{std::move(*models)}};
    // The models are read once, so the file systems and the VFS live only while loading.
    const assets::DiskFileSystem disk;
    auto packs = assets::findPacks(disk, content);
    if (!packs) {
        return std::unexpected{packs.error()};
    }
    packs->packs.push_back({.path = gamePack, .kind = assets::FoundPack::Kind::Folder});
    const auto opened = assets::openPacks(disk, packs->packs);
    if (!opened) {
        return std::unexpected{opened.error()};
    }
    auto vfs = assets::VirtualFileSystem::mount(opened->locations);
    if (!vfs) {
        return std::unexpected{vfs.error()};
    }
    for (std::size_t row = 0; row < characters.size(); ++row) {
        const auto id = assets::AssetId::parse(characters[row]);
        if (!id) {
            return std::unexpected{id.error()};
        }
        auto character = assets::loadCharacterModel(*vfs, *id);
        if (!character) {
            return std::unexpected{character.error()};
        }
        auto model = assets::loadModel(*vfs, character->model);
        if (!model) {
            return std::unexpected{model.error()};
        }
        if (auto checked = assets::checkCharacterModel(*character, *model); !checked) {
            return std::unexpected{checked.error()};
        }
        const auto addStart = std::chrono::steady_clock::now();
        auto handle = viewer->models_->add(*model);
        if (!handle) {
            return std::unexpected{handle.error()};
        }
        const std::chrono::duration<double, std::milli> addTime = std::chrono::steady_clock::now() - addStart;
        viewer->data_.push_back(std::make_unique<assets::ModelData>(std::move(*model)));
        viewer->handles_.push_back(*handle);
        const assets::ModelData& data = *viewer->data_.back();
        float column = -0.5F * static_cast<float>(character->animations.size() - 1);
        for (const auto& [animation, clip] : character->animations) {
            Actor actor{.model = viewer->data_.size() - 1,
                        .position = {column * Spacing, 0.0F, (static_cast<float>(row) - 0.5F) * Spacing},
                        .animation = animation,
                        .player = std::make_unique<animation::AnimationPlayer>(data)};
            if (!actor.player->play(clip)) {
                return core::fail(std::format("{} has no clip {}", character->model.str(), clip));
            }
            viewer->actors_.push_back(std::move(actor));
            column += 1.0F;
        }
        core::logInfo("model viewer: {} ({}) with {} animations, uploaded in {:.2f} ms", characters[row],
                      character->model.str(), character->animations.size(), addTime.count());
    }
    return viewer;
}

void ModelViewer::update(double seconds, rhi::Extent extent) {
    const auto step = static_cast<float>(seconds - time_);
    time_ = seconds;
    // A frame that was not drawn (a minimised window) left its instances behind.
    models_->clear();
    for (Actor& actor : actors_) {
        // In place: the root motion is left out, as the field decides how an entity moves.
        actor.player->advance(step);
        models_->submit({.model = handles_[actor.model],
                         .world = glm::translate(glm::mat4{1.0F}, actor.position),
                         .joints = actor.player->jointMatrices()});
    }
    const auto angle = static_cast<float>(seconds) * 2.0F * std::numbers::pi_v<float> / CameraTurnSeconds;
    const float aspect =
        extent.height > 0 ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0F;
    // The whole row fits a 16:9 target; a narrower one moves the camera back so it still does.
    const float distance = BaseDistance * std::max(1.0F, WideAspect / aspect);
    const glm::vec3 eye{distance * std::sin(angle), BaseHeight * distance / BaseDistance, distance * std::cos(angle)};
    const glm::vec3 target{0.0F, 120.0F, 0.0F};
    models_->setCamera({.view = glm::lookAt(eye, target, glm::vec3{0, 1, 0}),
                        .projection = render::perspective(FieldOfView, aspect, NearPlane, FarPlane),
                        // An empty viewport is the whole target, which may have been resized since `extent`.
                        .viewport = {}});
}

} // namespace dwe::app
