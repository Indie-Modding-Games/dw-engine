#pragma once

#include <dwe/session/session_system.hpp>

#include <memory>
#include <set>
#include <string>
#include <string_view>

namespace dwe::session {

// The maps the player has stood on, for the island map (a dwe improvement: the original has no
// map of the island and keeps no such list). It follows the player's map on every tick and keeps
// the set in its own save section, `map_visits`; a save from before it starts with none, so only
// the maps visited from then on count.
class MapVisits final : public SessionSystem {
public:
  // The save section's name.
  static constexpr std::string_view SaveName = "map_visits";

  MapVisits();
  ~MapVisits() override;
  MapVisits(const MapVisits&) = delete;
  MapVisits& operator=(const MapVisits&) = delete;
  MapVisits(MapVisits&&) = delete;
  MapVisits& operator=(MapVisits&&) = delete;

  core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
  core::Result<void> startNewGame(GameSession& session) override;
  void tick(GameSession& session, const ruleset::ClockAdvance& advance) override;
  void afterLoad(GameSession& session) override;

  // Counts a map as visited; an empty id is ignored.
  void visit(std::string_view map);
  [[nodiscard]] bool visited(std::string_view map) const;
  [[nodiscard]] const std::set<std::string, std::less<>>& maps() const { return maps_; }

private:
  class Serializer;

  void follow(const GameSession& session);

  std::set<std::string, std::less<>> maps_;
  std::unique_ptr<Serializer> serializer_;
};

} // namespace dwe::session
