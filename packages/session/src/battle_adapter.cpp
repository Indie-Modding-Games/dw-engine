#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/battle/messages.hpp>
#include <dwe/game/battle/strategies.hpp>
#include <dwe/game/inventory/item_effect.hpp>
#include <dwe/game/partner/effects.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/partner.hpp>
#include <dwe/game/save/autosave.hpp>
#include <dwe/game/save/sections.hpp>
#include <dwe/game/versus/protocol.hpp>
#include <dwe/ruleset/species_extra.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/game_hooks.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/messages.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace dwe::session {

namespace {

// The names mods see for a battle's kind and outcome (game_hooks.hpp).
std::string_view kindName(game::battle::BattleKind kind) {
  switch (kind) {
  case game::battle::BattleKind::Normal:
    return "normal";
  case game::battle::BattleKind::Tournament:
    return "tournament";
  case game::battle::BattleKind::Arena:
    return "arena";
  case game::battle::BattleKind::Versus:
    return "versus";
  }
  return "normal";
}

std::string_view outcomeName(game::battle::Outcome outcome) {
  switch (outcome) {
  case game::battle::Outcome::Victory:
    return "victory";
  case game::battle::Outcome::Fled:
    return "fled";
  case game::battle::Outcome::Defeat:
    return "defeat";
  }
  return "defeat";
}

using game::battle::BattleResult;
using game::battle::Outcome;
using game::partner::Stat;
using game::partner::StatBlock;
using game::save::AutosaveReason;
using messages::formatMessage;

// The roles of the tables in rules/<id>/ruleset.yaml.
constexpr std::string_view DigimonTableRole = "digimon";
constexpr std::string_view MoveTableRole = "moves";

constexpr unsigned SeedHalfBits = 32;

// The widest stats a raised partner reaches, and so a registered one (partner care caps).
constexpr std::int32_t MaxPool = 9999;
constexpr std::int32_t MaxStat = 999;
constexpr std::int32_t MaxDiscipline = 100;

// A versus fighter's stats as another machine sent them: each within what a raised partner has.
core::Result<void> checkVersusStats(const game::versus::VersusFighter& fighter) {
  struct Range {
    std::string_view name;
    std::int32_t value;
    std::int32_t low;
    std::int32_t high;
  };
  const std::array ranges{Range{"hp", fighter.hp, 1, MaxPool},
                          Range{"mp", fighter.mp, 0, MaxPool},
                          Range{"offense", fighter.offense, 0, MaxStat},
                          Range{"defense", fighter.defense, 0, MaxStat},
                          Range{"speed", fighter.speed, 0, MaxStat},
                          Range{"brains", fighter.brains, 0, MaxStat},
                          Range{"discipline", fighter.discipline, 0, MaxDiscipline}};
  for (const Range& range : ranges) {
    if (range.value < range.low || range.value > range.high) {
      return core::fail(formatMessage(messages::VersusStatOutside, fighter.name, range.name,
                                      range.value, range.low, range.high));
    }
  }
  return {};
}

// The battle's stat raises, in the partner's order (both go HP, MP, offense, defense, speed,
// brain).
StatBlock statBlockOf(const game::battle::StatValues& gains) {
  StatBlock block;
  for (std::size_t index = 0; index < gains.size(); ++index) {
    block[game::partner::AllStats.at(index)] = gains.at(index);
  }
  return block;
}

// What an item of the inventory does once thrown in battle (item.c, with GAME_STATE 1).
game::battle::ThrownItem thrownItemOf(const game::inventory::ItemEffect& effect) {
  game::battle::ThrownItem item;
  item.hp = effect.healing.hp;
  item.mp = effect.healing.mp;
  if (effect.revive) {
    item.reviveHpPercent = effect.revive->hpPercent;
  }
  item.curesStatus = effect.battleStatusCure.has_value();
  item.cureRings = effect.battleStatusCure == 1;
  item.protects = effect.protects;
  if (effect.battleBoost) {
    const game::inventory::BattleBoost& boost = *effect.battleBoost;
    item.boost = game::battle::StatBoost{.offense = boost.offense,
                                         .defense = boost.defense,
                                         .speed = boost.speed,
                                         .limitPercent = boost.limitPercent,
                                         .limitCap = boost.limitCap,
                                         .ignoresLimit = boost.ignoresLimit};
  }
  item.consciousOnly = effect.consciousOnly;
  // The medicines raise them only when they cure the sickness, which a throw never does.
  if (effect.healingParticles && !effect.healingParticles->onlyIfSicknessCured) {
    item.healingParticles = true;
    item.healingRing = effect.healingParticles->ring;
  }
  return item;
}

// The partner in battle as the inventory sees it: it never refuses, and the effect goes with the
// tamer's throw.
class ThrowTarget final : public game::inventory::ItemEffectTarget {
public:
  explicit ThrowTarget(game::battle::Battle& battle) : battle_(&battle) {}

