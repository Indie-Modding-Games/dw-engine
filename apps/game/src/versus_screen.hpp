#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/audio/audio.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/ui/ui.hpp>

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "field_scene.hpp"
#include "graphics_options.hpp"
#include "options.hpp"
#include "versus_arena.hpp"
#include "versus_flow.hpp"
#include "versus_loopback.hpp"

namespace dwe::ruleset {
class Ruleset;
} // namespace dwe::ruleset

namespace dwe::session {
class BattleAdapter;
struct VersusPlacement;
} // namespace dwe::session

namespace dwe::app {

inline constexpr std::string_view VersusDocument = "versus";
// The main menu's entry that opens it.
inline constexpr std::string_view VersusCommand = "versus";
// Every action of the versus document goes to the game as this.
inline constexpr std::string_view VersusActionCommand = "versus_action";

// «Versus en línea» from the title screen: the versus flow (versus_flow.hpp) on the versus
// document, with the battles read from the command line's ruleset, the saves of the saves folder
// and WebRTC links (net::RtcPeerLink) through the signaling server of the game pack's
// versus/network.yaml or --versus-server. It never writes a save. Its battles show on the versus
// arena (versus_arena.hpp) with the document over them.
class VersusScreen {
public:
  // The renderer and the audio, which must outlive it, may be null: then the battles show only on
  // the document (and they are not timed by the fighters' models: see VersusArena::prepare).
  VersusScreen(const assets::VirtualFileSystem& files, const Options& options, bool keepsSaves,
               ui::Ui& ui, render::Renderer* renderer, audio::Audio* audio);
  ~VersusScreen();
  VersusScreen(const VersusScreen&) = delete;
  VersusScreen& operator=(const VersusScreen&) = delete;
  VersusScreen(VersusScreen&&) = delete;
  VersusScreen& operator=(VersusScreen&&) = delete;

  // Shows the versus menu; the rules are read the first time. False when they cannot be (the
  // log says why): the title stays.
  bool open();
  [[nodiscard]] bool active() const { return active_; }
  // Takes the document's commands while it is open; false for any other command.
  bool handle(const ui::UiCommand& command);
  [[nodiscard]] bool takesText() const { return active_ && flow_ && flow_->takesText(); }
  void type(std::string_view text, bool enter);
  // The finisher's own button (the "extra" input; vs_combat.c: VS__tickInput's square).
  void pressFinisher();
  // Once per game tick: the network, the battle and the document's values.
  void tick(double nowSeconds);
  // Whether the player left the versus screens since the last call (the title shows again).
  [[nodiscard]] bool takeClosed();
  // The arena's scene while a battle shows on it, for the game to draw under the document; null
  // otherwise.
  [[nodiscard]] FieldScene* arenaScene();
  // Once per drawn frame, before the arena is drawn: alpha between the last two ticks.
  void update(double frameSeconds, float alpha, const GraphicsOptions& graphics);

private:
  void close();
  // Builds the arena ahead of the battles: once the players link, on the tick after the wait
  // shows (it takes a second or two), then one more arena's model a tick.
  void readyArena();
  void buildArena();

  const assets::VirtualFileSystem* files_;
  const Options* options_;
  bool keepsSaves_;
  ui::Ui* ui_;
  render::Renderer* renderer_;
  audio::Audio* audio_;
  std::unique_ptr<ruleset::Ruleset> rules_;
  std::unique_ptr<session::BattleAdapter> battles_;
  // Where each arena's battles start (vs_intro.c: VS__placePlayer1), once the arena is built.
  std::vector<session::VersusPlacement> placements_;
  bool arenaBuilt_ = false;
  // Whether the last tick's view was one where the players link, and the arena models are left to
  // load ahead.
  bool linking_ = false;
  bool preloading_ = true;
  // The arena the battles show on, and the other player's view of --versus-loopback, which readies
  // its battles as this machine's are; both before the flows that call them.
  std::unique_ptr<VersusArena> arena_;
  std::unique_ptr<VersusBattleView> rivalView_;
  // With --versus-loopback: the other player's flow desc (the links aside) and the other player of
  // this opening, before the flow, whose links borrow its network.
  std::optional<VersusFlowDesc> rivalDesc_;
  std::unique_ptr<VersusLoopback> loopback_;
  std::optional<VersusFlow> flow_;
  // The last tick's time, for the loopback's clock.
  std::optional<double> lastSeconds_;
  bool active_ = false;
  bool closed_ = false;
};

} // namespace dwe::app
