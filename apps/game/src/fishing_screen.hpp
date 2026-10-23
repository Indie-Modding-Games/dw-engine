#pragma once

#include <dwe/game/fishing/fishing.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/session/fishing_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

class FieldScene;

inline constexpr std::string_view FishingBaitDocument = "fishing_bait";
inline constexpr std::string_view FishingNoticeDocument = "fishing_notice";
inline constexpr std::string_view FishingHudDocument = "fishing_hud";

// The buttons the fishing reads on a logic tick, besides what the field reads.
struct FishingButtons {
  bool confirmDown = false;
  bool cancelPressed = false;
  bool left = false;
  bool right = false;
};

// The pause menu's Fish (game_menu.c: initializeFishing; main/fish.c, the FISH overlay): the player
// takes out the rod, picks a bait, casts, waits for a bite, strikes and reels the fish in, with the
// line's tension on a gauge and the original's notices. The field and the game time wait meanwhile
// (clock.c: tickGameClock stops while isFishing). The rod and the fish are drawn as markers until
// their models are.
class FishingScreen {
public:
  // The adapters, the session, the UI and the sound (nullptr: silent) must outlive this object.
  FishingScreen(session::FishingAdapter& fishing, const session::InventoryAdapter& inventory,
                session::GameSession& session, ui::Ui& ui, game::story::SoundPort* sound);

  // Whether the player fishes or reads a fishing notice: the field waits meanwhile.
  [[nodiscard]] bool active() const;
  // The player entered a map (checkFishingMap).
  void enterMap(FieldScene& scene, std::string_view mapId);
  // Once per frame: tells the pause menu whether to offer fishing (getFishingAvailability).
  void update(bool fieldIdle);
  // One logic tick: the fishing runs at the original's 20 frames per second.
  void tick(FieldScene& scene, const FishingButtons& buttons, double seconds);
  // Takes the pause menu's fish, the bait menu's choice and the notices' answers; false for any
  // other command.
  bool handle(const ui::UiCommand& command);

private:
  struct Notice {
    std::string text;
    std::string item;
    std::int32_t size = 0;
  };

  void frame(FieldScene& scene);
  [[nodiscard]] game::fishing::FishingInput inputOf(const FieldScene& scene) const;
  void follow(FieldScene& scene, const game::fishing::FishingOutput& out);
  void openBaitMenu();
  void showNextNotice();
  void showMarkers(FieldScene& scene) const;
  void showGauge();

  session::FishingAdapter* fishing_;
  const session::InventoryAdapter* inventory_;
  session::GameSession* session_;
  ui::Ui* ui_;
  game::story::SoundPort* sound_;
  double frameSeconds_ = 0.0;
  // What the buttons did since the last fishing frame.
  bool confirmPressed_ = false;
  bool confirmReleased_ = false;
  bool cancelPressed_ = false;
  bool confirmDown_ = false;
  FishingButtons buttons_;
  std::vector<assets::ItemHandle> baits_;
  std::deque<Notice> notices_;
  bool noticeShown_ = false;
  bool gaugeShown_ = false;
  std::optional<std::int32_t> offered_;
};

} // namespace dwe::app
