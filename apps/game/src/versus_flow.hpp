#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/battle/events.hpp>
#include <dwe/game/versus/protocol.hpp>
#include <dwe/net/peer_link.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/battle_registry.hpp>
#include <dwe/session/versus_series.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

// A save a versus player can bring partners from: what the list shows of it and its registered
// partners.
struct VersusSave {
  // The player's name and the partner's nickname, as the save's summary has them.
  std::string player;
  std::string nickname;
  std::vector<session::RegisteredPartner> partners;
};

// What the versus flow needs from the game: the battles, who this machine is, the saves and the
// network. The callbacks let the tests run two flows over a simulated network.
struct VersusFlowDesc {
  // Must outlive the flow and have read the rules (with the ruleset's versus rules).
  const session::BattleAdapter* battles = nullptr;
  game::versus::Identity identity;
  // The saves to pick partners from, read when the player gets to pick.
  std::function<std::vector<VersusSave>()> saves;
  // A link that makes a room, for the host; one that joins the room with its code, for the guest.
  // What goes wrong on the way (no server, a wrong code, no route) is the link's failure.
  std::function<std::unique_ptr<net::PeerLink>()> host;
  std::function<std::unique_ptr<net::PeerLink>(std::string_view code)> join;
  // The seed of a new series, for the host.
  std::function<std::uint64_t()> seed;
};

// The screens of an online versus series (docs/modulos/apps/game.md, «Versus en línea»), without
// the UI: a menu to host or join, the host's series length and its room code, the guest's code
// entry, the pick of a save and of its registered partners, the battles with this side's commands,
// the result of each and the series' end, and what went wrong when it ends early.
// It takes the document's actions and the keyboard's text, and gives the settings the document
// shows. Nothing here writes a save.
class VersusFlow {
public:
  enum class View : std::uint8_t {
    Menu,
    HostSetup,
    Hosting,
    JoinEntry,
    Joining,
    SavePick,
    Lineup,
    WaitingLineup,
    Match,
    Intermission,
    Result,
    Error,
  };

  explicit VersusFlow(VersusFlowDesc desc);
  ~VersusFlow();
  VersusFlow(const VersusFlow&) = delete;
  VersusFlow& operator=(const VersusFlow&) = delete;
  VersusFlow(VersusFlow&&) = delete;
  VersusFlow& operator=(VersusFlow&&) = delete;

  // Starts again at the menu, with no link.
  void open();
  [[nodiscard]] View view() const { return view_; }
  // Whether the player left the versus screens (back to the title).
  [[nodiscard]] bool closed() const { return closed_; }
  // Whether the view takes the keyboard's text (the guest's code entry).
  [[nodiscard]] bool takesText() const { return view_ == View::JoinEntry; }

  // A document action: up, down, left, right, confirm, cancel, or a row's "pick:<n>".
  void act(std::string_view action);
  // The keyboard's text of a frame, and whether Enter was pressed.
  void type(std::string_view text, bool enter);
  // Once per game tick (60 a second): the network, and every third tick a battle frame.
  void tick(net::Millis now);

  [[nodiscard]] screens::ScreenSettings settings() const;
  [[nodiscard]] const session::VersusSeries* series() const {
    return series_ ? &*series_ : nullptr;
  }

private:
  void leave();
  void fail(std::string key, std::string detail = {});
  void startHosting();
  void startJoining();
  void enterSavePick();
  void pickPartner();
  void startBattle();
  void takeEvents(const game::battle::BattleEvents& events);
  void actInMatch(std::string_view action);
  void moveCursor(std::int32_t step, std::int32_t count);
  [[nodiscard]] bool enoughPartners(const VersusSave& save) const;
  [[nodiscard]] std::int32_t battles() const;
  [[nodiscard]] std::vector<game::battle::Command> localMenu() const;

  VersusFlowDesc desc_;
  View view_ = View::Menu;
  bool closed_ = false;
  std::int32_t cursor_ = 0;
  // The series length the host picks, by its place in the rules' series.
  std::int32_t seriesChoice_ = 0;
  std::string typed_;
  // A code that cannot be one, shown under the entry.
  bool badEntry_ = false;
  std::vector<VersusSave> saves_;
  std::int32_t save_ = 0;
  // The picked partners, by their row in the save's registered partners.
  std::vector<std::int32_t> picks_;
  // The link before the series: the series refers to it, so it goes after it.
  std::unique_ptr<net::PeerLink> link_;
  std::optional<session::VersusSeries> series_;
  std::int64_t ticks_ = 0;
  // Battle frames in a row with no tick played: the other machine is behind.
  std::int32_t stalledFrames_ = 0;
  // Frames the result of a battle stays before the next one starts.
  std::int32_t intermissionFrames_ = 0;
  // A first cancel in a battle asks for a second to leave; the frames it waits for it.
  std::int32_t leaveFrames_ = 0;
  // The last lines of what happened in the battle: a text key and its names and number.
  struct LogLine {
    std::string key;
    std::string who;
    std::string what;
    std::int32_t amount = 0;
  };
  std::deque<LogLine> log_;
  std::string errorKey_;
  std::string errorDetail_;
};

} // namespace dwe::app
