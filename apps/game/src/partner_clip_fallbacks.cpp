#include "partner_clip_fallbacks.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace dwe::app {
namespace {

constexpr std::string_view IdleAnimation = "idle";
constexpr std::string_view WalkAnimation = "walk";
constexpr std::array<std::string_view, 3> MovingAnimations{WalkAnimation, "walk_tired", "run"};

} // namespace

std::map<std::uint32_t, std::uint32_t>
partnerClipFallbacks(const assets::CharacterModel& character,
                     const std::function<bool(std::uint32_t)>& hasClip) {
  std::map<std::uint32_t, std::uint32_t> fallbacks;
  const std::optional<std::uint32_t> idle = character.clipOf(IdleAnimation);
  if (!idle || !hasClip(*idle)) {
    return fallbacks;
  }
  std::optional<std::uint32_t> walk = character.clipOf(WalkAnimation);
  if (walk && !hasClip(*walk)) {
    walk.reset();
  }
  for (const auto& [name, clip] : character.animations) {
    if (hasClip(clip)) {
      continue;
    }
    const bool moving = std::ranges::contains(MovingAnimations, name);
    fallbacks.insert_or_assign(clip, moving && walk ? *walk : *idle);
  }
  return fallbacks;
}

} // namespace dwe::app
