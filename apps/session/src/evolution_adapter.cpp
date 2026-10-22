#include <dwe/core/log.hpp>
#include <dwe/game/evolution/rules.hpp>
#include <dwe/game/evolution/strategies.hpp>
#include <dwe/game/partner/partner.hpp>
#include <dwe/game/save/sections.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/story_adapter.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace dwe::session {

namespace {

using game::evolution::BaseStats;
using game::evolution::EvolutionInputs;
using game::evolution::EvolutionResult;
using game::evolution::EvolutionTables;
using game::partner::EventKind;
using game::partner::Stat;
using game::partner::StatBlock;
using messages::formatMessage;

// Whether an event script runs when the rules are asked (IS_SCRIPT_PAUSED is 1 while none does): only the script
// routines (back from the virus form, a script's evolution) run inside one.
constexpr bool NoScriptRunning = false;
constexpr bool ScriptRunning = true;

BaseStats baseStatsOf(const StatBlock& stats) {
    return {.hp = stats[Stat::Hp],
            .mp = stats[Stat::Mp],
            .offense = stats[Stat::Offense],
            .defense = stats[Stat::Defense],
            .speed = stats[Stat::Speed],
            .brains = stats[Stat::Brain]};
}

StatBlock statBlockOf(const BaseStats& stats) {
    StatBlock block;
    block[Stat::Hp] = stats.hp;
    block[Stat::Mp] = stats.mp;
    block[Stat::Offense] = stats.offense;
    block[Stat::Defense] = stats.defense;
    block[Stat::Speed] = stats.speed;
    block[Stat::Brain] = stats.brains;
    return block;
}

// Each stat at most its limit (evl.c: EVL_clampBaseStats: 9999 HP and MP, 999 the rest, from care.limits).
StatBlock clampedToLimits(StatBlock stats, const game::partner::CareLimits& limits) {
    for (const Stat stat : game::partner::AllStats) {
        const bool hpOrMp = stat == Stat::Hp || stat == Stat::Mp;
        stats[stat] = std::min(stats[stat], hpOrMp ? limits.hpMpMax : limits.statMax);
    }
    return stats;
}

// The evolution's stats: the virus form's rebuilt ones or the gains, with HP and MP within the new maximums.
void applyStats(GameSession& session, const EvolutionResult& result) {
    game::partner::Partner& partner = session.partner();
    if (result.restoredStats) {
        // Back from the virus form: set, then each stat at its limit, with no cap per gain (decision of 2026-10-20).
        game::partner::PartnerStats stats = partner.state().stats;
        stats.base = clampedToLimits(statBlockOf(*result.restoredStats), session.partnerRules().care.limits);
        partner.setStats(stats);
    } else {
        partner.addStatGains(statBlockOf(result.statChange));
    }
    // evl.c: EVL_applyEvolution keeps the current HP and MP within the new maximums.
    game::partner::PartnerStats stats = partner.state().stats;
    stats.currentHp = std::min(stats.currentHp, stats.base[Stat::Hp]);
    stats.currentMp = std::min(stats.currentMp, stats.base[Stat::Mp]);
    partner.setStats(stats);
}

} // namespace

// Writes and reads the evolution state with the module's own YAML form, by the tables' form ids.
class EvolutionAdapter::Serializer final : public game::save::SectionSerializer {
public:
    explicit Serializer(EvolutionAdapter& adapter) : adapter_(&adapter) {}

    [[nodiscard]] core::Result<core::YamlNode> write() const override {
        if (!adapter_->evolution_) {
            return core::fail(std::string{messages::EvolutionNotRead});
        }
        // The raised forms are the story's when there is one; the section keeps a copy for saves read without it.
        game::evolution::EvolutionState state = adapter_->state_;
        if (adapter_->desc_.story != nullptr) {
            state.raised = adapter_->desc_.story->raisedSpecies();
        }
        return game::evolution::toYaml(state, adapter_->evolution_->tables());
    }

    core::Result<void> read(const core::YamlNode& state) override {
        if (!adapter_->evolution_) {
            return core::fail(std::string{messages::EvolutionNotRead});
        }
        auto read = game::evolution::fromYaml(state, adapter_->evolution_->tables());
        if (!read) {
            return std::unexpected{read.error()};
        }
        adapter_->state_ = std::move(*read);
        adapter_->evolutions_.clear();
        return {};
    }

private:
    EvolutionAdapter* adapter_;
};

