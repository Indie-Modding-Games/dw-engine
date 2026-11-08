#include "options.hpp"

#include <dwe/content/dev_assets.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/number_text.hpp>
#include <dwe/session/slot_names.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <memory>
#include <ranges>
#include <system_error>

#include "graphics_options.hpp"
#include "messages.hpp"

namespace dwe::app {
namespace {

// Reads two numbers separated by `separator`, such as 1280x720.
std::optional<std::array<int, 2>> parsePair(std::string_view text, char separator) {
  std::array<int, 2> values{};
  const char* begin = std::to_address(text.begin());
  const char* end = std::to_address(text.end());
  auto first = std::from_chars(begin, end, values[0]);
  if (first.ec != std::errc{} || first.ptr == end || *first.ptr != separator) {
    return std::nullopt;
  }
  auto second = std::from_chars(first.ptr + 1, end, values[1]);
  if (second.ec != std::errc{} || second.ptr != end) {
    return std::nullopt;
  }
  return values;
}

std::optional<render::TimeOfDay> parseTime(std::string_view text) {
  if (text == "day") {
    return render::TimeOfDay::Day;
  }
  if (text == "dusk") {
    return render::TimeOfDay::Dusk;
  }
  if (text == "night") {
    return render::TimeOfDay::Night;
  }
  return std::nullopt;
}

bool readStart(Options& options, std::string_view value) {
  const std::size_t comma = value.find(',');
  const std::string_view map = value.substr(0, comma);
  if (map.empty()) {
    return false;
  }
  options.startMap = std::string{map};
  if (comma == std::string_view::npos) {
    return true;
  }
  const std::string_view exit = value.substr(comma + 1);
  const auto parsed = std::from_chars(exit.data(), exit.data() + exit.size(), options.startExit);
  return parsed.ec == std::errc{} && parsed.ptr == exit.data() + exit.size() &&
         options.startExit >= 0;
}

} // namespace

std::optional<session::NewGameChain> newGameChainNamed(std::string_view name) {
  if (name == "original") {
    return session::NewGameChain::Original;
  }
  if (name == "seeded") {
    return session::NewGameChain::Seeded;
  }
  return std::nullopt;
}

namespace {

// Reads one option that takes a value; false if the value is not valid for it or the option is
// unknown.
bool readOption(Options& options, std::string_view argument, std::string_view value) {
  if (argument == "--content" || argument == "--check-content") {
    options.content = value;
    options.contentGiven = true;
    options.checkContent = options.checkContent || argument == "--check-content";
    return true;
  }
  if (argument == "--saves") {
    options.saves = std::filesystem::path{value};
    return true;
  }
  if (argument == "--reports") {
    options.reports = std::filesystem::path{value};
    return true;
  }
  if (argument == "--versus-server") {
    options.versusServer = value;
    return value.starts_with("ws://") || value.starts_with("wss://");
  }
  if (argument == "--mods") {
    options.mods.emplace_back(value);
    return true;
  }
  if (argument == RulesetOption) {
    options.ruleset = value;
    return !value.empty();
  }
  if (argument == StoryStageOption) {
    options.storyStage = value;
    return !value.empty();
  }
  if (argument == NewGameChainOption) {
    options.newGameChain = newGameChainNamed(value);
    return options.newGameChain.has_value();
  }
  if (argument == "--autotest") {
    options.autotest = value;
    return true;
  }
  if (argument == "--trace") {
    options.trace = value;
    return true;
  }
  if (argument == "--control") {
    options.control = std::string{value};
    return !value.empty();
  }
  if (argument == "--viewer-yaw") {
    float degrees = 0.0F;
    const auto [end, error] = core::fromChars(value.data(), value.data() + value.size(), degrees);
    options.viewerYaw = degrees;
    return error == std::errc{} && end == value.data() + value.size();
  }
  if (argument == "--viewer-frame") {
    float height = 0.0F;
    const auto [end, error] = core::fromChars(value.data(), value.data() + value.size(), height);
    options.viewerFrame = height;
    return error == std::errc{} && end == value.data() + value.size() && height > 0.0F;
  }
  if (argument == "--shots") {
    options.shots = value;
    return true;
  }
  if (argument == "--skip-steps") {
    options.skipSteps.clear();
    for (const auto part : std::views::split(value, ',')) {
      options.skipSteps.emplace_back(std::string_view{part});
    }
    return std::ranges::none_of(options.skipSteps, &std::string::empty);
  }
  if (argument == "--start") {
    return readStart(options, value);
  }
  if (argument == LanguageOption) {
    options.language = std::string{value};
    return !value.empty();
  }
  if (argument == "--virtual-gamepad") {
    options.virtualGamepad = value;
    return true;
  }
  if (argument == "--ui-demo-bag-size") {
    if (value != "10" && value != "20" && value != "30")
      return false;
    options.uiDemo = true;
    options.uiDemoBagSlots = value == "10" ? 10 : value == "20" ? 20 : 30;
    return true;
  }
  if (argument == "--ui-demo-chart-detail") {
    options.uiDemo = true;
    options.uiDemoPlayer = true;
    options.uiDemoChartDetail = value;
    return true;
  }
  if (argument == "--ui-demo-chart") {
    options.uiDemo = true;
    options.uiDemoPlayer = true;
    options.uiDemoChart = value;
    return true;
  }
  if (argument == "--dump-signs") {
    options.dumpSigns.clear();
    for (const auto part : std::views::split(value, ',')) {
      options.dumpSigns.emplace_back(std::string_view{part});
    }
    return std::ranges::none_of(options.dumpSigns, &std::string::empty);
  }
  if (argument == "--map") {
    options.maps.clear();
    for (const auto part : std::views::split(value, ',')) {
      options.maps.emplace_back(std::string_view{part});
    }
    return std::ranges::none_of(options.maps, &std::string::empty);
  }
  if (argument == "--size") {
    const auto size = parsePair(value, 'x');
    if (!size || (*size)[0] <= 0 || (*size)[1] <= 0) {
      return false;
    }
    options.width = (*size)[0];
    options.height = (*size)[1];
    return true;
  }
  if (argument == "--load") {
    options.load = session::slotFromName(value);
    return options.load.has_value();
  }
  if (argument == "--clock-speed") {
    double speed = 0.0;
    const auto parsed = core::fromChars(value.data(), value.data() + value.size(), speed);
    options.clockSpeed = speed;
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && speed >= 0.0;
  }
  if (argument == SeedOption) {
    std::uint64_t seed = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seed);
    options.seed = seed;
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
  }
  if (argument == OriginalRollsOption) {
    const bool hex = value.starts_with("0x") || value.starts_with("0X");
    const std::string_view digits = hex ? value.substr(2) : value;
    std::uint32_t state = 0;
    const auto parsed =
        std::from_chars(digits.data(), digits.data() + digits.size(), state, hex ? 16 : 10);
    options.originalRolls = state;
    return !digits.empty() && parsed.ec == std::errc{} &&
           parsed.ptr == digits.data() + digits.size();
  }
  if (argument == "--time") {
    const auto time = parseTime(value);
    options.time = time;
    return time.has_value();
  }
  if (argument == "--weather") {
    options.weather = game::weather::weatherKindNamed(value);
    return options.weather.has_value();
  }
  if (argument == "--season") {
    options.seasonsOff = value == "none";
    options.season = game::weather::seasonNamed(value);
    return options.seasonsOff || options.season.has_value();
  }
  if (argument == "--text-font") {
    options.textFont = ui::textFontNamed(value);
    return options.textFont.has_value();
  }
  if (argument == "--graphics") {
    options.graphics = std::string{value};
    return withGraphicsFlag({}, value).has_value();
  }
  if (argument == "--ui-skin") {
    options.uiSkin = std::string{value};
    return !value.empty();
  }
  if (argument == "--focus") {
    const auto focus = parsePair(value, ',');
    if (!focus) {
      return false;
    }
    options.focus = glm::vec2{static_cast<float>((*focus)[0]), static_cast<float>((*focus)[1])};
    return true;
  }
  return false;
}

// Reads one option without a value; false if it is not one.
bool readFlag(Options& options, std::string_view argument) {
  if (argument == "--texture-churn") {
    options.textureChurn = true;
  } else if (argument == "--test-scene") {
    options.testScene = true;
  } else if (argument == "--ui-demo") {
    options.uiDemo = true;
  } else if (argument == "--ui-demo-fishing") {
    options.uiDemo = true;
    options.uiDemoFishing = true;
  } else if (argument == "--ui-demo-chart-all-raised") {
    options.uiDemo = true;
    options.uiDemoPlayer = true;
    options.uiDemoChartAllRaised = true;
  } else if (argument == "--ui-demo-player") {
    options.uiDemo = true;
    options.uiDemoPlayer = true;
  } else if (argument == "--fullscreen") {
    options.fullscreen = true;
  } else if (argument == "--no-vsync") {
    options.vsync = false;
  } else if (argument == "--control-paused") {
    options.controlPaused = true;
  } else if (argument == "--new-game") {
    options.newGame = true;
  } else if (argument == "--versus-loopback") {
    options.versusLoopback = true;
  } else if (argument == "--list-saves") {
    options.listSaves = true;
  } else if (argument == CheckSystemOption) {
    options.checkSystem = true;
  } else if (argument == TuningOption) {
    options.tuning = true;
  } else if (argument == ExploreOption) {
    options.explore = true;
  } else if (argument == ChibiOption) {
    options.chibi = true;
  } else if (argument == content::DevAssetsOption) {
    options.devAssets = true;
  } else {
    return false;
  }
  return true;
}

} // namespace

std::optional<Options> parseOptions(std::span<char* const> arguments) {
  Options options;
  std::size_t index = 1;
  while (index < arguments.size()) {
    const std::string_view argument = arguments[index];
    if (argument == "--model-viewer") {
      options.modelViewer = true;
      ++index;
      while (index < arguments.size() && !std::string_view{arguments[index]}.starts_with("--")) {
        options.characters.emplace_back(arguments[index]);
        ++index;
      }
      continue;
    }
    if (readFlag(options, argument)) {
      ++index;
      continue;
    }
    if (index + 1 >= arguments.size() || !readOption(options, argument, arguments[index + 1])) {
      core::logError("bad argument '{}'; usage: {}", argument, Usage);
      return std::nullopt;
    }
    index += 2;
  }
  if (options.modelViewer && options.characters.empty()) {
    options.characters = {"models/tamer", "models/partner"};
  }
  if (options.newGame && options.load) {
    core::logError("{}", messages::NewGameOrLoad);
    return std::nullopt;
  }
  // The texture churn is a check of the test scene.
  options.testScene = options.testScene || options.textureChurn;
  return options;
}

} // namespace dwe::app