  bool accepts(const game::inventory::ItemEffect& /*effect*/,
               game::inventory::UseContext /*context*/) override {
    return true;
  }

  void apply(const game::inventory::ItemEffect& effect,
             game::inventory::UseContext /*context*/) override {
    thrown_ = battle_->throwItem(thrownItemOf(effect));
  }

  [[nodiscard]] const core::Result<void>& thrown() const { return thrown_; }

private:
  game::battle::Battle* battle_;
  core::Result<void> thrown_;
};

game::partner::BattleResult partnerResultOf(Outcome outcome) {
  switch (outcome) {
  case Outcome::Victory:
    return game::partner::BattleResult::Won;
  case Outcome::Fled:
    return game::partner::BattleResult::Fled;
  case Outcome::Defeat:
    break;
  }
  return game::partner::BattleResult::Lost;
}

game::partner::BattleConsequences consequencesOf(const BattleResult& result) {
  return {.result = partnerResultOf(result.outcome),
          .injured = result.injured,
          .mood = {.happiness = result.care.happiness,
                   .discipline = result.care.discipline,
                   .tiredness = result.care.tiredness},
          .battles = result.care.battles,
          .foodChange = result.foodChange,
          .livesLost = result.livesLost,
          .elapsedMinutes = result.elapsedMinutes,
          .hourCrossed = result.hourCrossed,
          .lifetimeHoursLost = result.lifetimeHoursLost};
}

template <typename Table>
core::Result<Table> loadTable(const assets::VirtualFileSystem* files, const ruleset::Ruleset& rules,
                              std::string_view role,
                              core::Result<Table> (*load)(const assets::VirtualFileSystem&,
                                                          const assets::AssetId&)) {
  if (files == nullptr) {
    return core::fail(std::string{messages::BattleNeedsTables});
  }
  const assets::AssetId* id = rules.table(role);
  if (id == nullptr) {
    return core::fail(formatMessage(messages::NoMoveTable, role));
  }
  return load(*files, *id);
}

} // namespace

// Writes and reads the partner's moves.
class BattleAdapter::Serializer final : public game::save::SectionSerializer {
public:
  explicit Serializer(BattleAdapter& adapter) : adapter_(&adapter) {}

  [[nodiscard]] core::Result<core::YamlNode> write() const override {
    return toYaml(adapter_->moves_);
  }

  core::Result<void> read(const core::YamlNode& state) override {
    auto moves = partnerMovesFromYaml(state);
    if (!moves) {
      return std::unexpected{moves.error()};
    }
    adapter_->moves_ = std::move(*moves);
    return {};
  }

private:
  BattleAdapter* adapter_;
};

BattleAdapter::BattleAdapter(BattleAdapterDesc desc)
    : desc_(std::move(desc)), serializer_(std::make_unique<Serializer>(*this)) {}

BattleAdapter::~BattleAdapter() = default;

