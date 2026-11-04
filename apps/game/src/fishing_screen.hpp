#pragma once

#include <dwe/game/fishing/fishing.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/session/fishing_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>
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
  // The camera button (Home; gamepad North): with it held the arrows move the view.
  bool cameraDown = false;
  bool left = false;
  bool right = false;
  bool up = false;
  bool down = false;
};

// The pause menu's Fish (game_menu.c: initializeFishing; main/fish.c, the FISH overlay): the player
// takes out the rod, picks a bait, casts, waits for a bite, strikes and reels the fish in, with the
// line's tension on a gauge and the original's notices. The field and the game time wait meanwhile
// (clock.c: tickGameClock stops while isFishing). The view scrolls as the fishing's camera says,
// the fish, the sea serpent, the rod, the bobber and the line are drawn as the original's.
class FishingScreen {
public:
  // The adapters, the session, the UI, the sound (nullptr: silent) and the story (nullptr: the sea
  // serpent's scene does not play) must outlive this object.
  FishingScreen(session::FishingAdapter& fishing, const session::InventoryAdapter& inventory,
                session::GameSession& session, ui::Ui& ui, game::story::SoundPort* sound,
                session::StoryAdapter* story);

  // Whether the player fishes or reads a fishing notice: the field waits meanwhile.
  [[nodiscard]] bool active() const;
  // The player entered a map (checkFishingMap).
  void enterMap(FieldScene& scene, std::string_view mapId);
  // Once per frame: tells the pause menu whether to offer fishing (getFishingAvailability).
  void update(bool fieldIdle);
  // One logic tick: the fishing runs at the original's 20 frames per second.
  void tick(FieldScene& scene, const FishingButtons& buttons, double seconds);
  // One logic tick while the rod is away: the water of a fishing map runs as one of the field's
  // objects from the map's load (FISH_init adds FISH_tickFishingWorld and its drawing), so the fish
  // swim and show all the time, under the menus and in a battle too (BTL_battleTickFrame runs
  // tickObjects). Nothing while the fishing runs (tick does) or on a map without water; water
  // that went with the map still loaded (the partner's death) stops showing.
  void tickWater(FieldScene& scene, double seconds);
  // The partner evolves (FISH_tickFishingWorld and its drawing skip PARTNER_STATE 13): the water
  // neither moves nor shows.
  void hideWater(FieldScene& scene);
  // Takes the pause menu's fish, the bait menu's choice and the notices' answers; false for any
  // other command.
  bool handle(const ui::UiCommand& command);

private:
  struct Notice {
    std::string text;
    std::string item;
    std::int32_t size = 0;
    // Where the view scrolls while it shows (FISH_showTextBox's point).
    glm::vec3 target{0.0F};
  };

  void frame(FieldScene& scene);
  [[nodiscard]] game::fishing::FishingInput inputOf(const FieldScene& scene) const;
  void follow(FieldScene& scene, const game::fishing::FishingOutput& out);
  void openBaitMenu();
  void showNextNotice();
  void showMarkers(FieldScene& scene) const;
  void showPrims(FieldScene& scene) const;
  void showCamera(FieldScene& scene) const;
  void showGauge();

  session::FishingAdapter* fishing_;
  const session::InventoryAdapter* inventory_;
  session::GameSession* session_;
  ui::Ui* ui_;
  game::story::SoundPort* sound_;
  session::StoryAdapter* story_;
  double frameSeconds_ = 0.0;
  // What the buttons did since the last fishing frame.
  bool confirmPressed_ = false;
  bool confirmReleased_ = false;
  bool cancelPressed_ = false;
  bool confirmDown_ = false;
  bool cameraPressed_ = false;
  bool cameraDown_ = false;
  FishingButtons buttons_;
  std::vector<game::inventory::Stack> baits_;
  std::deque<Notice> notices_;
  bool noticeShown_ = false;
  glm::vec3 noticeTarget_{0.0F};
  bool gaugeShown_ = false;
  // Whether the scene draws this water's prims.
  bool waterShown_ = false;
  std::optional<std::int32_t> offered_;
};

} // namespace dwe::app
