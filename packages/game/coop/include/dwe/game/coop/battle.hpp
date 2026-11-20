#pragma once

#include <dwe/game/coop/protocol.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::coop {

// What a player is in the running battle: the one whose partner fights (it started it and gives
// the commands) or the one who watches and helps.
enum class BattleRole : std::uint8_t { Starter, Helper };

// Whether a battle action is one a player in this role may take: the commands and the end box's
// close belong to the starter; an item from one's own bag and a cheer to either.
[[nodiscard]] bool roleAllows(BattleRole role, BattleActionKind kind);

// The host's side of the co-op battles (docs/architecture/multiplayer.md, §6 decision 3): the
// shared world runs one battle at a time, on the host's simulation. It numbers each battle, knows
// who started the running one and judges the guest's asks and actions. It holds no game state:
// the app starts and runs the battle and tells it what happened.
class BattleReferee {
public:
  struct Running {
    std::uint32_t battle = 0;
    PlayerId starter = HostPlayerId;
    std::uint32_t ask = 0;
  };

  // Whether the battle the guest asks for can start: not while another one runs (the guest is
  // pulled into that one as its helper: the second battle never starts), nor on another map than
  // the host's. None: it can.
  [[nodiscard]] std::optional<BattleRefusal> judge(const BattleAsk& ask, std::string_view hostMap,
                                                   bool hostBusy) const;
  // A battle began in the host's world: the host's own (`ask` 0) or the guest's ask it accepted.
  // The message that tells the guest.
  BattleBegin begin(PlayerId starter, std::uint32_t ask, std::string mapId,
                    std::vector<std::int16_t> enemyNpcs, std::vector<std::string> species,
                    bool fixedFight);
  [[nodiscard]] const std::optional<Running>& running() const { return running_; }
  // The role of a player in the running battle; none without one.
  [[nodiscard]] std::optional<BattleRole> roleOf(PlayerId player) const;
  // Whether the host applies this action of the guest: one for the running battle, of a kind its
  // role allows, while the guest is there.
  [[nodiscard]] bool allows(const BattleAction& action) const;
  // The running battle ended: the message that tells the guest (none without a running battle).
  // Spoils go only to a guest that started it and is still there.
  std::optional<BattleEnd> end(BattleOutcome outcome, std::vector<std::int16_t> leftMap,
                               std::int32_t bits, std::optional<BattleSpoils> spoils);
  // The guest dropped. A battle its partner fights goes on alone: the partner fights on its own
  // call (it takes no more commands) and nothing is sent back; the host keeps watching and helping.
  void guestLeft() { guestGone_ = true; }
  // The guest is back (a new link): it sees the next battle again.
  void guestJoined() { guestGone_ = false; }
  [[nodiscard]] bool guestGone() const { return guestGone_; }
  // Whether the fighting partner takes commands from its starter: not when that is a guest who
  // dropped.
  [[nodiscard]] bool starterPresent() const;

private:
  std::uint32_t next_ = 1;
  std::optional<Running> running_;
  bool guestGone_ = false;
};

// The guest's side: its asks, the battle the host runs and the newest frame of it. It holds no
// game state either; the app shows the frames and gives the outcome to the guest's own script.
class BattleMirror {
public:
  // An ask for the battle the guest's map script started, to send to the host; the guest waits
  // for the answer (a begin or a refusal).
  BattleAsk ask(std::string mapId, std::vector<std::int16_t> enemyNpcs, bool fixedFight,
                bool predefined, BattleFighterProfile fighter);
  [[nodiscard]] std::optional<std::uint32_t> pendingAsk() const { return pending_; }

  // The host's battle messages, in the order they arrive.
  void receive(const BattleBegin& begin);
  void receive(const BattleRefused& refused);
  void receive(const BattleFrame& frame);
  void receive(const BattleEnd& end);

  // Once each: the battle that began, the guest's ask the host refused, the battle that ended.
  [[nodiscard]] std::optional<BattleBegin> takeBegan();
  [[nodiscard]] std::optional<BattleRefused> takeRefused();
  [[nodiscard]] std::optional<BattleEnd> takeEnded();

  // The running battle, its newest frame and this player's role in it.
  [[nodiscard]] const std::optional<BattleBegin>& running() const { return running_; }
  [[nodiscard]] const std::optional<BattleFrame>& frame() const { return frame_; }
  [[nodiscard]] std::optional<BattleRole> role() const;
  // An action to send for the running battle; none without one or when the role does not allow
  // it.
  [[nodiscard]] std::optional<BattleAction> action(BattleActionKind kind, std::int32_t value = 0,
                                                   std::string item = {}) const;

  // The host is gone: the battle and the ask with it. What was running or pending, so the app can
  // let its own script and screen go.
  struct Dropped {
    std::optional<std::uint32_t> ask;
    bool battle = false;
  };
  Dropped hostLeft();

private:
  std::uint32_t nextAsk_ = 1;
  std::optional<std::uint32_t> pending_;
  std::optional<BattleBegin> running_;
  std::optional<BattleFrame> frame_;
  std::optional<BattleBegin> began_;
  std::optional<BattleRefused> refused_;
  std::optional<BattleEnd> ended_;
};

} // namespace dwe::game::coop
