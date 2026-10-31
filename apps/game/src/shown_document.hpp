#pragma once

#include <dwe/ui/ui.hpp>

#include <string>
#include <string_view>

namespace dwe::app {

// A screen's document while the screen shows it: hidden again when the screen goes, so a game that
// ends with the screen open (a save loaded over it, the title) leaves no box on screen that no one
// answers.
class ShownDocument {
public:
  ShownDocument(ui::Ui& ui, std::string_view name) : ui_(&ui), name_(name) {}
  ShownDocument(const ShownDocument&) = delete;
  ShownDocument& operator=(const ShownDocument&) = delete;
  ShownDocument(ShownDocument&&) = delete;
  ShownDocument& operator=(ShownDocument&&) = delete;
  ~ShownDocument() { hide(); }

  void show(bool modal) {
    if (!shown_) {
      ui_->show(name_, modal);
      shown_ = true;
    }
  }
  void hide() {
    if (shown_) {
      ui_->hide(name_);
      shown_ = false;
    }
  }
  [[nodiscard]] bool shown() const { return shown_; }

private:
  ui::Ui* ui_;
  std::string name_;
  bool shown_ = false;
};

} // namespace dwe::app
