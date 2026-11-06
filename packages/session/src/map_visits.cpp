#include <dwe/assets/yaml_reader.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/map_visits.hpp>

#include <string>
#include <utility>
#include <vector>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view MapsKey = "maps";

} // namespace

class MapVisits::Serializer final : public game::save::SectionSerializer {
public:
  explicit Serializer(MapVisits& visits) : visits_(&visits) {}

  [[nodiscard]] core::Result<YamlNode> write() const override {
    YamlNode maps = YamlNode::sequence();
    for (const std::string& map : visits_->maps_) {
      maps.append(YamlNode::scalar(map, YamlNode::ScalarStyle::Quoted));
    }
    YamlNode root = YamlNode::mapping();
    root.set(std::string{MapsKey}, std::move(maps));
    return root;
  }

  core::Result<void> read(const YamlNode& state) override {
    YamlReader reader;
    std::vector<std::string> maps;
    if (reader.expectKeys(state, SaveName, {MapsKey})) {
      for (const YamlNode& map : reader.sequence(reader.field(state, MapsKey), MapsKey)) {
        maps.push_back(reader.text(map));
      }
    }
    auto read = reader.finish(std::move(maps), std::string{SaveName});
    if (!read) {
      return std::unexpected{read.error()};
    }
    visits_->maps_.clear();
    for (std::string& map : *read) {
      visits_->visit(map);
    }
    return {};
  }

  // A save from before the island map: nothing is known of where the player has been.
  core::Result<void> readMissing() override {
    visits_->maps_.clear();
    return {};
  }

private:
  MapVisits* visits_;
};

MapVisits::MapVisits() : serializer_(std::make_unique<Serializer>(*this)) {}

MapVisits::~MapVisits() = default;

core::Result<void> MapVisits::addSaveSections(game::save::SaveSections& sections) {
  return sections.add(std::string{SaveName}, *serializer_, game::save::MigrationChain{});
}

core::Result<void> MapVisits::startNewGame(GameSession& session) {
  maps_.clear();
  follow(session);
  return {};
}

void MapVisits::tick(GameSession& session, const ruleset::ClockAdvance& /*advance*/) {
  follow(session);
}

void MapVisits::afterLoad(GameSession& session) {
  follow(session);
}

void MapVisits::visit(std::string_view map) {
  if (!map.empty() && !maps_.contains(map)) {
    maps_.emplace(map);
  }
}

bool MapVisits::visited(std::string_view map) const {
  return maps_.contains(map);
}

void MapVisits::follow(const GameSession& session) {
  if (const auto place = session.currentPlace()) {
    visit(place->map);
  }
}

} // namespace dwe::session
