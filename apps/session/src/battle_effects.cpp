#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/effect_script.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/session/battle_effects.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::session {

namespace {

// The slot of the battle's effect sounds (battle_effect.c: BTL_playEFESound: playSound(8, id)).
constexpr std::int64_t EffectSoundSlot = 8;

// An extracted YAML file of the packs, parsed.
core::Result<core::YamlNode> readYaml(const assets::VirtualFileSystem& files,
                                      const std::string& id) {
  auto parsed = assets::AssetId::parse(id);
  if (!parsed) {
    return std::unexpected{parsed.error()};
  }
  auto bytes = files.read(*parsed);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  return core::parseYaml(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}

} // namespace

std::optional<assets::EffectScript> readMoveEffect(const assets::VirtualFileSystem& files,
                                                   std::string_view move) {
  auto effect = readYaml(files, std::format("effect/{}/effect", move));
  auto script = readYaml(files, std::format("effect/{}/script", move));
  if (!effect || !script) {
    core::logDebug("[port:battle] BTL_loadMoveEFE (battle_effect.c:2157): move={} has no effect",
                   move);
    return std::nullopt;
  }
  auto read = assets::readEffectScript(*effect, *script);
  if (!read) {
    core::logWarning("effect/{}: {}", move, read.error().message);
    return std::nullopt;
  }
  return std::move(*read);
}

BattleEffects::BattleEffects(const assets::VirtualFileSystem& files, game::story::SoundPort& sound)
    : files_(&files), sound_(&sound) {}

void BattleEffects::frame(const game::battle::BattleEvents& events,
                          const game::battle::MoveCatalog& moves) {
  using game::battle::EventKind;
  for (const game::battle::BattleEvent& event : events) {
    switch (event.kind) {
    case EventKind::AttackStarted:
      if (event.move) {
        startAttack(event.fighter, event.other, moves.at(*event.move).id);
      }
      break;
    case EventKind::AttackHit:
    case EventKind::AttackBlocked:
      // battle_main.c: BTL_tickAttackHits: the attack's effect registered the hit, and the
      // fighter it hit loses its own effect.
      if (event.other) {
        if (const auto found = running_.find(*event.other); found != running_.end()) {
          runner_.noteHit(found->second.slot);
        }
      }
      removeEffect(event.fighter);
      break;
    case EventKind::BattleEnded:
      end();
      break;
    default:
      break;
    }
  }
  runner_.tick();
  runner_.render();
}

void BattleEffects::end() {
  for (const auto& [fighter, running] : running_) {
    if (running.section) {
      runner_.stop(running.slot, *running.section);
    }
  }
  running_.clear();
  runner_.stopAll();
}

std::optional<std::size_t> BattleEffects::slotOf(const std::string& move) {
  if (const auto found = slots_.find(move); found != slots_.end()) {
    return found->second;
  }
  if (missing_.contains(move)) {
    return std::nullopt;
  }
  std::optional<assets::EffectScript> read = readMoveEffect(*files_, move);
  if (!read) {
    missing_.insert(move);
    return std::nullopt;
  }
  const std::size_t slot = runner_.load(std::move(*read));
  slots_.emplace(move, slot);
  moves_.emplace(slot, move);
  return slot;
}

void BattleEffects::startAttack(game::battle::FighterId fighter,
                                std::optional<game::battle::FighterId> target,
                                const std::string& move) {
  removeEffect(fighter);
  const std::optional<std::size_t> slot = slotOf(move);
  if (!slot) {
    return;
  }
  const std::optional<std::size_t> section = runner_.start(*slot, fighter, target);
  core::logDebug("[port:battle] BTL_startEFE (battle_effect.c:2181): fighter={} move={} slot={}",
                 fighter, move, *slot);
  running_[fighter] = Running{.slot = *slot, .section = section};
}

void BattleEffects::removeEffect(game::battle::FighterId fighter) {
  const auto found = running_.find(fighter);
  if (found == running_.end()) {
    return;
  }
  if (found->second.section) {
    runner_.stop(found->second.slot, *found->second.section);
  }
  running_.erase(found);
}

const std::string& BattleEffects::moveOf(std::size_t slot) const {
  return moves_.at(slot);
}

void BattleEffects::Sounds::play(std::size_t slot, std::int32_t sound) {
  core::logDebug("[port:audio] BTL_playEFESound (battle_effect.c:4006): move={} sound={}",
                 owner_->moveOf(slot), sound);
  owner_->sound_->playSound(EffectSoundSlot, sound);
}

std::optional<std::int64_t> BattleEffects::Sounds::playHeld(std::size_t slot, std::int32_t sound) {
  core::logDebug("[port:audio] BTL_playEFESound (battle_effect.c:4006): move={} sound={} held",
                 owner_->moveOf(slot), sound);
  return owner_->sound_->playHeldSound(EffectSoundSlot, sound);
}

void BattleEffects::Sounds::stop(std::int64_t voice) {
  owner_->sound_->stopHeldSound(voice);
}

} // namespace dwe::session
