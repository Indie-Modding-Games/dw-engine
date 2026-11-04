#pragma once

#include <dwe/core/math.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/render/map_scene.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/ui/text_font.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

// What the command line asks for (see Usage). With no view option the game starts in the field.
struct Options {
  // The content folder: its packs, those in its packs/ folder, and the team's packs (a package's or
  // the source tree's) when it has no game pack of its own. Without --content, main picks
  // content::defaultContentFolder: ./content when it has a base pack, else the one the first start
  // installs in the user's data folder.
  std::filesystem::path content = "content";
  // Whether --content (or --check-content) named it: a folder the player did not choose never asks
  // for the disc.
  bool contentGiven = false;
  // Folders with more packs, such as mods (--mods, may repeat). The VFS stacks them by layer and
  // priority.
  std::vector<std::filesystem::path> mods;
  // Set by --dev-assets: also use the local assets of the dev assets config, with a banner on
  // screen (docs/architecture/assets-de-desarrollo.md). Never on by default.
  bool devAssets = false;
  // The ruleset the game plays with (--ruleset <id>): rules/<id>/ in the packs.
  std::string ruleset{dwe::ruleset::OriginalRuleset};
  // The story lock's stage to play (--story-stage <id>|all) instead of the ruleset's active_stage;
  // "all" opens everything, for development and tests. Empty: the ruleset's.
  std::string storyStage;
  // The folder of the save files (--saves); the user's data folder when not given.
  std::optional<std::filesystem::path> saves;
  // Where the bug reports (F12) go (--reports); `reports` in the user's data folder when not given.
  std::optional<std::filesystem::path> reports;
  // Set by --list-saves: log the save slots and quit, without a window.
  bool listSaves = false;
  // Set by --check-content: open every asset of the folder's packs and quit, without a window.
  bool checkContent = false;
  // Set by --dump-signs <map>[,<map>...]: write the maps' images and their signs to --shots and
  // quit, without a window (sign_dump.hpp).
  std::vector<std::string> dumpSigns;
  int width = 1280;
  int height = 720;
  bool fullscreen = false;
  bool vsync = true;
  // Start with the debug overlay's tuning panel open (--tuning), for manual tests and screenshots.
  bool tuning = false;
  // Start with free exploration on (--explore) for this run, whatever the saved setting
  // (docs/architecture/warps-de-script.md). Never on by default.
  bool explore = false;
  // Draw the characters in the chibi style (--chibi) for this run, whatever the saved setting
  // (chibi_style.hpp); in the model viewer, each character twice: as the original and chibi.
  bool chibi = false;
  // The model viewer's models turned by this many degrees (--viewer-yaw; 0 shows their fronts, 90
  // a side) under a fixed camera, instead of the camera slowly going round.
  std::optional<float> viewerYaw;
  // The field's start, instead of field/warps (--start <map>[,<exit>]).
  std::optional<std::string> startMap;
  int startExit = 0;
  // How the game starts: a new game (--new-game, also when nothing is given) or a saved one (--load
  // <slot>, as slot_01 or autosave_01).
  bool newGame = false;
  std::optional<game::save::SlotId> load;
  // How a new game's story starts (--new-game-chain original|seeded, or an autotest route's
  // new_game) instead of the ruleset's story_scenes chain: seeded skips the opening.
  std::optional<session::NewGameChain> newGameChain;
  // The game clock's speed (--clock-speed <x>): 1 is the original's (a game minute per real
  // second), 60 a game hour per real second.
  std::optional<double> clockSpeed;
  // The seed of a new game's random numbers (--seed <n>), so that a route plays the same every run;
  // a new seed from the system clock when not given. A loaded game keeps the random state it saved.
  std::optional<std::uint64_t> seed;
  // For an oracle run (--original-rolls <state>): the battles roll with the original's generator
  // from its 32-bit state (decimal or 0x hex), the one the original had as its fight began.
  std::optional<std::uint32_t> originalRolls;
  // The time of day to start at (the clock at 8:00, 16:00 or 20:00); the field rules' start hour
  // when not given.
  std::optional<render::TimeOfDay> time;
  // The font of "original look" text (--text-font modern|original), instead of the packs' default;
  // for autotests.
  std::optional<ui::TextFont> textFont;
  // The skin of the UI (--ui-skin <id>), instead of the saved or the packs' default; for autotests.
  std::optional<std::string> uiSkin;
  // The field's enhanced graphics on top of the saved ones (--graphics shadows=off|round|sun,
  // lighting=on|off,colors=on|off; graphics_options.hpp): for captures and autotests, unsaved.
  std::optional<std::string> graphics;

