#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/ui/ui.hpp>

#include <memory>
#include <string_view>

#include "coop_connection.hpp"
#include "options.hpp"

namespace dwe::app {

inline constexpr std::string_view CoopOpenCommand = "coop_open";

// The main-menu cooperative lobby: creates or joins a WebRTC room and keeps the validated coop
// handshake alive. The connected link is exposed for the gameplay bridge to attach next.
class CoopScreen {
public:
  CoopScreen(const assets::VirtualFileSystem& files, const Options& options, ui::Ui& ui);
  bool handle(const ui::UiCommand& command);
  [[nodiscard]] bool takesText() const { return active_ && connection_.takesText(); }
  void type(std::string_view text, bool enter);
  void tick(double nowSeconds);
  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] game::coop::CoopLink* link() { return connection_.link(); }
  [[nodiscard]] bool connected() const { return connection_.connected(); }
  [[nodiscard]] bool isHost() const { return isHost_; }

private:
  void publish();

  ui::Ui* ui_;
  CoopConnection connection_;
  bool active_ = false;
  bool isHost_ = false;
};

} // namespace dwe::app
