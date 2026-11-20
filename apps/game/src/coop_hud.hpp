#pragma once

#include <dwe/screens/field_hud_view.hpp>
#include <dwe/ui/ui.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace dwe::app {

// The co-op game's own HUD (an engine addition; the original has no online play): the other
// player's name floating over their character in the field, and short notices about the room (the
// other player dropped, the host closed the room, a guest's game is not saved). One small document
// whose look is its own, whatever the skin, shown only while it has something to show.
inline constexpr std::string_view CoopHudDocument = "coop_hud";
// What the document reads. The tag: whether it shows, where (x and y in percent of the window, the
// point over the character's head) and who it names (CoopPeerLabel: a name, else a text key with
// a number); its revision counts up when any of them changes.
inline constexpr std::string_view CoopTagVisibleSetting = "coop_hud.tag.visible";
inline constexpr std::string_view CoopTagXSetting = "coop_hud.tag.x";
inline constexpr std::string_view CoopTagYSetting = "coop_hud.tag.y";
inline constexpr std::string_view CoopTagNameSetting = "coop_hud.tag.name";
inline constexpr std::string_view CoopTagNameKeySetting = "coop_hud.tag.name_key";
inline constexpr std::string_view CoopTagNumberSetting = "coop_hud.tag.number";
inline constexpr std::string_view CoopTagRevisionSetting = "coop_hud.tag.revision";
// The other tags of a room of more than two (#352): coop_hud.tag.<n>.* for n 2 and 3, with the
// same fields as the first; the first tag's revision redraws them all.
inline constexpr std::size_t MaxCoopTags = 3;
// Whether the tag says that player can be asked for a trade now (the trade key's hint); the
// other tags' is coop_hud.tag.<n>.trade.
inline constexpr std::string_view CoopTagTradeSetting = "coop_hud.tag.trade";
// Whether the tag says that player trains (#365); the other tags' is coop_hud.tag.<n>.training.
inline constexpr std::string_view CoopTagTrainingSetting = "coop_hud.tag.training";
// The notice: whether it shows, its text key ("{name}" in it stands for the player it is about)
// and that player, as the tag names them; its revision counts up with each notice.
inline constexpr std::string_view CoopNoticeShownSetting = "coop_hud.notice.shown";
inline constexpr std::string_view CoopNoticeTextSetting = "coop_hud.notice.text";
inline constexpr std::string_view CoopNoticeNameSetting = "coop_hud.notice.name";
inline constexpr std::string_view CoopNoticeNameKeySetting = "coop_hud.notice.name_key";
inline constexpr std::string_view CoopNoticeNumberSetting = "coop_hud.notice.number";
inline constexpr std::string_view CoopNoticeRevisionSetting = "coop_hud.notice.revision";
// The notices' texts (content/packs/game/localization/<language>/online.yaml).
inline constexpr std::string_view CoopDisconnectedText = "online.coop.disconnected";
inline constexpr std::string_view CoopHostLeftText = "online.coop.host_left";
// A guest whose link to the host dropped during the game: it goes back to its seat (#356).
inline constexpr std::string_view CoopRejoiningText = "online.coop.rejoining";
// The host's game under way: a player joined it, {name}, or came back to its seat.
inline constexpr std::string_view CoopJoinedText = "online.coop.joined";
// As a guest's game starts: it is the host's world, {name}'s, and only its partner and bag go home.
inline constexpr std::string_view CoopGuestNotSavedText = "online.coop.guest_not_saved";
// A guest's partner and bag went home to its save as its game ended, from {name}'s world; or they
// could not (coop_guest_return.hpp).
inline constexpr std::string_view CoopWentHomeText = "online.coop.went_home";
inline constexpr std::string_view CoopHomeFailedText = "online.coop.home_failed";
// A save that came back from {name}'s world, as it loads.
inline constexpr std::string_view CoopCameHomeText = "online.coop.came_home";
// The notices told as a game's field first shows (that one, and what a guest's game keeps) stay
// longer than the room's: they are the first thing the game says.
inline constexpr double CoopFieldNoticeSeconds = 8.0;
// A guest whose host is busy (a long load there): it waits for the host, {name}.
inline constexpr std::string_view CoopWaitingHostText = "online.coop.waiting_host";
// The words that name a player with no name of their own.
inline constexpr std::string_view HostPlayerText = "online.host_player";
inline constexpr std::string_view GuestPlayerText = "online.guest_player";

