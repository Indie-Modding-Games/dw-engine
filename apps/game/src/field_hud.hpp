#pragma once

#include <dwe/screens/field_hud_view.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/ui/ui.hpp>

#include <optional>

namespace dwe::app {

// The field HUD's status panel: it publishes the game time and the partner's state (apps/screens:
// field_hud_view.hpp) to the HUD document, only when something it shows has changed.
class FieldHud {
public:
  // Publishes the labels, which never change.
  FieldHud(ui::Ui& ui, const session::GameSession& session);

  // Publishes the views when they changed; once per frame. `fieldRunning` is false while a menu or
  // a battle stops the field; `bubbleRuns` whether the partner's need bubble shows (the field
  // runs, or a training course does); `partnerBubble` is where the bubble settles in the window, if
  // it projects.
  void update(bool fieldRunning, bool bubbleRuns,
              std::optional<screens::WindowFraction> partnerBubble);

private:
  void publishStatus(bool fieldRunning);
  void publishPartner(bool fieldRunning, std::optional<screens::WindowFraction> partnerBubble);

  ui::Ui* ui_;
  const session::GameSession* session_;
  std::optional<screens::FieldHudView> published_;
  std::optional<screens::FieldPartnerView> publishedPartner_;
};

} // namespace dwe::app
