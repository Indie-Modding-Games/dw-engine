#pragma once

#include <dwe/animation/animation_player.hpp>
#include <dwe/animation/chibi.hpp>
#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/math.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "user_settings.hpp"

namespace dwe::app {

// The chibi style (an engine improvement; the original has none): characters drawn with a big head
// and short limbs (animation/chibi.hpp). It changes the drawing only: animations, collisions and
// every size the game reads stay the model's own. Its modes: off; external models (the default),
// only the models that do not come from the original game, a mod's or an imported one, so that
// they match the original's look; or all, every character. A UI setting saved with the user's
// settings, and the command with the same name that the options send; `--chibi` draws all for a
// run (captures).
inline constexpr std::string_view ChibiStyleSetting = "chibi_style";

enum class ChibiMode : std::uint8_t { Off, External, All };

[[nodiscard]] std::optional<ChibiMode> parseChibiMode(std::string_view name);
[[nodiscard]] std::string_view chibiModeName(ChibiMode mode);
// The saved mode, or external models when there is none or it is not known.
[[nodiscard]] ChibiMode startChibiMode(const std::optional<std::string>& saved);

// Whether a model comes from the original game: the pack that provides it is one of the engine's
// base or remaster layers. A mod's model, or a mod's replacement of an original one, does not.
[[nodiscard]] bool isOriginalModel(const assets::VirtualFileSystem& files,
                                   const assets::AssetId& model);

// Whether a model is drawn chibi in this mode.
[[nodiscard]] constexpr bool drawsChibi(ChibiMode mode, bool originalModel) {
  return mode == ChibiMode::All || (mode == ChibiMode::External && !originalModel);
}

// Keeps the mode in the user's settings: the command's value, when it is a mode, becomes the mode
// and is saved.
class ChibiModeChanger {
public:
  // settingsFile is where the choice is saved; nothing for runs that must not touch the user's
  // settings (autotests). The settings must outlive the changer.
  ChibiModeChanger(UserSettings& settings, std::optional<std::filesystem::path> settingsFile);

  [[nodiscard]] ChibiMode mode() const { return mode_; }
  ChibiMode change(const ui::UiValue& value);

private:
  UserSettings* settings_;
  std::optional<std::filesystem::path> settingsFile_;
  ChibiMode mode_;
};

// The game pack's per-species corrections (models/chibi.yaml); none when the file is missing or
// wrong (logged).
[[nodiscard]] animation::ChibiOverrides loadChibiOverrides(const assets::VirtualFileSystem& files);

// The species folder of a species model id (<species folder>/model), or empty.
[[nodiscard]] std::string_view chibiSpeciesOf(const assets::AssetId& model);

// The rig of a model with its species' override, if any.
[[nodiscard]] animation::ChibiRig chibiRigOf(const assets::AssetId& id,
                                             const assets::ModelData& model,
                                             const animation::ChibiOverrides& overrides);

// Scratch for the chibi matrices of one instance at a time, so that drawing allocates nothing once
// warm.
class ChibiJoints {
public:
  // The player's joint matrices drawn chibi; the original's for an inactive rig.
  [[nodiscard]] std::span<const glm::mat4> of(const assets::ModelData& model,
                                              const animation::ChibiRig& rig,
                                              const animation::AnimationPlayer& player);

private:
  std::vector<glm::mat4> world_;
  std::vector<glm::mat4> joints_;
};

} // namespace dwe::app