std::vector<std::string> BattleAdapter::learnMove(std::string_view move) {
  std::vector<std::string> learned;
  if (learn(moves_, move)) {
    learned.emplace_back(move);
  }
  if (catalog_ == nullptr) {
    return learned;
  }
  if (const auto ref = catalog_->find(move)) {
    for (const game::battle::MoveRef paired : catalog_->learnedWith(*ref)) {
      const std::string& id = catalog_->at(paired).id;
      if (learn(moves_, id)) {
        learned.push_back(id);
      }
    }
  }
  return learned;
}

ruleset::SectionChecks BattleAdapter::sectionChecks() const {
  return {{std::string{game::battle::SectionName}, game::battle::checkBattleSection},
          {std::string{PartnerMovesSection}, checkPartnerMovesSection}};
}

core::Result<void> BattleAdapter::loadTables(const ruleset::Ruleset& rules) {
  if (!desc_.digimon) {
    // With the ruleset's species patches (moves of the hidden content's partners).
    auto digimon = desc_.files == nullptr || rules.table(DigimonTableRole) == nullptr
                       ? loadTable<assets::DigimonTable>(desc_.files, rules, DigimonTableRole,
                                                         assets::loadDigimonTable)
                       : ruleset::loadPatchedDigimonTable(*desc_.files, rules);
    if (!digimon) {
      return std::unexpected{digimon.error()};
    }
    desc_.digimon = std::move(*digimon);
  }
  if (!desc_.moves) {
    auto moves =
        loadTable<assets::MoveTable>(desc_.files, rules, MoveTableRole, assets::loadMoveTable);
    if (!moves) {
      return std::unexpected{moves.error()};
    }
    desc_.moves = std::move(*moves);
  }
  return {};
}

core::Result<void> BattleAdapter::readRules(const ruleset::Ruleset& rules) {
  if (auto loaded = loadTables(rules); !loaded) {
    return loaded;
  }
  auto section = rules.requireSection(game::battle::SectionName);
  if (!section) {
    return std::unexpected{section.error()};
  }
  auto battleRules = game::battle::readBattleRules(**section);
  if (!battleRules) {
    return std::unexpected{battleRules.error()};
  }
  // A ruleset's species patches (the hidden content): the species whose effect points a new
  // partner's moves use (effects.original_types).
  auto extra = ruleset::readSpeciesExtra(rules);
  if (!extra) {
    return std::unexpected{extra.error()};
  }
  for (const auto& [id, patch] : extra->species) {
    if (patch.effectType) {
      battleRules->effects.originalTypes.insert_or_assign(id, *patch.effectType);
    }
  }
  if (!desc_.moves) {
    return core::fail(std::string{messages::BattleNeedsTables});
  }
  auto catalog = game::battle::MoveCatalog::build(*desc_.moves, *battleRules);
  if (!catalog) {
    return std::unexpected{catalog.error()};
  }
  partnerTypes_.clear();
  partnerVariants_.clear();
  for (const auto& [id, patch] : extra->species) {
    if (patch.partnerType) {
      partnerTypes_.insert_or_assign(
          id, static_cast<game::battle::DigimonType>(std::clamp(
                  *patch.partnerType, 0, static_cast<int>(game::battle::DigimonType::Virus))));
    }
    for (const auto& [move, change] : patch.partnerMoves) {
      const auto ref = catalog->find(move);
      if (!ref) {
        return core::fail(formatMessage(messages::UnknownPartnerMove, id, move));
      }
      partnerVariants_[id].insert_or_assign(
          move, catalog->addVariant(*ref, change.power, change.iframes));
    }
  }
  auto movesSection = rules.requireSection(PartnerMovesSection);
  if (!movesSection) {
    return std::unexpected{movesSection.error()};
  }
  auto moveRules = readPartnerMoveRules(**movesSection);
  if (!moveRules) {
    return std::unexpected{moveRules.error()};
  }
  section_ = **section;
  rules_ = std::make_unique<game::battle::BattleRules>(std::move(*battleRules));
  catalog_ = std::make_unique<game::battle::MoveCatalog>(std::move(*catalog));
  moveRules_ = *moveRules;
  return {};
}

