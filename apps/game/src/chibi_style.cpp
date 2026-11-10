#include "chibi_style.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>
#include <format>
#include <string>

namespace dwe::app {

namespace {

constexpr std::string_view OffName = "off";
constexpr std::string_view ExternalName = "external";
// The mode that reshaped the original's models too, gone since they are the reference.
constexpr std::string_view OldAllName = "all";

} // namespace

std::optional<ChibiMode> parseChibiMode(std::string_view name) {
  if (name == OffName) {
    return ChibiMode::Off;
  }
  if (name == ExternalName || name == OldAllName) {
    return ChibiMode::External;
  }
  return std::nullopt;
}

std::string_view chibiModeName(ChibiMode mode) {
  switch (mode) {
  case ChibiMode::Off:
    return OffName;
  case ChibiMode::External:
  case ChibiMode::EveryModel:
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

namespace {

// A game pack file's text, or nothing when it is missing or cannot be read (logged).
std::optional<std::string> readPackText(const assets::VirtualFileSystem& files,
                                        std::string_view path) {
  const auto id = assets::AssetId::parse(path);
  if (!id || files.resolve(*id) == nullptr) {
    return std::nullopt;
  }
  const auto bytes = files.read(*id);
  if (!bytes) {
    core::logWarning("chibi style: {}", bytes.error().message);
    return std::nullopt;
  }
  return std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

} // namespace

ChibiStyleData loadChibiStyleData(const assets::VirtualFileSystem& files) {
  ChibiStyleData data;
  if (const auto text = readPackText(files, "models/chibi_profile")) {
    auto profile = animation::parseChibiProfile(*text);
    if (profile) {
      data.profile = std::move(*profile);
    } else {
      core::logWarning("chibi style: models/chibi_profile: {}", profile.error().message);
    }
  } else {
    core::logWarning("chibi style: no models/chibi_profile, no model is reshaped");
  }
  if (const auto text = readPackText(files, "models/chibi")) {
    auto overrides = animation::parseChibiOverrides(*text);
    if (overrides) {
      data.overrides = std::move(*overrides);
    } else {
      core::logWarning("chibi style: models/chibi: {}", overrides.error().message);
    }
  }
  return data;
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

std::string chibiDecision(const assets::AssetId& id, const animation::ChibiRig& rig) {
  const animation::ChibiClassification& found = rig.classification;
  std::string line =
      std::format("chibi style: {} is {} (legs {}, arms {}, wings {}, tails {}, head {})", id.str(),
                  animation::nameOf(found.shape), found.legs, found.arms, found.wings, found.tails,
                  found.head ? static_cast<int>(*found.head) : -1);
  if (rig.alreadyChibi) {
    line += ": chibi already, drawn as it is";
  } else if (rig.active) {
    line += std::format(
        ": not chibi, reshaped (head x{:.2f}, arm x{:.2f}, leg x{:.2f}, hand x{:.2f}, "
        "foot x{:.2f}, neck x{:.2f}, tail x{:.2f}, wing x{:.2f})",
        rig.proportions.head, rig.proportions.arm, rig.proportions.leg, rig.proportions.hand,
        rig.proportions.foot, rig.proportions.neck, rig.proportions.tail, rig.proportions.wing);
  } else {
    line += ": not measured, drawn as it is";
  }
  for (const animation::BodyRatio ratio : animation::BodyRatios) {
    const auto own = rig.measure[ratio];
    if (!own) {
      continue;
    }
    line += std::format(" | {} {:.2f}", animation::nameOf(ratio), *own);
    if (const auto& range = rig.reference[ratio]) {
      line += std::format(" [{:.2f}-{:.2f}]", range->low, range->high);
    }
    if (rig.active) {
      line += std::format(" -> {:.2f}", rig.drawn[ratio].value_or(0.0F));
    }
  }
  return line;
}

animation::ChibiRig chibiRigOf(const assets::AssetId& id, const assets::ModelData& model,
                               const ChibiStyleData& style, bool original) {
  const auto found = style.overrides.find(chibiSpeciesOf(id));
  animation::ChibiRig rig = animation::buildChibiRig(
      model, style.profile, found != style.overrides.end() ? &found->second : nullptr);
  if (original) {
    core::logDebug("{}", chibiDecision(id, rig));
  } else {
    core::logInfo("{}", chibiDecision(id, rig));
  }
  return rig;
}

std::span<const glm::mat4> ChibiJoints::of(const assets::ModelData& model,
                                           const animation::ChibiRig& rig,
                                           const animation::AnimationPlayer& player) {
  return of(model, rig, player.pose(), player.jointMatrices());
}

std::span<const glm::mat4> ChibiJoints::of(const assets::ModelData& model,
                                           const animation::ChibiRig& rig,
                                           std::span<const assets::NodeTransform> pose,
                                           std::span<const glm::mat4> joints) {
  if (!rig.active) {
    return joints;
  }
  world_.resize(model.nodes.size());
  joints_.resize(joints.size());
  animation::computeChibiJointMatrices(model, rig, pose, joints, world_, joints_);
  return joints_;
}

} // namespace dwe::app