// How the other player is named: their own online name, else a text key (with the guest's number
// for "Guest {number}").
struct CoopPeerLabel {
  std::string name;
  std::string key;
  std::int32_t number = 0;

  friend bool operator==(const CoopPeerLabel&, const CoopPeerLabel&) = default;
};

// The label of the other player from this one's side: its name (CoopLink::remoteName: the guest's
// from its hello, the host's from its welcome). An older host sends no name: the guest calls it
// "Host". A guest with no name is "Guest 1" (the host is the room's first member, as the rooms
// screen counts them).
[[nodiscard]] CoopPeerLabel coopPeerLabel(bool localHosts, std::string_view remoteName);
// The label of player `id` in a room of up to four: its name, else "Host" or "Guest {id}".
[[nodiscard]] CoopPeerLabel coopPlayerLabel(std::uint8_t id, std::string_view name);

// The tag as the document shows it.
struct CoopTagView {
  bool visible = false;
  double x = 0.0;
  double y = 0.0;
  CoopPeerLabel peer;
  // The trade key's hint under the name (coop_trades.hpp: CoopTrades::askNearest).
  bool trade = false;
  // Whether that player's partner is on a gym course now (#365): the tag says so under the name.
  bool training = false;

  friend bool operator==(const CoopTagView&, const CoopTagView&) = default;
};

// The tag of the other player (none while no one is linked) from where their head lands in the
// window (none when it does not project): hidden off the window, or while the field does not run
// (a menu, a talk or a battle stops it, and the field's HUD hides too). `trade` shows the trade
// key's hint under the name, `training` that the player trains.
[[nodiscard]] CoopTagView coopTagViewOf(const std::optional<CoopPeerLabel>& peer,
                                        std::optional<screens::WindowFraction> head,
                                        bool fieldRunning, bool trade = false,
                                        bool training = false);

// What losing the co-op link during play means for this player: nothing out of play, a notice
// that the guest dropped on the host (its slot stays for a reconnect), and back to the title on
// the guest, whose game was the host's world.
enum class CoopLinkLoss : std::uint8_t { Quiet, PeerLeft, HostLeft };
[[nodiscard]] CoopLinkLoss coopLinkLoss(bool localHosts, bool playing);

// Publishes the tag and the notices to the document, and shows it while either is up.
// A guest whose host's world stays quiet (the host is busy with a long load, its link kept alive)
// says it waits for the host instead of looking frozen: the notice comes once the quiet passed
// QuietAfter and again before it goes while the quiet lasts.
class CoopHostWait {
public:
  static constexpr std::chrono::milliseconds QuietAfter{2000};
  static constexpr std::chrono::milliseconds Every{3000};

  // Whether the notice is due at `now`, the host's world quiet for `quietFor`.
  [[nodiscard]] bool due(std::chrono::milliseconds quietFor, std::chrono::milliseconds now);

private:
  std::optional<std::chrono::milliseconds> shownAt_;
};

class CoopHud {
public:
  // How long a notice stays.
  static constexpr double NoticeSeconds = 4.0;

  explicit CoopHud(ui::Ui& ui) : ui_{&ui} {}

  // The tag this frame; publishes only what changed.
  void showTag(const CoopTagView& view) { showTags({&view, 1}); }
  // A tag over each other player this frame (up to MaxCoopTags; the ones past them hide).
  void showTags(std::span<const CoopTagView> views);
  // Shows a notice for `seconds`, in place of the one up; `peer` fills its "{name}".
  void notice(std::string_view textKey, const CoopPeerLabel& peer = {},
              double seconds = NoticeSeconds);
  // Shows a notice once the one up is gone (from the next advance when none is): news no notice
  // that comes right after it may hide, such as a guest's progress going home as the host leaves.
  void noticeNext(std::string_view textKey, const CoopPeerLabel& peer = {},
                  double seconds = NoticeSeconds);
  // Counts the notice's time down (real seconds), and shows or hides the document.
  void advance(double seconds);
  [[nodiscard]] bool noticeShown() const { return noticeLeft_ > 0.0; }

private:
  void showDocument();

  ui::Ui* ui_;
  std::optional<std::array<CoopTagView, MaxCoopTags>> published_;
  double noticeLeft_ = 0.0;
  bool noticePublished_ = false;
  struct Pending {
    std::string textKey;
    CoopPeerLabel peer;
    double seconds = NoticeSeconds;
  };
  std::optional<Pending> next_;
};

} // namespace dwe::app
