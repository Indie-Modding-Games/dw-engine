#pragma once

#include <dwe/core/error.hpp>
#include <dwe/platform/platform.hpp>
#include <dwe/platform/window.hpp>
#include <dwe/rhi/device.hpp>

#include <filesystem>
#include <optional>
#include <string>

#include "options.hpp"

namespace dwe::app {

// Whether this start asks for the player's disc first: a normal game (no autotest, no control
// channel, no dev assets) whose content folder was not named with --content and has no base pack
// yet.
[[nodiscard]] bool needsFirstRun(const Options& options);

// The game's first start (docs/guides/beta-linux.md): a screen in English or Spanish that asks for
// the player's own disc image, reads it with dwx into the user's data folder
// (content::DiscInstall), shows the progress and why it failed (a missing file, another game,
// another region), and gives back the content folder it installed, or nothing when the player
// quits. It is drawn with Dear ImGui, because the game's own UI needs the content it installs; the
// texts are the game pack's localization/<language>/first_run, read from the team's packs.
core::Result<std::optional<std::filesystem::path>>
runFirstRun(platform::Platform& platform, platform::Window& window, rhi::Device& device,
            const std::optional<std::string>& language);

} // namespace dwe::app
