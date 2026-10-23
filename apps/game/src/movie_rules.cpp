#include "movie_rules.hpp"

#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>

#include <array>
#include <initializer_list>
#include <utility>

namespace dwe::app {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::array<std::string_view, MovieMomentCount> MomentKeys{"boot", "new_game",
                                                                    "cleared_load", "ending"};

MovieRule readMovieRule(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a movie", {"file", "skippable"})) {
    return {};
  }
  return MovieRule{.file = reader.text(reader.field(node, "file")),
                   .skippable = reader.boolean(reader.field(node, "skippable"))};
}

} // namespace

core::Result<MovieRules> readMovieRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, "the movies section", {"movies", "landing_idle_frames"})) {
    return std::unexpected{reader.error(section.file)};
  }
  const YamlNode& movies = reader.field(root, "movies");
  if (!reader.expectKeys(movies, "the movies", {"boot", "new_game", "cleared_load", "ending"})) {
    return std::unexpected{reader.error(section.file)};
  }
  MovieRules rules;
  for (std::size_t moment = 0; moment < MovieMomentCount; ++moment) {
    rules.movies[moment] = readMovieRule(reader, reader.field(movies, MomentKeys[moment]));
  }
  rules.landingIdleFrames =
      reader.integer<std::uint32_t>(reader.field(root, "landing_idle_frames"));
  return reader.finish(std::move(rules), section.file);
}

std::optional<MovieRules> loadMovieRules(const assets::VirtualFileSystem& files,
                                         std::string_view rulesetId) {
  auto rules = ruleset::loadRuleset(files, rulesetId);
  if (!rules) {
    core::logWarning("movies: no ruleset: {}", rules.error().message);
    return std::nullopt;
  }
  auto section = rules->requireSection(MoviesSection);
  if (!section) {
    core::logWarning("movies: {}", section.error().message);
    return std::nullopt;
  }
  auto movies = readMovieRules(**section);
  if (!movies) {
    core::logWarning("movies: {}", movies.error().message);
    return std::nullopt;
  }
  return std::move(*movies);
}

} // namespace dwe::app
