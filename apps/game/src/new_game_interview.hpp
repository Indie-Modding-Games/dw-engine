#pragma once

#include <dwe/animation/animation_player.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/model_renderer.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/rhi/types.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/ui/ui.hpp>

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "naming_box.hpp"
#include "new_game_scene_rules.hpp"

namespace dwe::app {

// The new game's opening before the first map (main.c: newGameScene): the old Digimon on black
// asks the player about themselves (shop/script_menu.c: newGameStateMachine, the lines of MAPHEAD's
// table 0x4D6); the answers pick the partner, and the player names themselves and the partner in
// the naming box. Then it fades out and the game starts with the choice (main.c: main).
class NewGameInterview {
public:
  static core::Result<std::unique_ptr<NewGameInterview>>
  create(render::Renderer& renderer, ui::Ui& ui, const assets::VirtualFileSystem& files,
         NewGameSceneRules rules, game::story::SoundPort* sound);
  ~NewGameInterview();
  NewGameInterview(const NewGameInterview&) = delete;
  NewGameInterview& operator=(const NewGameInterview&) = delete;
  NewGameInterview(NewGameInterview&&) = delete;
  NewGameInterview& operator=(NewGameInterview&&) = delete;

  // One engine tick: the player's buttons and the scene's frame count.
  void tick(const input::InputState& input);
  // What the keyboard typed, for the naming box while it is open (takesText).
  [[nodiscard]] bool takesText() const { return naming_.isOpen(); }
  void type(std::string_view text, bool enter) { naming_.type(text, enter); }
  // Once per frame: the host's animation and the camera.
  void update(double frameSeconds, rhi::Extent extent);
  [[nodiscard]] std::span<render::Drawer* const> drawers() const { return drawers_; }
  // How dark the screen is, from 0 to 1 (the fades in and out).
  [[nodiscard]] float fadeLevel() const;
  // Over once it faded out; the answers and the names.
  [[nodiscard]] bool finished() const;
  [[nodiscard]] session::NewGameChoice choice() const;

private:
  // What waits for the player: a line to confirm, a selection, the naming box.
  enum class Waiting : std::uint8_t { Nothing, Line, Selection, Naming };

  NewGameInterview(ui::Ui& ui, NewGameSceneRules rules, std::vector<std::string> lines,
                   std::unique_ptr<render::ModelRenderer> models,
                   std::unique_ptr<assets::ModelData> host);

  // The state machine's states (newGameStateMachine's switch), each run once until it waits.
  void step();
  void take(const input::InputState& input);
  void play(const game::story::DialogueSound& sound);
  void turnHost();
  void say(std::size_t line, std::uint8_t next);
  void ask(std::size_t line, std::uint8_t next);
  void showText(bool open);
  [[nodiscard]] std::string lineText(std::size_t line) const;
  [[nodiscard]] std::string tamerName() const;
  [[nodiscard]] std::uint64_t ticksOf(std::uint32_t frames) const;

  ui::Ui* ui_;
  NewGameSceneRules rules_;
  std::vector<std::string> lines_;
  std::unique_ptr<render::ModelRenderer> models_;
  std::unique_ptr<assets::ModelData> host_;
  render::ModelHandle hostHandle_{};
  std::unique_ptr<animation::AnimationPlayer> hostPlayer_;
  glm::vec3 hostPosition_{0.0F};
  float hostYaw_ = 0.0F;
  std::array<render::Drawer*, 2> drawers_{};
  NamingBox naming_;
  game::story::SoundPort* sound_ = nullptr;

  std::uint64_t ticks_ = 0;
  // The tick the shown page is drawn by; presses before it do not count.
  std::uint64_t inputFrom_ = 0;
  // The questions start once the host turned to the player (pstat 0xF3 = 0).
  bool turned_ = false;
  std::uint8_t state_ = 0;
  std::uint8_t next_ = 0;
  Waiting waiting_ = Waiting::Nothing;
  std::optional<std::size_t> shownLine_;
  std::size_t cursor_ = 0;
  NameTarget target_ = NameTarget::Tamer;
  std::optional<std::size_t> answer_;
  std::string tamerName_;
  std::string partnerName_;
  std::optional<std::uint64_t> endTick_;
};

} // namespace dwe::app
