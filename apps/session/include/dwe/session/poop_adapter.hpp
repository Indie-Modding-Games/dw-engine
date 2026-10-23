#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/poop_piles.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/session/session_system.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace dwe::session {

// The ruleset section of the piles' drawing.
inline constexpr std::string_view PoopPilesSection = "poop_piles";

// How the field draws a pile (partner_impl.c: initializePoop, renderPoop).
struct PoopPileLook {
  // The pile's model.
  assets::AssetId model;
  // A pile of this size is drawn at the model's own scale; the others in proportion.
  std::int32_t modelSize = 0;
};

// The `poop_piles` rules.
struct PoopPileRules {
  PoopPileLook look;
};

// Reads the `poop_piles` section; errors name the line.
core::Result<PoopPileRules> readPoopPileRules(const ruleset::Section& section);

// A pile the partner left that the field has not placed yet: its size, and whether the partner
// went on the field (partner.c: tickWildPoop) or on waking (partner_impl.c: handleSleeping).
struct PendingPile {
  std::int32_t size = 0;
  bool wild = true;
};

// The partner's poop piles around the world (partner_impl.c: WORLD_POOP), with the care rules'
// limits, and the `poop_piles` save section. The partner says it left a pile (PoopedOnGround);
// the field, which knows where the partner stands, places it. Without the `poop_piles` rules the
// piles are kept but not drawn.
class PoopAdapter final : public SessionSystem {
public:
  PoopAdapter();
  ~PoopAdapter() override;
  PoopAdapter(const PoopAdapter&) = delete;
  PoopAdapter& operator=(const PoopAdapter&) = delete;
  PoopAdapter(PoopAdapter&&) = delete;
  PoopAdapter& operator=(PoopAdapter&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
  core::Result<void> startNewGame(GameSession& session) override;
  void routePartnerEvent(GameSession& session, const game::partner::PartnerEvent& event) override;

  [[nodiscard]] const std::optional<PoopPileRules>& rules() const { return rules_; }

  // The oldest pile not placed yet, if any.
  [[nodiscard]] std::optional<PendingPile> nextPending() const;
  // Places the oldest pending pile in front of the partner standing on `tile` and facing
  // `rotation` (the original's units; partner_impl.c: createPoopPile). Returns where it went.
  std::optional<game::partner::Tile> placePending(GameSession& session, std::string_view map,
                                                  game::partner::Tile tile, std::int32_t rotation);

  // The piles of a map.
  [[nodiscard]] std::vector<game::partner::PoopPile> onMap(std::string_view map) const;
  // Takes a pile off a tile (eaten); its size, if there was one.
  std::optional<std::int32_t> remove(std::string_view map, game::partner::Tile tile);

private:
  class Serializer;

  std::optional<PoopPileRules> rules_;
  std::optional<game::partner::PoopPiles> piles_;
  std::deque<PendingPile> pending_;
  std::unique_ptr<Serializer> serializer_;
};

} // namespace dwe::session
