#include <dwe/core/log.hpp>

#include <algorithm>
#include <optional>
#include <set>
#include <vector>

#include "coop_machine_spot.hpp"
#include "coop_snapshot.hpp"
#include "coop_snapshot_interpolation.hpp"
#include "coop_world.hpp"
#include "field_scene.hpp"

// The co-op shared world's side that needs the scene: kept out of coop_world.cpp so its logic
// tests against a bare field.

namespace dwe::app {

namespace coop = game::coop;

void applyCoopMove(FieldScene& scene, CoopGuestWorld& guest, bool ownDeath) {
  if (auto move = guest.apply(scene.field(), scene.movingCoopSelf() || ownDeath)) {
    scene.moveCoopSelf(move->map, move->exit, move->at, move->reload);
  }
}

void presentCoopPlayers(FieldScene& scene, const PresentedCoopWorld* world,
                        const CoopGuestWorld& guest, std::string_view hostPartner,
                        std::span<const coop::PlayerId> fighting) {
  const game::field::Field& field = scene.field();
  // Positions of another map are never drawn on this one (#356).
  const bool sameMap = world != nullptr && world->mapId == field.mapId() &&
                       !field.mapChangePending() && !scene.movingCoopSelf();
  std::set<coop::PlayerId> shown;
  if (sameMap) {
    const auto entityOf = [&](coop::PlayerId owner,
                              coop::EntityRole role) -> const PresentedCoopEntity* {
      const auto found = std::ranges::find_if(world->entities, [&](const auto& entity) {
        return entity.owner == owner && entity.role == role;
      });
      return found != world->entities.end() ? &*found : nullptr;
    };
    const auto stateOf = [&](coop::PlayerId owner,
                             coop::EntityRole role) -> std::optional<game::field::CharacterState> {
      const PresentedCoopEntity* found = entityOf(owner, role);
      if (found == nullptr) {
        return std::nullopt;
      }
      return game::field::CharacterState{
          .position = found->position, .yaw = found->yaw, .motion = sceneMotionOf(found->motion)};
    };
    const auto clipOf = [&](coop::PlayerId owner,
                            coop::EntityRole role) -> std::optional<game::field::PlayedClip> {
      const PresentedCoopEntity* found = entityOf(owner, role);
      return found != nullptr ? playedClipOf(found->action) : std::nullopt;
    };
    // Another partner on the same machine is drawn beside them (coop-training.md T8).
    std::vector<glm::vec3> taken;
    if (field.scriptedClipOf(field.partnerEntity())) {
      taken.push_back(field.partner().position);
    }
    struct Drawn {
      std::optional<game::field::CharacterState> player;
      std::optional<game::field::CharacterState> partner;
    };
    const auto drawnOf = [&](coop::PlayerId owner) {
      Drawn drawn{.player = stateOf(owner, coop::EntityRole::Player),
                  .partner = stateOf(owner, coop::EntityRole::Partner)};
      if (drawn.partner && clipOf(owner, coop::EntityRole::Partner)) {
        const MachinePair pair =
            machinePairBeside(drawn.player ? drawn.player->position : drawn.partner->position,
                              drawn.partner->position, drawn.partner->yaw, taken);
        if (drawn.player) {
          drawn.player->position = pair.player;
        }
        drawn.partner->position = pair.partner;
        taken.push_back(pair.partner);
      }
      return drawn;
    };
    for (const PresentedCoopEntity& entity : world->entities) {
      const coop::PlayerId owner = entity.owner;
      if (entity.role != coop::EntityRole::Player || owner == guest.self() ||
          !guest.inSession(owner) || shown.contains(owner)) {
        continue;
      }
      std::string partner = guest.partnerOf(owner);
      if (partner.empty() && owner == coop::HostPlayerId) {
        partner = std::string{hostPartner};
      }
      Drawn drawn = drawnOf(owner);
      if (auto presented = scene.presentCoopPlayer(owner, drawn.player, drawn.partner, partner,
                                                   clipOf(owner, coop::EntityRole::Player),
                                                   clipOf(owner, coop::EntityRole::Partner));
          !presented) {
        core::logWarning("coop: could not present player {}: {}", owner, presented.error().message);
      }
      shown.insert(owner);
    }
  }
  for (const coop::PlayerId id : scene.coopPlayers()) {
    if (shown.contains(id)) {
      continue;
    }
    if (guest.inSession(id)) {
      (void)scene.presentCoopPlayer(id, std::nullopt);
    } else if (!guest.keepsBodies(id, fighting)) {
      scene.removeCoopPlayer(id);
    }
  }
}

void applyCoopMapEdits(FieldScene& scene, std::span<const coop::WorldEdit> edits) {
  for (const coop::WorldEdit& edit : edits) {
    if (edit.kind == coop::WorldEditKind::MapObject) {
      scene.setMapObjectFlag(edit.key, edit.value);
    } else if (edit.kind == coop::WorldEditKind::MapBlock) {
      scene.blockCells({.column = edit.key, .row = edit.value}, edit.width, edit.height);
    }
  }
}

} // namespace dwe::app