core::Result<void> BattleAdapter::addSaveSections(game::save::SaveSections& sections) {
  return sections.add(std::string{MovesSaveName}, *serializer_, game::save::MigrationChain{});
}

const assets::DigimonRow* BattleAdapter::rowOf(std::string_view species) const {
  return desc_.digimon ? desc_.digimon->find(species) : nullptr;
}

std::optional<MoveSlot> BattleAdapter::finisherSlot(const assets::DigimonRow& row) const {
  // partner_impl.c: initializePartner: the last move of the species' list that is a finisher.
  for (std::size_t slot = row.moves.size(); slot-- > 0;) {
    const std::optional<std::string>& move = row.moves.at(slot);
    if (!move) {
      continue;
    }
    if (const auto ref = catalog_->find(*move); ref && catalog_->at(*ref).finisher) {
      return static_cast<MoveSlot>(slot);
    }
  }
  return std::nullopt;
}

core::Result<void> BattleAdapter::startNewGame(GameSession& session) {
  moves_ = {};
  if (!catalog_) {
    return core::fail(std::string{messages::BattleNotRead});
  }
  const std::string& species = session.partner().state().species;
  const assets::DigimonRow* row = rowOf(species);
  if (row == nullptr) {
    return core::fail(formatMessage(messages::UnknownPartnerSpecies, species));
  }
  // partner_impl.c: initializePartner: it equips and learns one technique, its position by species,
  // and equips its finisher.
  const MoveSlot first = moveRules_.newGameSlotOf(species);
  if (const std::optional<std::string>& move = row->moves.at(static_cast<std::size_t>(first))) {
    learn(moves_, *move);
    moves_.equipped.at(0) = first;
  }
  moves_.equipped.at(game::battle::FinisherSlot) = finisherSlot(*row);
  return {};
}

void BattleAdapter::routePartnerEvent(GameSession& /*session*/,
                                      const game::partner::PartnerEvent& event) {
  if (event.kind == game::partner::EventKind::Reborn) {
    // evolution.c: reincarnatePartner: moves[0] = 0x2E, the others 0xFF; the learned moves stay.
    moves_.equipped = {};
    moves_.equipped.at(0) = moveRules_.rebornSlot;
  }
}

void BattleAdapter::evolveMoves(
    const std::optional<std::string>& learned,
    const std::array<std::optional<MoveSlot>, game::battle::EquippedSlots>& equipped) {
  if (learned) {
    learn(moves_, *learned);
    const std::optional<game::battle::MoveRef> move =
        catalog_ ? catalog_->find(*learned) : std::nullopt;
    if (move) {
      for (const game::battle::MoveRef together : catalog_->learnedWith(*move)) {
        learn(moves_, catalog_->at(together).id);
      }
    }
  }
  moves_.equipped = equipped;
  const auto place = [&](std::size_t index) {
    const std::optional<MoveSlot>& slot = moves_.equipped.at(index);
    return slot ? std::to_string(*slot) : std::string{"-"};
  };
  core::logDebug("[port:evolution] EVL_applyEvolution (evl.c:1449): learns {}, equips {} {} {} {}",
                 learned.value_or("nothing"), place(0), place(1), place(2), place(3));
}

