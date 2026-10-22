#include "free_exploration.hpp"

#include <dwe/core/log.hpp>
#include <dwe/game/field/field.hpp>

#include <cstddef>
#include <format>
#include <string>
#include <utility>
#include <variant>

#include "messages.hpp"

namespace dwe::app {

bool startFreeExploration(bool option, const std::optional<bool>& saved) {
    return option || saved.value_or(false);
}

FreeExploration::FreeExploration(FreeExplorationDesc desc, bool explore) : desc_{std::move(desc)} {
    desc_.scene->setExplore(explore);
    desc_.ui->setSetting(FreeExplorationSetting, explore);
    if (explore) {
        core::logInfo("{}", messages::FreeExplorationAtStart);
    }
}

bool FreeExploration::handle(const ui::UiCommand& command) {
    if (command.name == FreeExplorationSetting) {
        const bool* explore = std::get_if<bool>(&command.value);
        if (explore != nullptr) {
            desc_.scene->setExplore(*explore);
            desc_.settings->freeExploration = *explore;
            if (desc_.settingsFile) {
                saveUserSettings(*desc_.settingsFile, *desc_.settings);
            }
        }
        desc_.ui->setSetting(FreeExplorationSetting, desc_.scene->field().explores());
        return true;
    }
    if (command.name == ChooseDestinationCommand) {
        if (const double* place = std::get_if<double>(&command.value); place != nullptr && *place >= 0.0) {
            desc_.scene->chooseDestination(static_cast<std::size_t>(*place));
        }
        return true;
    }
    if (command.name == StayHereCommand) {
        desc_.scene->chooseDestination(std::nullopt);
        return true;
    }
    return false;
}

void FreeExploration::update() const {
    const auto& choice = desc_.scene->field().destinationChoice();
    const bool shown = desc_.ui->isVisible(DestinationChoiceDocument);
    if (!choice) {
        if (shown) {
            desc_.ui->hide(DestinationChoiceDocument);
        }
        return;
    }
    if (shown) {
        return;
    }
    desc_.ui->setSetting(ExplorationPlacesSetting, static_cast<double>(choice->places.size()));
    desc_.ui->setSetting(ExplorationStorySetting, choice->vanilla ? static_cast<double>(*choice->vanilla) : -1.0);
    for (std::size_t index = 0; index < choice->places.size(); ++index) {
        const std::string& map = choice->places[index].map;
        desc_.ui->setSetting(std::format("{}{}", ExplorationPlaceNamePrefix, index), desc_.scene->nameKeyOf(map));
        desc_.ui->setSetting(std::format("{}{}", ExplorationPlaceMapPrefix, index), map);
    }
    desc_.ui->show(DestinationChoiceDocument, true);
}

} // namespace dwe::app
