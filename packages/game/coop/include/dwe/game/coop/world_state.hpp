#pragma once

#include <dwe/game/coop/protocol.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::coop {

// Whether an edit is the shared world's (true) or one player's own (false): the story facts that
// belong to a player (its raised species, medals, key items, the freeze of its own scripts) stay
// in its game. Without a rule every edit is shared.
using WorldScope = std::function<bool(const WorldEdit&)>;

// What the host made of a guest's WorldAsk. Only Accepted changes the world.
enum class AskVerdict : std::uint8_t {
  Accepted,
  // No such guest in the session (it left, or never joined).
  UnknownPlayer,
  // Not newer than the newest ask taken from that guest (resent or reordered).
  Stale,
  // Map edits for a map the session is not on.
  WrongMap,
  // Nothing in it is the shared world's, or nothing it changes is new.
  Nothing,
};

[[nodiscard]] std::string_view askVerdictName(AskVerdict verdict);

// The host's record of the shared world's changes (#357). It holds no story or field: the app
// owns the values (its own game is the world) and says what each edit would change. The ledger
// numbers the changes, drops asks it took already, keeps each guest to the shared part and to the
// session's map, and says what to tell everyone.
class WorldLedger {
public:
  explicit WorldLedger(WorldScope shared = {}) : shared_{std::move(shared)} {}

  // The newest change's number; 0 before the first.
  [[nodiscard]] std::uint32_t revision() const { return revision_; }
  [[nodiscard]] bool shared(const WorldEdit& edit) const { return !shared_ || shared_(edit); }
  void setScope(WorldScope shared) { shared_ = std::move(shared); }

  // The host's own game changed the shared world (`edits`, on map `mapId`): the changes for every
  // guest (one per MaxWorldEdits edits), none when nothing in it is shared.
  [[nodiscard]] std::vector<WorldChange> record(std::string_view mapId,
                                                std::span<const WorldEdit> edits);

  struct Taken {
    AskVerdict verdict = AskVerdict::Nothing;
    // The change to apply on the host and send every guest (Accepted only).
    std::optional<WorldChange> change;
    // The edits left out as a player's own (its game keeps them; the world never takes them).
    std::vector<WorldEdit> refused;
  };
  // Guest `from`'s ask on a session whose map is `worldMap`. `known`: the guest is in the session.
  // `changes(edit)`: whether the host's world would change by it (the app's values); edits that
  // change nothing are dropped. The ask's map edits count only on the session's map.
  [[nodiscard]] Taken accept(PlayerId from, const WorldAsk& ask, std::string_view worldMap,
                             bool known, const std::function<bool(const WorldEdit&)>& changes);

  // A guest left: a new one with its id starts its asks from scratch.
  void leave(PlayerId id) { asks_.erase(id); }

private:
  WorldScope shared_;
  std::uint32_t revision_ = 0;
  std::map<PlayerId, std::uint32_t> asks_;
};

// A guest's side of the shared world: which of the host's changes are new, and its own asks'
// numbers. A change or sync not newer than the newest taken is dropped (resent, or already
// covered by a sync), so nothing is applied twice.
class WorldReplica {
public:
  [[nodiscard]] std::uint32_t revision() const { return revision_; }
  // Whether to apply this change: true once per revision, in order.
  [[nodiscard]] bool take(const WorldChange& change);
  // Whether to apply this sync part: any part of a sync at or past the newest revision taken.
  // Its last part makes its revision the newest.
  [[nodiscard]] bool take(const WorldSync& sync);
  // The next ask, with this guest's edits, split so each message stays under MaxWorldEdits.
  [[nodiscard]] std::vector<WorldAsk> ask(std::string_view mapId, std::span<const WorldEdit> edits);

private:
  std::uint32_t revision_ = 0;
  std::uint32_t asks_ = 0;
};

// The host's whole shared story (`triggers`, `variables` from 0) at `revision`, as the sync
// messages that carry it.
[[nodiscard]] std::vector<WorldSync> worldSyncParts(std::uint32_t revision,
                                                    const std::vector<bool>& triggers,
                                                    std::span<const std::int32_t> variables);

// Splits a change's edits so each message stays under MaxWorldEdits.
[[nodiscard]] std::vector<std::vector<WorldEdit>>
worldEditBatches(std::span<const WorldEdit> edits);

} // namespace dwe::game::coop
