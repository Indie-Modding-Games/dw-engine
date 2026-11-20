#pragma once

#include <dwe/game/battle/battle.hpp>
#include <dwe/game/coop/battle.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

class CoopGameplayBridge;
class PlaySession;

// The guest's battle document (content/packs/game/ui/coop_battle): the host's battle as this
// player sees it, with the orders of the starter and the help of the other player.
inline constexpr std::string_view CoopBattleDocument = "coop_battle";
// Its commands: an order for the partner (a game::battle::Command's number), a cheer, an item of
// the bag thrown (its id) and the end box closed.
inline constexpr std::string_view CoopBattleOrderCommand = "coop_battle_order";
inline constexpr std::string_view CoopBattleCheerCommand = "coop_battle_cheer";
inline constexpr std::string_view CoopBattleItemCommand = "coop_battle_item";
inline constexpr std::string_view CoopBattleCloseCommand = "coop_battle_close";
// Its settings (ui_contract.cpp lists them all); each publish counts the revision up.
inline constexpr std::string_view CoopBattleRevisionSetting = "coop_battle.revision";

// The battle's outcome as the protocol and the story say it.
[[nodiscard]] game::coop::BattleOutcome coopOutcomeOf(game::battle::Outcome outcome);
// What a visitor's battle left its partner, to send back to its game, and that again as the
// result the guest's own game takes (no bits nor drops: those stay in the host's world).
[[nodiscard]] game::coop::BattleSpoils spoilsOf(const game::battle::BattleResult& result);
[[nodiscard]] game::battle::BattleResult resultOf(const game::coop::BattleSpoils& spoils,
                                                  game::coop::BattleOutcome outcome,
                                                  std::string partnerSpecies);

// The co-op battles of a played game (docs/architecture/multiplayer.md, §6 decision 3;
// coop-combat.md): the shared world runs one battle at a time on the host's game. The partner of
// the player who started it fights, and the other player's beside it when that player stands on the
// map, free (the guest's offer; the host's own in a guest's battle). Each player commands their own
// partner, throws items from their own bag and cheers; a player without a partner in it helps.
//
// On the host it answers the guest's asks (BattleReferee), tells the guest of every battle that
// begins, sends its frames and its end, and applies the guest's actions. On the guest the story's
// battles go to the host (PlaySession::handBattlesToHost) while the host stands on the same map;
// elsewhere, or when the host cannot run one, they are the guest's own solo battles. The guest's
// field holds while a host battle on its map runs, its fighters drawn where the host's frames put
// them, and the guest's own script gets the outcome when it ends.
class CoopBattles {
public:
  enum class Role : std::uint8_t { None, Host, Guest };

  explicit CoopBattles(ui::Ui& ui) : ui_{&ui} {}

  // Once per tick after the game's tick. `bridge` is null while there is no link (the battle's
  // bookkeeping still runs on the host, so a battle the guest left ends cleanly). `here` lists the
  // players on this game's map by the session's presence query (coopPlayerHere).
  void tick(PlaySession& play, CoopGameplayBridge* bridge, Role role,
            std::span<const game::coop::PlayerId> here);
  // The guest's battle document's commands; false for any other.
  bool handle(const ui::UiCommand& command);
  // The guest's buttons outside the document: `extra` calls the finisher for the starter and
  // cheers for the helper, as the battle's square button does.
  void press(const input::InputState& input);
  // The host: the guest's link went. Its partner fights on alone and nothing is sent back.
  void guestLeft();
  // The guest: the host went. The battle shown and the ask waiting go.
  void hostLeft(PlaySession* play);
  // No game is played any more: everything goes.
  void reset();

private:
  void hostTick(PlaySession& play, CoopGameplayBridge* bridge);
  void answer(PlaySession& play, CoopGameplayBridge& bridge, const game::coop::BattleAsk& ask);
  void apply(PlaySession& play, const game::coop::BattleAction& action);
  void offerAllies(PlaySession& play);
  [[nodiscard]] game::coop::BattleBegin beginOf(PlaySession& play, game::coop::PlayerId starter,
                                                std::uint32_t ask);
  [[nodiscard]] game::coop::BattleFrame frameOf(const PlaySession& play);
  void guestTick(PlaySession& play, CoopGameplayBridge& bridge);
  void sendOffer(PlaySession& play, CoopGameplayBridge& bridge);
  void startShowing(PlaySession& play, const game::coop::BattleBegin& begin);
  void applySpoils(PlaySession& play, const game::coop::BattleEnd& end);
  void showFrame(PlaySession& play);
  void stopShowing(PlaySession& play, const game::coop::BattleEnd& end);
  void lookAtBattle(PlaySession& play, const game::coop::BattleFrame& frame);
  void lookBack(PlaySession& play);
  void publish(const PlaySession& play);
  void hideDocument();
  void send(CoopGameplayBridge* bridge, game::coop::Message message);

  ui::Ui* ui_;
  PlaySession* play_ = nullptr;
  game::coop::BattleReferee referee_;
  std::uint32_t frames_ = 0;
  std::uint64_t ticks_ = 0;
  // The players on this game's map this tick (tick's `here`).
  std::vector<game::coop::PlayerId> here_;
  game::coop::BattleMirror mirror_;
  // The guest's ask waiting for its answer, and whether the battle shown is the one it asked for.
  std::optional<std::uint32_t> asked_;
  bool ownBattle_ = false;
  // Whether this player's partner fights in the host's battle beside the host's.
  bool allyBattle_ = false;
  // Whether this player watches the host's battle (it runs on this player's map), and whether
  // that one is a fixed fight (its enemies stay where it left them).
  bool watching_ = false;
  bool shownFixedFight_ = false;
  std::optional<std::uint32_t> shownFrame_;
  // The fighters as the last frame with them showed them (the end's frames have none).
  std::vector<game::coop::BattleFighterState> fighters_;
  // The guest's actions from its document, sent on the next tick.
  std::vector<game::coop::BattleAction> actions_;
  bool documentShown_ = false;
  // Whether the helper's camera was turned to the battle.
  bool cameraOnBattle_ = false;
};

} // namespace dwe::app
