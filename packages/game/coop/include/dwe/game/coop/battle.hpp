#pragma once

#include <dwe/game/coop/protocol.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::game::coop {

// What a player is in the running battle: the one whose partner fights first (it started it), one
// whose partner joined it beside that one, or one who watches and helps. Each player with a partner
// in it commands only that partner.
enum class BattleRole : std::uint8_t { Starter, Helper, Ally };

// Whether a battle action is one a player in this role may take: the commands belong to who has a
// partner in it, the end box's close to the starter; an item from one's own bag and a cheer to
// anyone.
[[nodiscard]] bool roleAllows(BattleRole role, BattleActionKind kind);

[[nodiscard]] bool spoilsInRange(const BattleSpoils& spoils);

// In game ticks (60 a second).
inline constexpr std::uint64_t BattleAskWaitTicks = 600;

// The host's game when it judges a guest's ask; `map` is empty during a map change.
struct HostMoment {
  std::string_view map;
  bool guestHere = false;
  bool battling = false;
  bool held = false;
};

struct BattleAskVerdict {
  bool waits = false;
  std::optional<BattleRefusal> refusal;

  [[nodiscard]] bool starts() const { return !waits && !refusal; }
};

// The host's side of the co-op battles (docs/architecture/multiplayer.md, §6 decision 3;
// coop-combat.md): the shared world runs one battle at a time, on the host's simulation. It numbers
// each battle, knows who started the running one and whose partners fight in it, keeps each
// guest's newest offer and judges the guests' asks and actions. It holds no game state: the app
// starts and runs the battle and tells it what happened.
class BattleReferee {
public:
  struct Running {
    std::uint32_t battle = 0;
    PlayerId starter = HostPlayerId;
    std::uint32_t ask = 0;
    std::vector<PlayerId> allies;
  };
  // A message for one guest.
  struct Addressed {
    PlayerId to = GuestPlayerId;
    BattleEnd end;
  };

  // Busy while a battle runs, NotHere from another map; otherwise it waits until the host is free.
  [[nodiscard]] BattleAskVerdict judge(const BattleAsk& ask, const HostMoment& host) const;
  // False once the ask waited BattleAskWaitTicks: the host then refuses it as busy.
  bool wait(PlayerId guest, const BattleAsk& ask, std::uint64_t tick);
  struct WaitingAsk {
    PlayerId guest = GuestPlayerId;
    BattleAsk ask;
    std::uint64_t since = 0;
  };
  [[nodiscard]] const std::vector<WaitingAsk>& waiting() const { return waiting_; }
  void forget(PlayerId guest);
  // A battle began in the host's world: the host's own (`ask` 0) or a guest's ask it accepted, with
  // the partners of `allies` beside the starter's. The message that tells every guest.
  BattleBegin begin(PlayerId starter, std::uint32_t ask, std::string mapId,
                    std::vector<std::int16_t> enemyNpcs, std::vector<std::string> species,
                    bool fixedFight, std::vector<PlayerId> allies = {}, bool folder = false);
  // Guest `guest`'s newest offer of its partner for the host's battles.
  void receive(PlayerId guest, const BattleOffer& offer);
  // The partner `guest` offers now: none when it is not free to join or not there.
  [[nodiscard]] std::optional<BattleFighterProfile> offered(PlayerId guest) const;
  // Whether the partner of any of `guests` there dies or waits to be reborn (their offers say so).
  [[nodiscard]] bool partnerDying(std::span<const PlayerId> guests) const;
  [[nodiscard]] const std::optional<Running>& running() const { return running_; }
  // The role of a player in the running battle; none without one.
  [[nodiscard]] std::optional<BattleRole> roleOf(PlayerId player) const;
  // Whether the host applies this action of guest `from`: one for the running battle, of a kind its
  // role allows, while that guest is there.
  [[nodiscard]] bool allows(PlayerId from, const BattleAction& action) const;
  // The running battle ended: the message for each guest there (none without a running battle).
  // Each gets its own spoils from `spoils` only when its partner fought in it.
  std::vector<Addressed> end(BattleOutcome outcome, std::vector<std::int16_t> leftMap,
                             std::int32_t bits,
                             std::span<const std::pair<PlayerId, BattleSpoils>> spoils);
  // A guest's link is up: it sees the battles from now on.
  void guestJoined(PlayerId guest);
  // A guest dropped. A battle its partner fights goes on: the partner fights on its own call (it
  // takes no more commands) and nothing is sent back; the others keep playing.
  void guestLeft(PlayerId guest);
  [[nodiscard]] bool present(PlayerId guest) const;
  // The guests there, in id order.
  [[nodiscard]] std::vector<PlayerId> guests() const;
  // Whether the fighting partner takes commands from its starter: not when that is a guest who
  // dropped.
  [[nodiscard]] bool starterPresent() const;

private:
  std::uint32_t next_ = 1;
  std::optional<Running> running_;
  std::vector<WaitingAsk> waiting_;
  std::array<std::optional<BattleOffer>, MaxPlayers> offers_{};
  std::array<bool, MaxPlayers> present_{};
};

// The guest's side: its asks, the battle the host runs and the newest frame of it. It holds no
// game state either; the app shows the frames and gives the outcome to the guest's own script.
class BattleMirror {
public:
  // The mirror of player `self` (the id the host's welcome gave this guest).
  explicit BattleMirror(PlayerId self = GuestPlayerId) : self_{self} {}
  [[nodiscard]] PlayerId self() const { return self_; }

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

  // Whether this guest's partner is free to join the host's battles: never while one of them runs,
  // and while its own ask waits for the host even though its game waits on that battle, so an
  // encounter the host starts meanwhile takes it as an ally, not a helper.
  [[nodiscard]] bool partnerFree(bool gameFree) const {
    return !running_ && (gameFree || pending_.has_value());
  }
  // The offer of this guest's partner for the host's battles to send, when it changed since the
  // last one sent (`available` with its fighter, and whether its partner dies); none when it did
  // not.
  [[nodiscard]] std::optional<BattleOffer> offer(bool available, BattleFighterProfile fighter,
                                                 bool partnerDying = false);
  // The running battle, its newest frame and this player's role in it.
  [[nodiscard]] const std::optional<BattleBegin>& running() const { return running_; }
  [[nodiscard]] const std::optional<BattleFrame>& frame() const { return frame_; }
  [[nodiscard]] std::optional<BattleRole> role() const;
  // The players whose partners fight in the running battle: its starter, then its allies; none
  // without one.
  [[nodiscard]] std::vector<PlayerId> fighting() const;
  // The player whose end box this one waits on: the starter, once the battle's end box is up and
  // this player cannot close it.
  [[nodiscard]] std::optional<PlayerId> waitingFor() const;
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
  PlayerId self_ = GuestPlayerId;
  std::uint32_t nextAsk_ = 1;
  std::optional<BattleOffer> offered_;
  std::optional<std::uint32_t> pending_;
  std::optional<BattleBegin> running_;
  std::optional<BattleFrame> frame_;
  std::optional<BattleBegin> began_;
  std::optional<BattleRefused> refused_;
  std::optional<BattleEnd> ended_;
};

} // namespace dwe::game::coop
