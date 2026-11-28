#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/game/coop/world_state.hpp>
#include <dwe/game/field/field_items.hpp>
#include <dwe/game/partner/poop_piles.hpp>
#include <dwe/game/story/story_state.hpp>
#include <dwe/ruleset/random.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "field_item_claim.hpp"
#include "field_pile_share.hpp"

namespace dwe::app {

// One game's shared world as co-op reads and writes it (#357, multiplayer.md "Shared world
// interactions"): its story, its ground items and chests, and the live map edits its scripts made
// in the last tick. Built by PlaySession::takeCoopWorld after each tick.
struct CoopWorldView {
  game::story::StoryState* story = nullptr;
  game::field::FieldItems* items = nullptr;
  // Names the ground items on the wire.
  const assets::ItemTable* itemTable = nullptr;
  std::string mapId;
  // Grows on every map load (FieldScene::mapSerial).
  std::uint32_t mapSerial = 0;
  // The field stands on its map: no change fading out or waiting for its map.
  bool settled = false;
  // The doors and mechanisms this game's scripts moved in the last tick (MapObject, MapBlock).
  std::vector<game::coop::WorldEdit> mapEdits;
  // What is shared and what is one player's own (StoryAdapter::playerOwnTrigger and Variable).
  game::coop::WorldScope shared;
  // A guest's item notice's take waiting for the host (FieldItemsScreen::claim); none on the host.
  FieldItemClaim* claim = nullptr;
  // The partners' poop piles of every map, and the dice that move a guest's pile a step aside on
  // the host when its tile is taken.
  game::partner::PoopPiles* piles = nullptr;
  ruleset::Random* random = nullptr;
  // A guest's piles its partner left and its partner's eat waiting for the host
  // (FieldPoop::pileShare); none on the host.
  FieldPileShare* pileShare = nullptr;
};

// A shared fact changed in this game, by another player or by its own scripts, with the value it
// had: what the map head of the map this game stands on decided with (CoopNpcPresence).
struct CoopFactChange {
  game::coop::WorldEdit edit;
  std::int32_t previous = 0;
};

// The story facts as this game and the session last agreed them: what the game changed since is
// its news. Applying the session's values moves the baseline too, so they are never sent back.
class CoopStoryBaseline {
public:
  [[nodiscard]] bool ready() const { return ready_; }
  void reset(const game::story::StoryState& state);
  // The shared facts that differ from the baseline, as edits; the baseline takes them. With
  // `changed`, each is noted there with the value it had.
  [[nodiscard]] std::vector<game::coop::WorldEdit>
  changes(const game::story::StoryState& state, const game::coop::WorldScope& shared,
          std::vector<CoopFactChange>* changed = nullptr);
  // Writes a fact edit into the state and the baseline; whether the state changed. Map and ground
  // edits, and facts the state has no room for, change nothing.
  bool apply(game::story::StoryState& state, const game::coop::WorldEdit& edit);
  // apply, noting the change in `changed` when it made one.
  bool apply(game::story::StoryState& state, const game::coop::WorldEdit& edit,
             std::vector<CoopFactChange>& changed);
  // Whether the state holds a different value for a fact edit (false for anything else).
  [[nodiscard]] static bool differs(const game::story::StoryState& state,
                                    const game::coop::WorldEdit& edit);
  // The state's value of a fact edit's key, as an edit.
  [[nodiscard]] static std::optional<game::coop::WorldEdit>
  current(const game::story::StoryState& state, const game::coop::WorldEdit& edit);

private:
  bool ready_ = false;
  std::vector<bool> triggers_;
  std::vector<std::int32_t> variables_;
};

// The whole shared story of a state, as sync messages at `revision`.
[[nodiscard]] std::vector<game::coop::WorldSync> storySync(std::uint32_t revision,
                                                           const game::story::StoryState& state);

// Applies a sync part's shared values to the state and the baseline, noting what changed.
void applyStorySync(game::story::StoryState& state, CoopStoryBaseline& baseline,
                    const game::coop::WorldSync& sync, const game::coop::WorldScope& shared,
                    std::vector<CoopFactChange>& changed);

// A chest whose trigger is set shows taken, wherever its trigger came from (another player took it,
// #357). Returns how many changed.
std::size_t followChestTriggers(game::field::FieldItems& items,
                                const game::story::StoryState& state);

// The host's ground as the guests get it: an id for each item from when it comes to lie there, and
// the list when anything on it changed (or the map did).
class CoopGroundTracker {
public:
  [[nodiscard]] std::optional<game::coop::GroundItems> update(const CoopWorldView& view);
  // The list as last sent, for a guest that needs it again.
  [[nodiscard]] const std::optional<game::coop::GroundItems>& last() const { return last_; }
  // The id the item in `slot` has now (none for an empty slot).
  [[nodiscard]] std::optional<std::uint32_t> idAt(std::size_t slot) const;

private:
  std::optional<game::coop::GroundItems> last_;
  std::uint32_t nextId_ = 1;
};

// A guest's ground on the host's map: the host's list. A pick-up there is claimed by the item's id
// (ItemClaim) and leaves the ground once the host's list no longer holds it.
class CoopGroundMirror {
public:
  void receive(game::coop::GroundItems ground) { host_ = std::move(ground); }
  // The host's id of the item show() laid in `slot` on this map load; none for an item of this
  // game's own ground (off the host's map), which no one else can take.
  [[nodiscard]] std::optional<std::uint32_t> idAt(const CoopWorldView& view,
                                                  std::size_t slot) const;
  // Lays the host's list on this game's ground while it stands on the host's map.
  void show(CoopWorldView& view);

private:
  std::optional<game::coop::GroundItems> host_;
  // What show() laid, by slot (0 for none), and on which map load.
  std::vector<std::uint32_t> shown_;
  std::optional<std::uint32_t> shownSerial_;
};

// The piles of a map as the wire names them.
[[nodiscard]] game::coop::PoopPiles pileList(const game::partner::PoopPiles& piles,
                                             std::string_view map);

// Lays the host's list on this game's piles: that map's become the host's, and with `restart`
// every other map's go first.
void applyPileList(game::partner::PoopPiles& piles, const game::coop::PoopPiles& list);

// The host's piles as the guests last got them, by map: a map's list goes to every guest when its
// piles change.
class CoopPileTracker {
public:
  // The map's list when it differs from the one last sent.
  [[nodiscard]] std::optional<game::coop::PoopPiles> update(const game::partner::PoopPiles& piles,
                                                            std::string_view map);
  // Every map's piles for a guest that joins or follows the host to a new map: the host's map's
  // list first, restarting the guest's piles, then each other map that has some.
  [[nodiscard]] static std::vector<game::coop::PoopPiles>
  sync(const game::partner::PoopPiles& piles, std::string_view hostMap);

private:
  std::map<std::string, std::vector<game::coop::PileEntry>, std::less<>> sent_;
};

} // namespace dwe::app