core::Result<game::battle::Combatant>
BattleAdapter::partnerCombatant(const GameSession& session) const {
  const game::partner::PartnerState& partner = session.partner().state();
  const assets::DigimonRow* row = rowOf(partner.species);
  if (row == nullptr) {
    return core::fail(formatMessage(messages::UnknownPartnerSpecies, partner.species));
  }
  game::battle::Combatant fighter = game::battle::combatantOf(*row);
  if (const auto type = partnerTypes_.find(partner.species); type != partnerTypes_.end()) {
    fighter.type = type->second;
  }
  if (const auto variants = partnerVariants_.find(partner.species);
      variants != partnerVariants_.end()) {
    fighter.moveVariants = variants->second;
  }
  const StatBlock& base = partner.stats.base;
  fighter.stats = {.maxHp = base[Stat::Hp],
                   .maxMp = base[Stat::Mp],
                   .offense = base[Stat::Offense],
                   .defense = base[Stat::Defense],
                   .speed = base[Stat::Speed],
                   .brain = base[Stat::Brain]};
  fighter.hp = partner.stats.currentHp;
  fighter.mp = partner.stats.currentMp;
  // battle_setup.c: BTL_initializeCombat fights with the partner's own charge mode (0 unless a
  // mod changed it); the battle hands it back untouched, as BTL_deinitializeCombat restores it.
  fighter.chargeMode = game::battle::chargeModeOf(partner.stats.chargeMode);
  // btl.c: a position the species has no move at is left empty.
  for (std::size_t place = 0; place < moves_.equipped.size(); ++place) {
    if (const std::optional<MoveSlot> slot = moves_.equipped.at(place)) {
      fighter.moves.at(place) = row->moves.at(static_cast<std::size_t>(*slot));
    }
  }
  return fighter;
}

core::Result<game::battle::Battle> BattleAdapter::start(GameSession& session,
                                                        BattleRequest request) {
  if (!rules_ || !section_) {
    return core::fail(std::string{messages::BattleNotRead});
  }
  auto partner = partnerCombatant(session);
  if (!partner) {
    return std::unexpected{partner.error()};
  }
  partner->position = request.partner;
  auto strategies = game::battle::createStrategies(*section_);
  if (!strategies) {
    return std::unexpected{strategies.error()};
  }
  // What mods hear once the battle started (battle.started), taken before the request moves.
  const std::string firstEnemy =
      request.enemies.empty() ? std::string{} : request.enemies[0].species;
  const std::string map = request.map;
  const BattleStartedEvent started{.kind = kindName(request.kind),
                                   .map = map,
                                   .enemies = static_cast<std::int64_t>(request.enemies.size()),
                                   .firstEnemy = firstEnemy,
                                   .scripted = request.scripted};
  const game::partner::PartnerState& state = session.partner().state();
  game::battle::BattleSetup setup{
      .partner = std::move(*partner),
      .condition = {.discipline = state.mood.discipline,
                    .tiredness = state.mood.tiredness,
                    .sick = state.conditions.has(game::partner::Condition::Sick),
                    .injured = state.conditions.has(game::partner::Condition::Injured),
                    .knownMoves = moves_.learned},
      .enemies = std::move(request.enemies),
      .scripted = request.scripted,
      .tamerless = request.tamerless,
      .map = std::move(request.map),
      .startMinute = session.clock().time().minute,
      .tamer = request.tamer,
      .kind = request.kind,
      .timeLimit = request.timeLimit,
      .recruitOnVictory = std::move(request.recruitOnVictory),
      .versus = std::nullopt};
  ruleset::Random& random = session.random();
  const std::uint64_t high = random.next();
  const std::uint64_t seed = (high << SeedHalfBits) | random.next();
  auto battle = game::battle::Battle::start(std::move(setup), *rules_, *catalog_,
                                            std::move(*strategies), seed);
  if (battle && originalRolls_) {
    battle->useOriginalRolls(*originalRolls_);
  }
  if (battle) {
    session.hooks().battleStarted(started);
  }
  return battle;
}

