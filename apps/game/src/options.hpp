#pragma once

#include <dwe/core/math.hpp>
#include <dwe/render/map_scene.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

// What the command line asks for (see Usage). With no view option the game starts in the field.
struct Options {
    // The content folder: its packs, those in its packs/ folder, and the team's packs from the source tree when it
    // has no game pack of its own.
    std::filesystem::path content = "content";
    // Folders with more packs, such as mods (--mods, may repeat). The VFS stacks them by layer and priority.
    std::vector<std::filesystem::path> mods;
    // The ruleset the game plays with (--ruleset <id>): rules/<id>/ in the packs.
    std::string ruleset{dwe::ruleset::OriginalRuleset};
    // Set by --check-content: open every asset of the folder's packs and quit, without a window.
    bool checkContent = false;
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool vsync = true;
    // The field's start, instead of field/warps (--start <map>[,<exit>]).
    std::optional<std::string> startMap;
    int startExit = 0;
    // The time of day to start at (the clock at 8:00, 16:00 or 20:00); the field rules' start hour when not given.
    std::optional<render::TimeOfDay> time;

    // A scripted run (--autotest <route.yaml>), and where its screenshots go (--shots <folder>).
    std::optional<std::filesystem::path> autotest;
    std::optional<std::filesystem::path> shots;
    // Kinds of steps to skip, where the screen cannot run them (--skip-steps fullscreen on an X server without a
    // window manager).
    std::vector<std::string> skipSteps;

    // Development views instead of the field.
    // The render foundation's test scene (--test-scene), with a texture made and destroyed every frame
    // (--texture-churn, render.md).
    bool testScene = false;
    bool textureChurn = false;
    // Maps shown by app::MapPreview (--map): the first, and the others in turn with debug_next_map.
    std::vector<std::string> maps;
    std::optional<glm::vec2> focus;
    // The UI demo (--ui-demo), with this file of virtual gamepad commands.
    bool uiDemo = false;
    std::optional<std::filesystem::path> virtualGamepad;
    // Characters of the game pack shown by app::ModelViewer (--model-viewer [id...]).
    bool modelViewer = false;
    std::vector<std::string> characters;
};

inline constexpr std::string_view Usage =
    "dwe-game [--content <folder>] [--mods <folder>]... [--ruleset <id>] [--size <width>x<height>] [--fullscreen] "
    "[--no-vsync] [--start <map>[,<exit>]] [--time day|dusk|night] [--autotest <route.yaml> [--shots <folder>] "
    "[--skip-steps "
    "<kind>[,<kind>...]]] | "
    "--check-content <folder> | --test-scene [--texture-churn] | --map <id>[,<id>...] [--focus <x>,<z>] | "
    "--ui-demo [--virtual-gamepad <file>] | --model-viewer [character id...]";

inline constexpr std::string_view RulesetOption = "--ruleset";

// Reads the command line; logs what is wrong and gives nothing for a bad one.
std::optional<Options> parseOptions(std::span<char* const> arguments);

} // namespace dwe::app
