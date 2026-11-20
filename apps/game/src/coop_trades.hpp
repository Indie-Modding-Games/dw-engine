#pragma once

#include <dwe/game/coop/protocol.hpp>
#include <dwe/game/coop/trade.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop_hud.hpp"
#include "coop_trade_bag.hpp"

namespace dwe::app {

class CoopGameplayBridge;
class PlaySession;

// The trade document (content/packs/game/ui/coop_trade): an ask waiting for its answer, an ask to
// answer, and the table with this player's bag, both offers and both confirmations.
inline constexpr std::string_view CoopTradeDocument = "coop_trade";
// The action that asks the nearest player in reach for a trade (T, the pad's right stick press).
inline constexpr std::string_view CoopTradeAction = "trade";
// The document's commands: the answer to an ask (true accepts), one more of a bag's item on the
// table and one less (its id), the confirmation (true) or taking it back, and calling it off.
inline constexpr std::string_view CoopTradeAnswerCommand = "coop_trade_answer";
inline constexpr std::string_view CoopTradeAddCommand = "coop_trade_add";
inline constexpr std::string_view CoopTradeTakeCommand = "coop_trade_take";
inline constexpr std::string_view CoopTradeConfirmCommand = "coop_trade_confirm";
inline constexpr std::string_view CoopTradeCancelCommand = "coop_trade_cancel";
// Each publish counts it up (ui_contract.cpp lists every setting).
inline constexpr std::string_view CoopTradeRevisionSetting = "coop_trade.revision";
// How near another player must stand to be asked, in world units (five collision cells).
inline constexpr float CoopTradeReach = 500.0F;

// One link of this game as the trades use it: the player at its other end and its bridge.
struct CoopTradeLink {
  game::coop::PlayerId player = game::coop::HostPlayerId;
  CoopGameplayBridge* bridge = nullptr;
};

// The item trades of a played co-op game (#360; docs/architecture/multiplayer.md, "Item trades").
// On the host it runs the broker (game::coop::TradeBroker) for the whole session and the host's
// own side of its trades; on a guest only the guest's side, whose messages go to the host. Either
// way each player's bag is changed by its own game only.
class CoopTrades {
public:
  enum class Role : std::uint8_t { None, Host, Guest };
  // How a player is named in the document and the notices.
  using LabelOf = std::function<CoopPeerLabel(game::coop::PlayerId)>;
  // A notice for the co-op HUD: its text key and the player it names.
  struct Notice {
    std::string key;
    game::coop::PlayerId player = game::coop::HostPlayerId;
  };

  explicit CoopTrades(ui::Ui& ui) : ui_{&ui} {}

  // Once per tick after the game's tick. `self` is this player's id. `links` are the host's, one
  // per guest (the broker's mail to a guest goes through its own, so any two players trade), or a
  // guest's one to the host; none while there is no link. On the host `world` lists every player
  // of the session (where it stands, whether it is busy).
  void tick(PlaySession& play, std::span<const CoopTradeLink> links, Role role,
            game::coop::PlayerId self, std::span<const game::coop::TradePlace> world,
            const LabelOf& labelOf);
  // The document's commands; false for any other.
  bool handle(const ui::UiCommand& command);
  // The trade action in the field: the nearest player in reach is asked on the frame's
  // askNearest.
  void press(const input::InputState& input, const PlaySession& play);
  // Once per frame where the players are drawn: asks the nearest player in reach if the action
  // was pressed, and says whether `tagged` can be asked now (the hint under its tag).
  bool askNearest(const PlaySession& play, game::coop::PlayerId tagged);
  // The host: a player left the session (its trade closes, the other side undoing a swap).
  void playerLeft(game::coop::PlayerId player);
  // The guest: the host went.
  void hostLeft();
  // No game is played any more.
  void reset();
  [[nodiscard]] std::optional<Notice> takeNotice() { return std::exchange(notice_, std::nullopt); }
  // The player's side, for tests and the report.
  [[nodiscard]] const std::optional<game::coop::TradeParty>& party() const { return party_; }

private:
  // A player's trade message to the host's broker (the host's own, or one from the link).
  void toBroker(game::coop::PlayerId from, const game::coop::Message& message);
  void deliver(std::vector<game::coop::TradeMail> mails);
  void fromParty(std::optional<game::coop::Message> message);
  void flush(std::span<const CoopTradeLink> links);
  void noticeOf(const game::coop::TradeClosed& closed);
  void changeOffer(std::string_view item, std::int32_t step);
  void publish(const PlaySession& play, const LabelOf& labelOf);
  void hideDocument();
  [[nodiscard]] std::optional<game::coop::PlayerId> nearest(const PlaySession& play) const;

  ui::Ui* ui_;
  Role role_ = Role::None;
  game::coop::TradeBroker broker_;
  std::vector<game::coop::TradePlace> world_;
  std::optional<game::coop::TradeParty> party_;
  InventoryTradeBag bag_;
  // This player's own messages for the host's broker, sent on the next tick, and the host's mails
  // for the other players.
  std::vector<game::coop::Message> ownOut_;
  std::vector<game::coop::TradeMail> outbox_;
  // The other player of the trade on screen, which the notices name.
  std::optional<game::coop::PlayerId> lastOther_;
  std::optional<Notice> notice_;
  // The trade whose ask was told while the player could not see it.
  std::optional<std::uint32_t> invitedNotice_;
  bool documentShown_ = false;
  // The trade action was pressed and waits for the frame's askNearest.
  bool askPressed_ = false;
};

} // namespace dwe::app
