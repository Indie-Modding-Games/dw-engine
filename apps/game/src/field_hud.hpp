#pragma once

#include <dwe/screens/field_hud_view.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/ui/ui.hpp>

#include <optional>

namespace dwe::app {

// The field HUD's status panel: it publishes the game time and the partner's state (apps/screens: field_hud_view.hpp)
// to the HUD document, only when something it shows has changed.
class FieldHud {
public:
    // Publishes the labels, which never change.
    FieldHud(ui::Ui& ui, const session::GameSession& session);

    // Publishes the view when it changed; once per frame. `fieldRunning` is false while a menu or a battle stops the
    // field.
    void update(bool fieldRunning);

private:
    ui::Ui* ui_;
    const session::GameSession* session_;
    std::optional<screens::FieldHudView> published_;
};

} // namespace dwe::app