EvolutionAdapter::EvolutionAdapter(EvolutionAdapterDesc desc)
    : desc_(std::move(desc)), serializer_(std::make_unique<Serializer>(*this)) {}

EvolutionAdapter::~EvolutionAdapter() = default;

ruleset::SectionChecks EvolutionAdapter::sectionChecks() const {
    return {{std::string{game::evolution::EvolutionSection}, game::evolution::checkEvolutionSection}};
}

core::Result<void> EvolutionAdapter::readRules(const ruleset::Ruleset& rules) {
    if (!desc_.tables) {
        if (desc_.files == nullptr) {
            return core::fail(std::string{messages::EvolutionNeedsTables});
        }
        auto tables = EvolutionTables::load(*desc_.files, rules);
        if (!tables) {
            return std::unexpected{tables.error()};
        }
        desc_.tables = std::move(*tables);
    }
    auto section = rules.requireSection(game::evolution::EvolutionSection);
    if (!section) {
        return std::unexpected{section.error()};
    }
    auto evolutionRules = game::evolution::readEvolutionRules(**section);
    if (!evolutionRules) {
        return std::unexpected{evolutionRules.error()};
    }
    auto selection = game::evolution::selectionStrategies().create(**section, game::evolution::SelectionKey);
    if (!selection) {
        return std::unexpected{selection.error()};
    }
    auto scoring = game::evolution::scoringStrategies().create(**section, game::evolution::ScoringKey);
    if (!scoring) {
        return std::unexpected{scoring.error()};
    }
    auto built = game::evolution::Evolution::create(std::move(*evolutionRules), *desc_.tables, std::move(*selection),
                                                    std::move(*scoring));
    if (!built) {
        return std::unexpected{built.error()};
    }
    const game::evolution::Evolution& evolution = evolution_.emplace(std::move(*built));
    if (!maps_ && desc_.files != nullptr) {
        // Packs without a map table (tests' stand-in packs) leave the map unknown to the rules.
        if (auto maps = assets::loadMapTable(*desc_.files); maps) {
            maps_ = std::move(*maps);
        }
    }
    warnUnknownMaps(evolution.rules().specials);
    return {};
}

void EvolutionAdapter::warnUnknownMaps(const game::evolution::SpecialRules& specials) const {
    if (!maps_) {
        return;
    }
    for (const auto* rules : {&specials.wakeUp, &specials.praiseScold}) {
        for (const game::evolution::SpecialRule& rule : *rules) {
            for (const game::evolution::SpecialRequirement& need : rule.requirements) {
                if (need.field == game::evolution::SpecialField::Map && maps_->find(need.mapId) == nullptr) {
                    core::logWarning("{}", formatMessage(messages::UnknownEvolutionMap, rule.target, need.mapId));
                }
            }
        }
    }
}

core::Result<void> EvolutionAdapter::addSaveSections(game::save::SaveSections& sections) {
    return sections.add(std::string{EvolutionSaveName}, *serializer_, game::save::MigrationChain{});
}

core::Result<void> EvolutionAdapter::startNewGame(GameSession& session) {
    // main.c: main marks the first partner as raised (setDigimonRaised); the item mark starts false.
    state_ = {};
    evolutions_.clear();
    if (!evolution_) {
        return core::fail(std::string{messages::EvolutionNotRead});
    }
    if (const auto species = evolution_->tables().find(session.partner().state().species)) {
        raise(*species);
    }
    return {};
}

