#include "chibi_style.hpp"

#include <dwe/core/log.hpp>

#include <string>

namespace dwe::app {

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