  // The language of the texts (--lang en|es...), instead of the saved one or the system's; for
  // autotests.
  std::optional<std::string> language;

  // A scripted run (--autotest <route.yaml>), and where its screenshots go (--shots <folder>).
  std::optional<std::filesystem::path> autotest;
  std::optional<std::filesystem::path> shots;
  // Kinds of steps to skip, where the screen cannot run them (--skip-steps fullscreen on an X
  // server without a window manager).
  std::vector<std::string> skipSteps;
  // A line per logic tick of where the characters and the fighters stand (--trace <file>,
  // state_trace.hpp), to compare a run with the original's.
  std::optional<std::filesystem::path> trace;
  // The debug control channel's socket (--control <path>, remote_control.hpp), and whether the
  // logic waits paused for it before its first tick (--control-paused).
  std::optional<std::filesystem::path> control;
  bool controlPaused = false;

  // Development views instead of the field.
  // The render foundation's test scene (--test-scene), with a texture made and destroyed every
  // frame
  // (--texture-churn, render.md).
  bool testScene = false;
  bool textureChurn = false;
  // Maps shown by app::MapPreview (--map): the first, and the others in turn with debug_next_map.
  std::vector<std::string> maps;
  std::optional<glm::vec2> focus;
  // The UI demo (--ui-demo), with this file of virtual gamepad commands.
  bool uiDemo = false;
  bool uiDemoFishing = false;
  bool uiDemoPlayer = false;
  std::optional<std::filesystem::path> uiDemoChart;
  std::optional<std::filesystem::path> uiDemoChartDetail;
  bool uiDemoChartAllRaised = false;
  int uiDemoBagSlots = 0;
  std::optional<std::filesystem::path> virtualGamepad;
  // Characters of the game pack shown by app::ModelViewer (--model-viewer [id...]).
  bool modelViewer = false;
  std::vector<std::string> characters;
};

inline constexpr std::string_view Usage =
    "dwe-game [--content <folder>] [--mods <folder>]... [--dev-assets] [--ruleset <id>] [--story-stage <id>|all] "
    "[--saves <folder>] [--reports <folder>] [--size "
    "<width>x<height>] "
    "[--fullscreen] [--no-vsync] [--tuning] [--explore] [--chibi] [--start <map>[,<exit>]] [--time day|dusk|night] [--lang "
    "<language>] "
    "[--text-font modern|original] [--ui-skin <id>] [--graphics <option>=<value>[,...]] "
    "[--new-game | --load <slot>] "
    "[--new-game-chain original|seeded] [--clock-speed <x>] "
    "[--seed <n>] [--original-rolls <state>] [--autotest <route.yaml> [--shots <folder>] [--skip-steps <kind>[,<kind>...]]] [--trace <file>] "
    "[--control <socket> [--control-paused]] | "
    "--list-saves | --check-content <folder> | --dump-signs <map>[,<map>...] --shots <folder> [--lang <language>] | --test-scene [--texture-churn] | --map <id>[,<id>...] "
    "[--focus <x>,<z>] | --ui-demo [--ui-demo-fishing] [--ui-demo-player] [--ui-demo-chart <layout.yaml>] [--ui-demo-chart-detail <layout.yaml>] [--ui-demo-chart-all-raised] [--ui-demo-bag-size <10|20|30>] "
    "[--virtual-gamepad <file>] | --model-viewer [character or species model id...] [--viewer-yaw <degrees>]";

inline constexpr std::string_view RulesetOption = "--ruleset";
inline constexpr std::string_view StoryStageOption = "--story-stage";
inline constexpr std::string_view NewGameChainOption = "--new-game-chain";

// A new game's chain by its name in --new-game-chain and in a route's new_game: original or
// seeded.
std::optional<session::NewGameChain> newGameChainNamed(std::string_view name);
inline constexpr std::string_view TuningOption = "--tuning";
inline constexpr std::string_view ExploreOption = "--explore";
inline constexpr std::string_view ChibiOption = "--chibi";
inline constexpr std::string_view LanguageOption = "--lang";
inline constexpr std::string_view SeedOption = "--seed";
inline constexpr std::string_view OriginalRollsOption = "--original-rolls";

// Reads the command line; logs what is wrong and gives nothing for a bad one.
std::optional<Options> parseOptions(std::span<char* const> arguments);

} // namespace dwe::app