void EvolutionAdapter::tick(GameSession& session, const ruleset::ClockAdvance& advance) {
    if (advance.hours <= 0 || !evolution_ || !session.partner().alive()) {
        return;
    }
    const auto inputs = inputsOf(session, NoScriptRunning);
    if (!inputs) {
        return;
    }
    // partner_impl.c: tickConditions checks the virus bar after the natural evolution and its target wins: when
    // the bar filled in this tick, the routed VirusBarFull evolves the partner and the natural check waits.
    if (std::ranges::any_of(session.partner().pendingEvents(), [](const game::partner::PartnerEvent& event) {
            return event.kind == EventKind::VirusBarFull;
        })) {
        return;
    }
    // partner_impl.c: tickConditions checks once a game hour.
    syncRaised(desc_.story, state_);
    const game::evolution::NaturalCheck check =
        evolution_->checkNatural(*inputs, state_, advance.hours, session.random());
    if (check.evolution) {
        apply(session, *check.evolution);
        return;
    }
    game::partner::EvolutionCounters counters = session.partner().state().evolution;
    counters.hours = check.evolutionTimer;
    session.partner().setEvolutionCounters(counters);
}

void EvolutionAdapter::routePartnerEvent(GameSession& session, const game::partner::PartnerEvent& event) {
    if (!evolution_) {
        return;
    }
    switch (event.kind) {
    case EventKind::VirusBarFull:
        if (const auto inputs = inputsOf(session, NoScriptRunning)) {
            syncRaised(desc_.story, state_);
            if (auto result = evolution_->fillVirusBar(*inputs, state_)) {
                apply(session, *result);
            }
        }
        break;
    case EventKind::Slept:
        if (const auto inputs = inputsOf(session, NoScriptRunning)) {
            syncRaised(desc_.story, state_);
            if (auto result = evolution_->wakeUp(*inputs, state_, session.random())) {
                apply(session, *result);
            }
        }
        break;
    case EventKind::Reborn:
        // evolution.c: reincarnatePartner marks the new baby as raised.
        if (const auto species = evolution_->tables().find(session.partner().state().species)) {
            raise(*species);
        }
        break;
    default:
        break;
    }
}

void EvolutionAdapter::afterPraiseOrScold(GameSession& session, const PraiseOrScold& event) {
    if (!evolution_) {
        return;
    }
    const auto inputs = inputsOf(session, NoScriptRunning);
    if (!inputs) {
        return;
    }
    const game::evolution::PraiseScold praised{
        .scolded = event.scolded, .disciplineBefore = event.disciplineBefore, .happinessBefore = event.happinessBefore};
    syncRaised(desc_.story, state_);
    if (auto result = evolution_->praiseOrScold(*inputs, state_, praised, session.random())) {
        apply(session, *result);
    }
}

bool EvolutionAdapter::useItem(GameSession& session, std::string_view item) {
    if (!evolution_) {
        return false;
    }
    const auto inputs = inputsOf(session, NoScriptRunning);
    if (!inputs) {
        return false;
    }
    syncRaised(desc_.story, state_);
    auto result = evolution_->useItem(*inputs, state_, item);
    if (!result) {
        return false;
    }
    apply(session, *result);
    return true;
}

bool EvolutionAdapter::returnFromVirus(GameSession& session) {
    if (!evolution_) {
        return false;
    }
    const auto inputs = inputsOf(session, ScriptRunning);
    if (!inputs) {
        return false;
    }
    syncRaised(desc_.story, state_);
    auto result = evolution_->returnFromVirus(*inputs, state_);
    if (!result) {
        return false;
    }
    apply(session, *result);
    session.afterSystemCommand();
    return true;
}

core::Result<bool> EvolutionAdapter::evolveByScript(GameSession& session, std::string_view species) {
    if (!evolution_) {
        return core::fail(std::string{messages::EvolutionNotRead});
    }
    const auto target = evolution_->tables().find(species);
    if (!target) {
        return core::fail(formatMessage(messages::UnknownEvolutionForm, species));
    }
    const auto inputs = inputsOf(session, ScriptRunning);
    if (!inputs) {
        return false;
    }
    syncRaised(desc_.story, state_);
    auto result = evolution_->evolveByScript(*inputs, state_, *target);
    if (!result) {
        return false;
    }
    apply(session, *result);
    session.afterSystemCommand();
    return true;
}

game::evolution::Level EvolutionAdapter::levelOf(std::string_view species) const {
    if (!evolution_) {
        return game::evolution::Level::None;
    }
    const auto found = evolution_->tables().find(species);
    return found ? evolution_->tables().levelOf(*found) : game::evolution::Level::None;
}

