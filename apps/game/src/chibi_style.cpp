#include "chibi_style.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>
#include <string>

namespace dwe::app {

namespace {

constexpr std::string_view OffName = "off";
constexpr std::string_view ExternalName = "external";
constexpr std::string_view AllName = "all";

} // namespace

std::optional<ChibiMode> parseChibiMode(std::string_view name) {
  if (name == OffName) {
    return ChibiMode::Off;
  }
  if (name == ExternalName) {
    return ChibiMode::External;
  }
  if (name == AllName) {
    return ChibiMode::All;
  }
  return std::nullopt;
}

std::string_view chibiModeName(ChibiMode mode) {
  switch (mode) {
  case ChibiMode::Off:
    return OffName;
  case ChibiMode::All:
    return AllName;
  case ChibiMode::External:
    break;
  }
  return ExternalName;
}

ChibiMode startChibiMode(const std::optional<std::string>& saved) {
  if (!saved) {
    return ChibiMode::External;
  }
  const auto mode = parseChibiMode(*saved);
  if (!mode) {
    core::logWarning("chibi style: unknown saved mode {}", *saved);
  }
  return mode.value_or(ChibiMode::External);
}

bool isOriginalModel(const assets::VirtualFileSystem& files, const assets::AssetId& model) {
  const assets::AssetSource* source = files.resolve(model);
  if (source == nullptr) {
    return true;
  }
  const auto pack =
      std::ranges::find(files.packs(), source->packId, [](const assets::MountedPack& mounted) {
        return std::string_view{mounted.manifest.id};
      });
  return pack != files.packs().end() && pack->origin == assets::PackOrigin::Engine &&
         assets::isEngineLayer(pack->manifest.layer);
}

ChibiModeChanger::ChibiModeChanger(UserSettings& settings,
                                   std::optional<std::filesystem::path> settingsFile)
    : settings_{&settings}, settingsFile_{std::move(settingsFile)},
      mode_{startChibiMode(settings.chibiStyle)} {}

ChibiMode ChibiModeChanger::change(const ui::UiValue& value) {
  const std::string* name = std::get_if<std::string>(&value);
  const auto mode = name == nullptr ? std::nullopt : parseChibiMode(*name);
  if (!mode) {
    return mode_;
  }
  mode_ = *mode;
  settings_->chibiStyle = std::string{chibiModeName(mode_)};
  if (settingsFile_) {
    saveUserSettings(*settingsFile_, *settings_);
  }
  return mode_;
}

animation::ChibiOverrides loadChibiOverrides(const assets::VirtualFileSystem& files) {
  const auto id = assets::AssetId::parse("models/chibi");
  if (!id || files.resolve(*id) == nullptr) {
    return {};
  }
  const auto bytes = files.read(*id);
  if (!bytes) {
    core::logWarning("chibi style: {}", bytes.error().message);
    return {};
  }
  auto overrides =
      animation::parseChibiOverrides({reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  if (!overrides) {
    core::logWarning("chibi style: models/chibi: {}", overrides.error().message);
    return {};
  }
  return std::move(*overrides);
}

std::string_view chibiSpeciesOf(const assets::AssetId& model) {
  constexpr std::string_view Prefix = "digimon/";
  constexpr std::string_view Suffix = "/model";
  const std::string_view path = model.str();
  if (!path.starts_with(Prefix) || !path.ends_with(Suffix) ||
      path.size() <= Prefix.size() + Suffix.size()) {
    return {};
  }
  return path.substr(Prefix.size(), path.size() - Prefix.size() - Suffix.size());
}

animation::ChibiRig chibiRigOf(const assets::AssetId& id, const assets::ModelData& model,
                               const animation::ChibiOverrides& overrides) {
  const auto found = overrides.find(chibiSpeciesOf(id));
  animation::ChibiRig rig =
      animation::buildChibiRig(model, found != overrides.end() ? &found->second : nullptr);
  core::logDebug("chibi style: {} is {} (legs {}, arms {}, wings {}, tails {}, head {})", id.str(),
                 animation::nameOf(rig.classification.shape), rig.classification.legs,
                 rig.classification.arms, rig.classification.wings, rig.classification.tails,
                 rig.classification.head ? static_cast<int>(*rig.classification.head) : -1);
  return rig;
}

std::span<const glm::mat4> ChibiJoints::of(const assets::ModelData& model,
                                           const animation::ChibiRig& rig,
                                           const animation::AnimationPlayer& player) {
  if (!rig.active) {
    return player.jointMatrices();
  }
  world_.resize(model.nodes.size());
  joints_.resize(player.jointMatrices().size());
  animation::computeChibiJointMatrices(model, rig, player.pose(), player.jointMatrices(), world_,
                                       joints_);
  return joints_;
}

} // namespace dwe::app
