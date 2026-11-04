#include "partner_events.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/screens/name_keys.hpp>

#include <algorithm>
#include <string>

#include "messages.hpp"

namespace dwe::app {

using game::partner::EventKind;

void showPartnerEvents(std::span<const game::partner::PartnerEvent> events,
                       const session::GameSession& session, FieldScene& scene) {
  for (const game::partner::PartnerEvent& event : events) {
    switch (event.kind) {
    case EventKind::GotSick:
      // Its notice is the award box's (AwardScreen::noteSickness).
      break;
    case EventKind::Evolved:
    case EventKind::Reborn: {
      const std::string species = session.partnerView().species;
      if (auto shown = scene.showPartnerSpecies(species); !shown) {
        core::logWarning("{}", core::formatMessage(messages::PartnerModelNotShown, species,
                                                   shown.error().message));
      }
      break;
    }
    default:
      core::logDebug("{}", core::formatMessage(messages::PartnerEventNotShown,
                                               static_cast<int>(event.kind), event.value));
      break;
    }
  }
}

bool fellSick(std::span<const game::partner::PartnerEvent> events) {
  return std::ranges::any_of(events, [](const game::partner::PartnerEvent& event) {
    return event.kind == EventKind::GotSick;
  });
}

std::string partnerNameOf(const session::GameSession& session,
                          const assets::DigimonTable* species) {
  if (std::string name = session.partnerView().nickname; !name.empty()) {
    return name;
  }
  const screens::NameKeys names{species, nullptr, nullptr};
  return localization::tr(names.species(session.partnerView().species));
}

} // namespace dwe::app
