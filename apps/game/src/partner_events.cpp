#include "partner_events.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>

#include <string>

#include "messages.hpp"

namespace dwe::app {

using game::partner::EventKind;

void showPartnerEvents(std::span<const game::partner::PartnerEvent> events,
                       const session::GameSession& session, FieldScene& scene) {
  for (const game::partner::PartnerEvent& event : events) {
    switch (event.kind) {
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

} // namespace dwe::app