core::Result<game::battle::Combatant>
BattleAdapter::versusCombatant(const game::versus::VersusFighter& fighter) const {
  if (!catalog_) {
    return core::fail(std::string{messages::BattleNotRead});
  }
  const assets::DigimonRow* row = rowOf(fighter.species);
  if (row == nullptr) {
    return core::fail(formatMessage(messages::VersusUnknownSpecies, fighter.name, fighter.species));
  }
  if (auto checked = checkVersusStats(fighter); !checked) {
    return std::unexpected{checked.error()};
  }
  game::battle::Combatant combatant = game::battle::combatantOf(*row);
  if (const auto type = partnerTypes_.find(fighter.species); type != partnerTypes_.end()) {
    combatant.type = type->second;
  }
  if (const auto variants = partnerVariants_.find(fighter.species);
      variants != partnerVariants_.end()) {
    combatant.moveVariants = variants->second;
  }
  // vs_scene.c: VS_loadFighterEntities: the record's stats, full HP and MP, charge mode 0.
  combatant.stats = {.maxHp = fighter.hp,
                     .maxMp = fighter.mp,
                     .offense = fighter.offense,
                     .defense = fighter.defense,
                     .speed = fighter.speed,
                     .brain = fighter.brains};
  combatant.hp = fighter.hp;
  combatant.mp = fighter.mp;
  combatant.chargeMode = game::battle::ChargeMode::Normal;
  for (std::size_t place = 0; place < fighter.techniques.size(); ++place) {
    const std::optional<std::string>& move = fighter.techniques.at(place);
    if (!move) {
      continue;
    }
    if (!catalog_->find(*move)) {
      return core::fail(formatMessage(messages::VersusUnknownMove, fighter.name, *move));
    }
    combatant.moves.at(place) = move;
  }
  // moves[3] = VS_getFirstSpecialMove(species): the species' finisher, as a raised partner's.
  if (const std::optional<MoveSlot> slot = finisherSlot(*row)) {
    combatant.moves.at(game::battle::FinisherSlot) = row->moves.at(static_cast<std::size_t>(*slot));
  }
  return combatant;
}

core::Result<game::battle::Battle>
BattleAdapter::startVersus(const game::versus::VersusFighter& host,
                           const game::versus::VersusFighter& guest,
                           const VersusPlacement& placement, std::uint64_t seed) const {
  if (!rules_ || !section_) {
    return core::fail(std::string{messages::BattleNotRead});
  }
  if (!rules_->versus) {
    return core::fail(std::string{messages::VersusNotRead});
  }
  auto first = versusCombatant(host);
  if (!first) {
    return std::unexpected{first.error()};
  }
  auto second = versusCombatant(guest);
  if (!second) {
    return std::unexpected{second.error()};
  }
  first->position = placement.host;
  second->position = placement.guest;
  auto strategies = game::battle::createStrategies(*section_);
  if (!strategies) {
    return std::unexpected{strategies.error()};
  }
  std::vector<game::battle::Combatant> enemies;
  enemies.push_back(std::move(*second));
  // VS_DISCIPLINE[0] and [1]: each side obeys with its own record's discipline.
  game::battle::BattleSetup setup{
      .partner = std::move(*first),
      .condition = {.discipline = host.discipline,
                    .tiredness = 0,
                    .sick = false,
                    .injured = false,
                    .knownMoves = {}},
      .enemies = std::move(enemies),
      .scripted = true,
      .tamerless = false,
      .map = placement.map,
      .startMinute = 0,
      .tamer = placement.bystander,
      .kind = game::battle::BattleKind::Versus,
      .timeLimit = rules_->versus->timeLimitSeconds,
      .recruitOnVictory = std::nullopt,
      .versus = game::battle::VersusSetup{.rivalDiscipline = guest.discipline}};
  return game::battle::Battle::start(std::move(setup), *rules_, *catalog_, std::move(*strategies),
                                     seed);
}

void BattleAdapter::giveRewards(GameSession& session, const BattleResult& result,
                                BattleAftermath& aftermath) const {
  aftermath.drops = result.drops;
  if (desc_.inventory == nullptr) {
    return;
  }
  game::inventory::Inventory& bag = desc_.inventory->inventory();
  bag.addMoney(result.bits);
  // A lost tournament match takes nothing (std_main.c: STD_deinitializeCombat).
  if (result.outcome == Outcome::Defeat && result.kind != game::battle::BattleKind::Tournament) {
    aftermath.itemsLost = desc_.inventory->loseOnDefeat(session);
  }
}

