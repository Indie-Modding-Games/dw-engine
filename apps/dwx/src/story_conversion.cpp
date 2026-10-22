#include "story_conversion.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/core/yaml.hpp>

#include <charconv>
#include <cstdio>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <print>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include "dwx.hpp"

namespace dwe::dwx {

namespace {

constexpr std::string_view MapHeadName = "maphead";

core::Result<void> convertFile(const std::filesystem::path& listing,
                               const std::filesystem::path& out) {
  std::ifstream file{listing, std::ios::binary};
  if (!file) {
    return core::fail(std::format("cannot read {}", assets::pathText(listing)));
  }
  std::ostringstream text;
  text << file.rdbuf();
  const auto parsed = core::parseYaml(text.str());
  if (!parsed) {
    return std::unexpected{parsed.error()};
  }
  const auto read = formats::script::readScriptListing(*parsed);
  if (!read) {
    return std::unexpected{read.error()};
  }
  const std::string stem = assets::pathText(listing.stem());
  std::optional<std::size_t> script;
  if (stem != MapHeadName) {
    std::size_t parsedId = 0;
    const auto [end, error] = std::from_chars(stem.data(), stem.data() + stem.size(), parsedId);
    if (error != std::errc{} || end != stem.data() + stem.size() || parsedId == 0 ||
        parsedId > std::numeric_limits<std::uint16_t>::max()) {
      return core::fail(
          std::format("{} is not script/maphead.yaml or script/dg/<n>.yaml with n from 1 to {}: "
                      "name it after its script",
                      assets::pathText(listing), std::numeric_limits<std::uint16_t>::max()));
    }
    script = parsedId;
  }
  const formats::script::LuaModuleName name = storyModuleName(script);
  const auto module = formats::script::convertScriptToLua(*read, name);
  if (!module) {
    return std::unexpected{module.error()};
  }
  const auto lines = core::emitYaml(formats::script::luaLinesToYaml(*module));
  const auto texts = core::emitYaml(formats::script::luaTextsToYaml(*module));
  if (!lines || !texts) {
    return core::fail(!lines ? lines.error().message : texts.error().message);
  }
  const std::string id = std::format("{:03}", name.script);
  for (const auto& [suffix, content] :
       {std::pair{std::string{".lua"}, std::string_view{module->code}},
        std::pair{std::string{"_lines.yaml"}, std::string_view{*lines}},
        std::pair{std::string{"_texts.yaml"}, std::string_view{*texts}}}) {
    if (auto written = writeFile(out / (id + suffix), std::as_bytes(std::span{content}));
        !written) {
      return written;
    }
  }
  return {};
}

} // namespace

formats::script::LuaModuleName storyModuleName(std::optional<std::size_t> script) {
  if (!script || *script == 0) {
    return {.script = 0, .source = "script/maphead", .textSource = "story_000"};
  }
  return {.script = static_cast<std::uint16_t>(*script),
          .source = std::format("script/dg/{:03}", *script),
          .textSource = std::format("story_{:03}", *script)};
}

int convertScript(const std::filesystem::path& listing, const std::filesystem::path& out) {
  if (auto converted = convertFile(listing, out); !converted) {
    std::println(stderr, "dwx convert-script: {}", converted.error().message);
    return ExitFailed;
  }
  return ExitOk;
}

} // namespace dwe::dwx
