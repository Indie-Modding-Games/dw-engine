#pragma once

#include <dwe/screens/field_hud_view.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::app {

// The co-op game's own HUD (an engine addition; the original has no online play): the other
// player's name floating over their character in the field, and short notices about the room (the
// other player dropped, the host closed the room). One small document whose look is its own,
// whatever the skin, shown only while it has something to show.
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

// The tag as the document shows it.
struct CoopTagView {
  bool visible = false;
  double x = 0.0;
  double y = 0.0;
  CoopPeerLabel peer;

  friend bool operator==(const CoopTagView&, const CoopTagView&) = default;
};

// The tag of the other player (none while no one is linked) from where their head lands in the
// window (none when it does not project): hidden off the window, or while the field does not run
// (a menu, a talk or a battle stops it, and the field's HUD hides too).
[[nodiscard]] CoopTagView coopTagViewOf(const std::optional<CoopPeerLabel>& peer,
                                        std::optional<screens::WindowFraction> head,
                                        bool fieldRunning);

// What losing the co-op link during play means for this player: nothing out of play, a notice
// that the guest dropped on the host (its slot stays for a reconnect), and back to the title on
// the guest, whose game was the host's world.
enum class CoopLinkLoss : std::uint8_t { Quiet, PeerLeft, HostLeft };
[[nodiscard]] CoopLinkLoss coopLinkLoss(bool localHosts, bool playing);

// Publishes the tag and the notices to the document, and shows it while either is up.
class CoopHud {
public:
  // How long a notice stays.
  static constexpr double NoticeSeconds = 4.0;

  explicit CoopHud(ui::Ui& ui) : ui_{&ui} {}

  // The tag this frame; publishes only what changed.
  void showTag(const CoopTagView& view);
  // Shows a notice for NoticeSeconds, in place of the one up; `peer` fills its "{name}".
  void notice(std::string_view textKey, const CoopPeerLabel& peer = {});
  // Counts the notice's time down (real seconds), and shows or hides the document.
  void advance(double seconds);
  [[nodiscard]] bool noticeShown() const { return noticeLeft_ > 0.0; }

private:
  void showDocument();

  ui::Ui* ui_;
  std::optional<CoopTagView> published_;
  double noticeLeft_ = 0.0;
  bool noticePublished_ = false;
};

} // namespace dwe::app
