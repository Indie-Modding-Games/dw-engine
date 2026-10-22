#include <dwe/assets/character_model.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>
#include <utility>

namespace dwe::assets {

namespace {

using core::YamlNode;

// The longest animation name: a word such as walk_tired, not text.
constexpr std::size_t MaxAnimationName = 64;

std::unexpected<core::Error> failAt(const YamlNode& node, std::string_view problem) {
  return core::fail(std::format("line {}, column {}: {}", node.line(), node.column(), problem));
}

bool isAnimationName(std::string_view name) {
  if (name.empty() || name.size() > MaxAnimationName) {
    return false;
  }
  return std::ranges::all_of(
      name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

core::Result<std::uint32_t> readClipId(const YamlNode& node) {
  std::uint32_t value = 0;
  if (node.isScalar()) {
    const std::string& text = node.text();
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error == std::errc{} && end == text.data() + text.size() && !text.empty()) {
      return value;
    }
  }
  return failAt(node, "a clip id must be a whole number from 0 to 4294967295");
}

} // namespace

std::optional<std::uint32_t> CharacterModel::clipOf(std::string_view animation) const {
  const auto found = animations.find(animation);
  if (found == animations.end()) {
    return std::nullopt;
  }
  return found->second;
}

core::Result<CharacterModel> parseCharacterModel(std::string_view yaml) {
  auto root = core::parseYaml(yaml);
  if (!root) {
    return std::unexpected{root.error()};
  }
  if (!root->isMapping()) {
    return failAt(*root, "a character model must be a mapping with 'model' and 'animations'");
  }
  std::optional<AssetId> model;
  std::map<std::string, std::uint32_t, std::less<>> animations;
  bool hasAnimations = false;
  for (const core::YamlEntry& entry : root->entries()) {
    if (entry.key == "model") {
      auto id = entry.value.isScalar() ? AssetId::parse(entry.value.text())
                                       : core::fail("not a single value");
      if (!id) {
        return failAt(entry.value,
                      std::format("'model' must be an asset id: {}", id.error().message));
      }
      model = std::move(*id);
    } else if (entry.key == "animations") {
      if (!entry.value.isMapping() || entry.value.entries().empty()) {
        return failAt(entry.value, "'animations' must map animation names to clip ids");
      }
      for (const core::YamlEntry& animation : entry.value.entries()) {
        if (!isAnimationName(animation.key)) {
          return failAt(
              animation.value,
              std::format("'{}' is not an animation name (a-z, 0-9 and _)", animation.key));
        }
        auto clip = readClipId(animation.value);
        if (!clip) {
          return std::unexpected{clip.error()};
        }
        animations.emplace(animation.key, *clip);
      }
      hasAnimations = true;
    } else {
      return failAt(entry.value, std::format("unknown key '{}'", entry.key));
    }
  }
  if (!model || !hasAnimations) {
    return failAt(*root, "a character model needs 'model' and 'animations'");
  }
  return CharacterModel{.model = std::move(*model), .animations = std::move(animations)};
}

core::Result<CharacterModel> loadCharacterModel(const VirtualFileSystem& vfs, const AssetId& id) {
  const auto bytes = vfs.read(id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  auto character =
      parseCharacterModel({reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  if (!character) {
    return core::fail(std::format("{}: {}", id.str(), character.error().message));
  }
  return character;
}

core::Result<void> checkCharacterModel(const CharacterModel& character, const ModelData& model) {
  for (const auto& [animation, clip] : character.animations) {
    if (model.findClip(clip) == nullptr) {
      return core::fail(
          std::format("{} has no clip {} for '{}'", character.model.str(), clip, animation));
    }
  }
  return {};
}

} // namespace dwe::assets
