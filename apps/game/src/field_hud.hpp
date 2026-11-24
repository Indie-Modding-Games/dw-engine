#pragma once

#include <dwe/screens/field_hud_view.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::app {

// The need meters (an engine improvement; it changes no rule): hunger, the toilet and tiredness
// as bars on the field that fill as each need nears. A UI setting the HUD reads and the command of
// the same name, saved with the user's settings (need_meters: true, false), on when not chosen yet.
inline constexpr std::string_view NeedMetersSetting = "need_meters";

// The field HUD's status panel: it publishes the game time and the partner's state
// (packages/screens: field_hud_view.hpp) to the HUD document, only when something it shows has
// changed.
class FieldHud {
public:
  // Publishes the labels, which never change.
  FieldHud(ui::Ui& ui, const session::GameSession& session);

  // Publishes the views when they changed; once per frame. `fieldRunning` is false while a menu or
  // a battle stops the field; `bubbleRuns` whether the partner's need bubble shows (the field
  // runs, or a training course does); `partnerBubble` is where the bubble settles in the window, if
  // it projects; `evolvingFrame` the evolving bubble's frame (EvolutionScene::bubbleFrame), 0 for
  // none.
  void update(bool fieldRunning, bool bubbleRuns,
              std::optional<screens::WindowFraction> partnerBubble, std::int32_t evolvingFrame = 0);

private:
  void publishStatus(bool fieldRunning);
  void publishPartner(bool fieldRunning, std::optional<screens::WindowFraction> partnerBubble,
                      std::int32_t evolvingFrame);

  ui::Ui* ui_;
  const session::GameSession* session_;
  std::optional<screens::FieldHudView> published_;
  std::optional<screens::FieldPartnerView> publishedPartner_;
};

} // namespace dwe::app
