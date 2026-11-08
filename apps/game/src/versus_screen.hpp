#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/ui/ui.hpp>

#include <memory>
#include <optional>
#include <string_view>

#include "options.hpp"
#include "versus_flow.hpp"

namespace dwe::session {
class BattleAdapter;
} // namespace dwe::session

namespace dwe::app {

inline constexpr std::string_view VersusDocument = "versus";
// The main menu's entry that opens it.
inline constexpr std::string_view VersusCommand = "versus";
// Every action of the versus document goes to the game as this.
inline constexpr std::string_view VersusActionCommand = "versus_action";

// «Versus en línea» from the title screen: the versus flow (versus_flow.hpp) on the versus
// document, with the battles read from the command line's ruleset, the saves of the saves folder
// and real UDP links (net::UdpLink). It never writes a save.
class VersusScreen {
public:
  VersusScreen(const assets::VirtualFileSystem& files, const Options& options, bool keepsSaves,
               ui::Ui& ui);
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
  // Once per game tick: the network, the battle and the document's values.
  void tick(double nowSeconds);
  // Whether the player left the versus screens since the last call (the title shows again).
  [[nodiscard]] bool takeClosed();

private:
  void close();

  const assets::VirtualFileSystem* files_;
  const Options* options_;
  bool keepsSaves_;
  ui::Ui* ui_;
  std::unique_ptr<session::BattleAdapter> battles_;
  std::optional<VersusFlow> flow_;
  bool active_ = false;
  bool closed_ = false;
};

} // namespace dwe::app