std::optional<game::evolution::Level> EvolutionAdapter::itemTargetLevel(std::string_view item) const {
    if (!evolution_) {
        return std::nullopt;
    }
    const auto target = evolution_->tables().itemTarget(item);
    if (!target) {
        return std::nullopt;
    }
    return evolution_->tables().levelOf(*target);
}

std::vector<game::evolution::EvolutionResult> EvolutionAdapter::takeEvolutions() {
    return std::exchange(evolutions_, {});
}

std::optional<EvolutionInputs> EvolutionAdapter::inputsOf(const GameSession& session, bool scriptRunning) const {
    if (!evolution_) {
        return std::nullopt;
    }
    const game::partner::PartnerState& partner = session.partner().state();
    const EvolutionTables& tables = evolution_->tables();
    const auto species = tables.find(partner.species);
    if (!species) {
        return std::nullopt;
    }
    std::optional<game::evolution::VirusBackup> backup;
    if (const auto kept = tables.find(partner.virusBackup.species)) {
        backup = game::evolution::VirusBackup{.species = *kept, .stats = baseStatsOf(partner.virusBackup.stats)};
    }
    return EvolutionInputs{.species = *species,
                           .stats = baseStatsOf(partner.stats.base),
                           .weight = partner.weight,
                           .careMistakes = partner.evolution.careMistakes,
                           .discipline = partner.mood.discipline,
                           .happiness = partner.mood.happiness,
                           .tiredness = partner.mood.tiredness,
                           .battles = partner.evolution.battles,
                           .learnedMoves = masteredMoves(),
                           .ageDays = partner.life.age,
                           .evolutionTimer = partner.evolution.hours,
                           .map = mapIdOf(session),
                           .tamerFree = session.careContext().tamerIdle,
                           .scriptPaused = !scriptRunning,
                           .virusForm = tables.find(session.partnerRules().care.toilet.virusForm),
                           .virusBackup = backup};
}

std::int32_t EvolutionAdapter::masteredMoves() const {
    if (battle_ == nullptr || !evolution_) {
        return 0;
    }
    return game::evolution::countMasteredMoves(evolution_->rules().requirements, battle_->moves().learned,
                                               evolution_->tables().moves());
}

std::string EvolutionAdapter::mapIdOf(const GameSession& session) {
    const std::optional<FieldPlace> place = session.currentPlace();
    return place ? place->map : std::string{};
}

void EvolutionAdapter::apply(GameSession& session, const EvolutionResult& result) {
    if (!evolution_) {
        return;
    }
    game::partner::Partner& partner = session.partner();
    const EvolutionTables& tables = evolution_->tables();
    const std::string target{tables.idOf(result.target)};
    if (auto changed = partner.changeSpecies(target, session.clock()); !changed) {
        core::logError("{}", formatMessage(messages::EvolutionNotApplied, target, changed.error().message));
        return;
    }
    applyStats(session, result);
    partner.setWeight(result.weight);
    game::partner::EvolutionCounters counters = partner.state().evolution;
    counters.careMistakes = result.careMistakes;
    counters.battles = result.battles;
    counters.hours = result.evolutionTimer;
    partner.setEvolutionCounters(counters);
    partner.addLifetime(result.lifetimeBonusHours);
    if (result.tamerLevelChance > 0) {
        session.addTamerLevel(result.tamerLevelChance, session.sessionRules().tamer.evolutionLevelGain);
    }
    if (result.restartsVirusBar) {
        partner.restartVirusBar();
    }
    if (result.virusBackup) {
        partner.setVirusBackup({.species = std::string{tables.idOf(result.virusBackup->species)},
                                .stats = statBlockOf(result.virusBackup->stats)});
    }
    game::evolution::recordEvolution(state_, result);
    raise(result.target);
    session.requestAutosave(game::save::AutosaveReason::Evolution);
    evolutions_.push_back(result);
}

void EvolutionAdapter::syncRaised(const StoryAdapter* story, game::evolution::EvolutionState& state) {
    if (story != nullptr) {
        state.raised = story->raisedSpecies();
    }
}

void EvolutionAdapter::raise(game::evolution::Species species) {
    state_.raised.insert(species);
    if (desc_.story != nullptr) {
        desc_.story->markRaised(species);
    }
}

} // namespace dwe::session