core::Result<BattleAftermath> BattleAdapter::finish(GameSession& session,
                                                    const BattleResult& result) {
  if (!rules_) {
    return core::fail(std::string{messages::BattleNotRead});
  }
  game::partner::Partner& partner = session.partner();
  BattleAftermath aftermath;
  // battle_ui.c: battleStatsGainsAndDrops: the HP and MP it ends with, then the raises.
  game::partner::PartnerStats stats = partner.state().stats;
  stats.currentHp = result.partnerHp;
  stats.currentMp = result.partnerMp;
  partner.setStats(stats);
  aftermath.statsBefore = stats.base;
  aftermath.statGains = partner.addStatGains(statBlockOf(result.statGains));
  partner.applyBattle(consequencesOf(result), session.clock());
  for (const std::string& move : result.learnedMoves) {
    if (learn(moves_, move)) {
      aftermath.learnedMoves.push_back(move);
    }
  }
  giveRewards(session, result, aftermath);
  aftermath.recruitJoined = result.recruitJoined;
  session.requestAutosave(AutosaveReason::BattleEnd);
  if (aftermath.recruitJoined) {
    session.requestAutosave(AutosaveReason::RecruitJoin);
  }
  if (result.kind != game::battle::BattleKind::Normal) {
    session.requestAutosave(AutosaveReason::TournamentResult);
  }
  session.hooks().battleEnded({.kind = kindName(result.kind),
                               .outcome = outcomeName(result.outcome),
                               .recruit = aftermath.recruitJoined.value_or(std::string{})});
  session.afterSystemCommand();
  return aftermath;
}

core::Result<game::inventory::UseResult> BattleAdapter::throwItem(game::battle::Battle& battle,
                                                                  assets::ItemHandle item) const {
  if (desc_.inventory == nullptr) {
    return core::fail(std::string{messages::BattleWithoutBag});
  }
  if (!battle.bagAvailable()) {
    return core::fail(std::string{game::battle::messages::BagUnavailable});
  }
  ThrowTarget target{battle};
  auto used = desc_.inventory->useInBattle(item, target);
  if (used && !target.thrown()) {
    return std::unexpected{target.thrown().error()};
  }
  return used;
}

core::Result<void> BattleAdapter::equip(const GameSession& session, std::size_t place,
                                        std::optional<MoveSlot> slot) {
  if (place >= moves_.equipped.size()) {
    return core::fail(formatMessage(messages::MovePlaceOutside, place, moves_.equipped.size()));
  }
  if (slot) {
    const std::string& species = session.partner().state().species;
    const assets::DigimonRow* row = rowOf(species);
    if (row == nullptr) {
      return core::fail(formatMessage(messages::UnknownPartnerSpecies, species));
    }
    const std::optional<std::string>* move = nullptr;
    if (*slot >= 0 && std::cmp_less(*slot, row->moves.size())) {
      move = &row->moves.at(static_cast<std::size_t>(*slot));
    }
    if (move == nullptr || !move->has_value()) {
      return core::fail(formatMessage(messages::NoMoveAtSlot, species, *slot));
    }
    // The menu offers only the techniques it learned (utils2.c: hasMove); the finisher comes with
    // the species.
    if (place != game::battle::FinisherSlot && !knows(moves_, **move)) {
      return core::fail(formatMessage(messages::MoveNotLearned, **move));
    }
  }
  moves_.equipped.at(place) = slot;
  return {};
}

TechniqueToggle BattleAdapter::toggleTechnique(const GameSession& session, MoveSlot slot) {
  const auto* species = rowOf(session.partner().state().species);
  return species && catalog_ ? session::toggleTechnique(moves_, *species, *catalog_, slot)
                             : TechniqueToggle::Unavailable;
}

bool BattleAdapter::canFinishTechniqueEdit(const GameSession& session) const {
  const auto* species = rowOf(session.partner().state().species);
  return species && catalog_ && session::canFinishTechniqueEdit(moves_, *species, *catalog_);
}

const game::battle::BattleRules& BattleAdapter::rules() const {
  return *rules_;
}

const game::battle::MoveCatalog& BattleAdapter::catalog() const {
  return *catalog_;
}

} // namespace dwe::session
