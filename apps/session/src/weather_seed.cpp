#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/weather/climate.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/weather_seed.hpp>

#include <charconv>
#include <format>
#include <string>
#include <system_error>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view SeedKey = "seed";

} // namespace

class WeatherSeed::Serializer final : public game::save::SectionSerializer {
public:
  explicit Serializer(WeatherSeed& weather) : weather_(&weather) {}

  [[nodiscard]] core::Result<YamlNode> write() const override {
    YamlNode root = YamlNode::mapping();
    root.set(std::string{SeedKey}, YamlNode::scalar(std::format("{}", weather_->seed_)));
    return root;
  }

  core::Result<void> read(const YamlNode& state) override {
    YamlReader reader;
    std::string text;
    if (reader.expectKeys(state, SaveName, {SeedKey})) {
      text = reader.text(reader.field(state, SeedKey));
    }
    std::uint64_t seed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seed);
    if (reader.ok() && (text.empty() || error != std::errc{} || end != text.data() + text.size())) {
      reader.fail(state, std::format("{} is not a weather seed", text));
    }
    if (auto done = reader.finish(true, std::string{SaveName}); !done) {
      return std::unexpected{done.error()};
    }
    weather_->setSeed(seed);
    return {};
  }

  // A save from before the weather: the seed comes from the session's once it has loaded.
  core::Result<void> readMissing() override {
    weather_->missing_ = true;
    return {};
  }

private:
  WeatherSeed* weather_;
};

WeatherSeed::WeatherSeed() : serializer_(std::make_unique<Serializer>(*this)) {}

WeatherSeed::~WeatherSeed() = default;

core::Result<void> WeatherSeed::addSaveSections(game::save::SaveSections& sections) {
  return sections.add(std::string{SaveName}, *serializer_, game::save::MigrationChain{});
}

core::Result<void> WeatherSeed::startNewGame(GameSession& session) {
  setSeed(game::weather::weatherSeedOf(session.seed()));
  return {};
}

void WeatherSeed::afterLoad(GameSession& session) {
  if (missing_) {
    setSeed(game::weather::weatherSeedOf(session.seed()));
  }
}

} // namespace dwe::session
