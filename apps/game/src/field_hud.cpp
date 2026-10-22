#include "field_hud.hpp"

#include <dwe/screens/screen_settings.hpp>

namespace dwe::app {

FieldHud::FieldHud(ui::Ui& ui, const session::GameSession& session) : ui_(&ui), session_(&session) {
    for (const screens::ScreenSetting& label : screens::fieldHudLabelSettings()) {
        ui_->setSetting(label.name, label.value);
    }
}

void FieldHud::update(bool fieldRunning, std::optional<screens::WindowFraction> partnerHead) {
    publishStatus(fieldRunning);
    publishPartner(fieldRunning && session_->playing(), partnerHead);
}

void FieldHud::publishPartner(bool fieldRunning, std::optional<screens::WindowFraction> partnerHead) {
    const screens::FieldPartnerView view = screens::fieldPartnerViewOf(partnerHead, fieldRunning);
    if (publishedPartner_ == view) {
        return;
    }
    screens::publish(*ui_, screens::settingsOf(view), screens::FieldPartnerRevisionSetting);
    publishedPartner_ = view;
}

void FieldHud::publishStatus(bool fieldRunning) {
    screens::FieldHudView view = published_.value_or(screens::FieldHudView{});
    if (session_->playing()) {
        view = screens::fieldHudViewOf(session_->clock().time(), session_->partnerView(), fieldRunning);
    } else {
        view.shown = false;
    }
    if (published_ == view) {
        return;
    }
    screens::publish(*ui_, screens::settingsOf(view), screens::FieldHudRevisionSetting);
    published_ = view;
}

} // namespace dwe::app
