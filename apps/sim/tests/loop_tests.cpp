// The closing criterion of phase 3 (docs/plan/fase-3-nucleo-jugable.md): the loop of raising, training, battling and
// evolving plays on the `original` ruleset, end to end, through the game's session and systems, with a fast clock.
// It needs the user's extracted disc (DWE_CONTENT): the species, the evolution charts and the map's fighters are the
// original's.

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/content/game_content.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/evolution/inputs.hpp>
#include <dwe/game/evolution/result.hpp>
#include <dwe/game/evolution/rules.hpp>
#include <dwe/game/evolution/tables.hpp>
#include <dwe/game/field/rules.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/inventory/item_effect.hpp>
#include <dwe/game/inventory/item_effects.hpp>
#include <dwe/game/inventory/use.hpp>
#include <dwe/game/partner/effects.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/partner.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/game/partner/training_rules.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/ruleset_store.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/sim/headless_session.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::sim {

namespace {

namespace evolution = game::evolution;
namespace partner = game::partner;

// The seeds the loop plays: each one is a different raising (in the sixth the partner falls sick), and every one must
// reach the same form.
constexpr std::array<std::uint64_t, 6> LoopSeeds{1, 2, 3, 4, 5, 6};
// The fast clock: a game hour per real second (GameClock::setSpeed).
constexpr double FastClock = 60.0;
// The carer looks at the partner once every game minute, as a player would.
constexpr std::int64_t CarerTurnMinutes = 1;
// A bound on one battle, far above any real one: a battle still going after it fails the test.
constexpr std::int64_t BattleFrameBound = 20LL * 60 * 30;
// The battles the loop fights at least, whatever the aimed form asks for.
constexpr std::int32_t LoopBattles = 2;
// The carer puts the food and the medicines it gives in the bag a stack at a time.
constexpr std::int32_t ItemStack = 10;
// Extra game days the loop may play past the rookie fallback's hours before it gives up.
constexpr std::int64_t SpareDays = 2;

template <typename T>
std::string errorOf(const core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// The minimum stats of a requirement in the partner's stat order (HP and MP already divided, as the table has them).
std::array<std::int32_t, partner::StatCount> minimumsOf(const evolution::Requirement& requirement) {
    const evolution::BaseStats& minimum = requirement.minimum;
    return {minimum.hp, minimum.mp, minimum.offense, minimum.defense, minimum.speed, minimum.brains};
}

// The stat a requirement asks for when it asks for exactly one.
std::optional<partner::Stat> onlyStatOf(const evolution::Requirement& requirement) {
    std::optional<partner::Stat> only;
    const auto minimums = minimumsOf(requirement);
    for (const partner::Stat stat : partner::AllStats) {
        if (minimums.at(static_cast<std::size_t>(stat)) == evolution::NoRequirement) {
            continue;
        }
        if (only) {
            return std::nullopt;
        }
        only = stat;
    }
    return only;
}

// The care policy aims at the first form of the partner's chart whose points the carer can earn without the weight
// (evolution.c: calculateRequirementScore): it asks for a single stat other than HP and MP (which the table compares
// divided), for care mistakes as a maximum and for discipline or a number of battles as its bonus. Training that stat
// gives the stats point, caring without mistakes the care point, and scolding up to that discipline or fighting that
// many battles the bonus point; the weight is left to the meals.
struct AimedForm {
    evolution::Species form = 0;
    partner::Stat stat = partner::Stat::Offense;
    const evolution::Requirement* requirement = nullptr;
};

std::optional<AimedForm> aimedFormOf(const evolution::EvolutionTables& tables, evolution::Species from) {
    for (const evolution::Species target : tables.pathTargets(from)) {
        const evolution::Requirement* requirement = tables.requirement(target);
        if (requirement == nullptr) {
            continue;
        }
        const auto stat = onlyStatOf(*requirement);
        const bool asksBattles = requirement->battles != evolution::NoRequirement && !requirement->battlesAreMaximum;
        const bool asksDiscipline = requirement->discipline != evolution::NoRequirement;
        if (stat && *stat != partner::Stat::Hp && *stat != partner::Stat::Mp && (asksBattles || asksDiscipline) &&
            requirement->careIsMaximum) {
            return AimedForm{.form = target, .stat = *stat, .requirement = requirement};
        }
    }
    return std::nullopt;
}

// The requirement points the partner scores for a form (evolution.c: calculateRequirementScore): care mistakes,
// weight, stats and the bonus, here only with the discipline and the battles this policy plays for (the bonus also
// counts the source form, happiness and learned moves).
std::int32_t pointsFor(const evolution::Requirement& requirement, const evolution::EvolutionRules& rules,
                       const partner::PartnerState& state, partner::Stat stat) {
    const std::int32_t careMistakes = state.evolution.careMistakes;
    const bool care =
        requirement.careIsMaximum ? careMistakes <= requirement.careMistakes : careMistakes >= requirement.careMistakes;
    const std::int32_t margin = rules.requirements.weightMargin;
    const bool weight = requirement.weight - margin <= state.weight && state.weight <= requirement.weight + margin;
    const bool stats = state.stats.base[stat] >= minimumsOf(requirement).at(static_cast<std::size_t>(stat));
    const bool battles = requirement.battles != evolution::NoRequirement && !requirement.battlesAreMaximum &&
                         state.evolution.battles >= requirement.battles;
    const bool discipline =
        requirement.discipline != evolution::NoRequirement && state.mood.discipline >= requirement.discipline;
    return static_cast<std::int32_t>(care) + static_cast<std::int32_t>(weight) + static_cast<std::int32_t>(stats) +
           static_cast<std::int32_t>(battles || discipline);
}

std::int64_t framesPerHour(const ruleset::GameClock& clock) {
    return clock.rules().framesPerMinute * ruleset::MinutesPerHour;
}

// The training course that trains a stat: the first of the training section's courses with that aptitude.
struct Course {
    std::string place;
    const partner::TrainingCourse* course = nullptr;
};

std::optional<Course> courseFor(const partner::TrainingRules& training, partner::Stat stat) {
    for (const partner::TrainingPlace& place : training.places) {
        for (const partner::TrainingCourse& course : place.courses) {
            if (course.aptitude == stat) {
                return Course{.place = place.id, .course = &course};
            }
        }
    }
    return std::nullopt;
}

// The item that surely cures the sickness or the injury: a medicine whose cure covers its whole roll (item.c:
// handleMedicineHealing).
std::optional<std::int32_t> sureCureOf(const assets::ItemTable& items, const game::inventory::ItemEffects& effects,
                                       partner::Condition condition) {
    for (const assets::ItemEntry& item : items.items) {
        const auto effect = effects.resolve(item.index);
        if (!effect || !effect->medicine) {
            continue;
        }
        const game::inventory::Medicine& medicine = *effect->medicine;
        const bool sure = condition == partner::Condition::Sick
                              ? medicine.sicknessRoll > 0 && medicine.sicknessCure >= medicine.sicknessRoll
                              : medicine.injuryRoll > 0 && medicine.injuryCure >= medicine.injuryRoll;
        if (sure) {
            return item.index;
        }
    }
    return std::nullopt;
}

// The battle the debug trigger starts on the map the field starts on (apps/game: battle("")): its first NPC that can
// fight.
session::BattleRequest startMapBattle(const assets::MapDocument& map, const assets::DigimonTable& species) {
    session::BattleRequest request;
    request.map = map.id;
    for (const assets::MapNpc& npc : map.npcs) {
        const assets::DigimonRow* row = npc.digimon ? species.find(*npc.digimon) : nullptr;
        if (row == nullptr || npc.hp <= 0) {
            continue;
        }
        auto enemy = game::battle::enemyFromMap(*row, npc);
        REQUIRE_MESSAGE(enemy.has_value(), errorOf(enemy));
        request.enemies.push_back(std::move(*enemy));
        break;
    }
    REQUIRE_FALSE(request.enemies.empty());
    return request;
}

bool hasEvent(const std::vector<partner::PartnerEvent>& events, partner::EventKind kind) {
    return std::ranges::any_of(events, [kind](const partner::PartnerEvent& event) { return event.kind == kind; });
}

// What the carer did over the loop.
struct LoopTally {
    std::int32_t meals = 0;
    std::int32_t toilets = 0;
    std::int32_t sleeps = 0;
    std::int32_t trainings = 0;
    std::int32_t scolds = 0;
    std::int32_t cures = 0;
    std::int32_t battles = 0;
    std::int32_t victories = 0;
};

// Plays the loop on one headless game with a scripted carer, checking the effect of every command as it goes.
class LoopCarer {
public:
    // The medicines that surely cure the sickness and the injury.
    struct Medicines {
        std::int32_t sickness = 0;
        std::int32_t injury = 0;
    };

    LoopCarer(HeadlessGame& game, const AimedForm& aim, Course course, session::BattleRequest battle,
              const Medicines& medicines)
        : game_(&game), session_(game.session.get()), startSpecies_(game.session->partner().state().species), aim_(aim),
          course_(std::move(course)), battle_(std::move(battle)), medicines_(medicines) {}

    // One carer's turn: the clock moves a turn, then the carer does one thing, as a player would.
    void turn(double seconds) {
        session_->tick(seconds);
        const auto events = session_->takePartnerEvents();
        // Good care keeps the partner alive: a death would start the loop again from a reborn baby.
        REQUIRE_FALSE(hasEvent(events, partner::EventKind::Died));
        const partner::Partner& raised = session_->partner();
        // Once it evolved the carer only watches.
        if (raised.state().species != startSpecies_) {
            return;
        }
        const partner::Conditions& conditions = raised.state().conditions;
        if (conditions.has(partner::Condition::Sick)) {
            cure(partner::Condition::Sick, medicines_.sickness);
        } else if (conditions.has(partner::Condition::Injured)) {
            cure(partner::Condition::Injured, medicines_.injury);
        } else if (conditions.has(partner::Condition::Hungry)) {
            feed();
        } else if (conditions.has(partner::Condition::NeedsToilet)) {
            useToilet();
        } else if (raised.canSleep()) {
            sleep();
        } else if (wantsTraining()) {
            train();
        } else if (wantsDiscipline()) {
            scold();
        } else if (wantsBattle()) {
            fight();
        }
    }

    [[nodiscard]] const LoopTally& tally() const { return tally_; }

private:
    // Gives one of an item, putting a stack in the bag first if it has none.
    [[nodiscard]] game::inventory::UseOutcome give(std::int32_t item) {
        if (game_->inventory->inventory().countOf(item) == 0) {
            static_cast<void>(game_->inventory->inventory().add(item, ItemStack));
        }
        const auto used = game_->inventory->use(*session_, item);
        REQUIRE_MESSAGE(used.has_value(), errorOf(used));
        return used->outcome;
    }

    void cure(partner::Condition condition, std::int32_t medicine) {
        // The partner may refuse it; the carer tries again next turn.
        if (give(medicine) == game::inventory::UseOutcome::Used) {
            CHECK_FALSE(session_->partner().state().conditions.has(condition));
            ++tally_.cures;
        }
    }

    void feed() {
        const partner::PartnerState before = session_->partner().state();
        const std::optional<std::string>& food = session_->partner().species().favoriteFood;
        REQUIRE(food.has_value());
        const assets::ItemEntry* item = game_->inventory->items().find(food.value_or(std::string{}));
        REQUIRE(item != nullptr);
        REQUIRE(give(item->index) == game::inventory::UseOutcome::Used);
        const partner::PartnerState& after = session_->partner().state();
        // A meal fills the partner up (partner_impl.c: partnerHandleFoodFeed).
        CHECK(after.hunger.energy > before.hunger.energy);
        ++tally_.meals;
    }

    void useToilet() {
        session_->useToilet();
        const partner::PartnerState& after = session_->partner().state();
        CHECK_FALSE(after.conditions.has(partner::Condition::NeedsToilet));
        CHECK(hasEvent(session_->takePartnerEvents(), partner::EventKind::UsedToilet));
        ++tally_.toilets;
    }

    void sleep() {
        const std::int64_t before = session_->clock().elapsedFrames();
        const auto slept = session_->sleep({});
        REQUIRE_MESSAGE(slept.has_value(), errorOf(slept));
        // Sleeping moves the clock to the wake hour and wakes the partner (partner_impl.c: handleSleep).
        CHECK(session_->clock().elapsedFrames() > before);
        CHECK(hasEvent(session_->takePartnerEvents(), partner::EventKind::Slept));
        CHECK_FALSE(session_->partner().state().conditions.has(partner::Condition::Sleepy));
        ++tally_.sleeps;
    }

    [[nodiscard]] bool wantsTraining() const {
        const partner::PartnerState& state = session_->partner().state();
        const std::int32_t minimum = minimumsOf(*aim_.requirement).at(static_cast<std::size_t>(aim_.stat));
        const std::int32_t overwork = session_->partnerRules().training.overworkTiredness;
        return state.stats.base[aim_.stat] < minimum && state.mood.tiredness + course_.course->tiredness < overwork;
    }

    void train() {
        const partner::PartnerState before = session_->partner().state();
        const auto trained = session_->train({.place = course_.place, .course = course_.course->id});
        REQUIRE_MESSAGE(trained.has_value(), errorOf(trained));
        const partner::PartnerState& after = session_->partner().state();
        // A session raises the trained stat by what it gave and tires the partner (trn_reward.c).
        CHECK(trained->gains[aim_.stat] > 0);
        CHECK(after.stats.base[aim_.stat] == before.stats.base[aim_.stat] + trained->gains[aim_.stat]);
        CHECK(after.mood.tiredness > before.mood.tiredness);
        ++tally_.trainings;
    }

    [[nodiscard]] bool wantsDiscipline() const {
        const std::int32_t goal = aim_.requirement->discipline;
        return goal != evolution::NoRequirement && session_->partner().state().mood.discipline < goal;
    }

    void scold() {
        const partner::PartnerState before = session_->partner().state();
        session_->scold();
        const partner::PartnerState& after = session_->partner().state();
        // A scold raises the discipline and costs happiness (partner_impl.c: handlePraiseScold).
        CHECK(after.mood.discipline > before.mood.discipline);
        CHECK(after.mood.happiness < before.mood.happiness);
        ++tally_.scolds;
    }

    // Battles fought up to the aimed form's bonus or the loop's few, while the partner is neither worn out nor injured.
    [[nodiscard]] bool wantsBattle() const {
        const partner::PartnerState& state = session_->partner().state();
        const evolution::Requirement& requirement = *aim_.requirement;
        const bool asksBattles = requirement.battles != evolution::NoRequirement && !requirement.battlesAreMaximum;
        const std::int32_t goal = asksBattles ? std::max(requirement.battles, LoopBattles) : LoopBattles;
        const std::int32_t overwork = session_->partnerRules().training.overworkTiredness;
        return tally_.battles < goal && state.mood.tiredness < overwork &&
               !state.conditions.has(partner::Condition::Injured);
    }

    void fight() {
        const partner::PartnerState before = session_->partner().state();
        auto fightNow = game_->battle->start(*session_, battle_);
        REQUIRE_MESSAGE(fightNow.has_value(), errorOf(fightNow));
        while (!fightNow->finished() && fightNow->state().frame < BattleFrameBound) {
            fightNow->step();
        }
        const std::optional<game::battle::BattleResult>& ended = fightNow->result();
        REQUIRE(ended.has_value());
        const game::battle::BattleResult result = ended.value_or(game::battle::BattleResult{});
        const auto aftermath = game_->battle->finish(*session_, result);
        REQUIRE_MESSAGE(aftermath.has_value(), errorOf(aftermath));
        REQUIRE_FALSE(hasEvent(session_->takePartnerEvents(), partner::EventKind::Died));
        const partner::PartnerState& after = session_->partner().state();
        // The battle's result is the partner's (battle_ui.c: battleStatsGainsAndDrops, tamer.c: startBattle): the
        // stat raises as the partner took them and HP of the end; a victory counts for the evolution, a defeat costs
        // lives.
        for (const partner::Stat stat : partner::AllStats) {
            CHECK(after.stats.base[stat] == before.stats.base[stat] + aftermath->statGains[stat]);
        }
        CHECK(after.stats.currentHp == std::min(result.partnerHp, after.stats.base[partner::Stat::Hp]));
        if (result.outcome == game::battle::Outcome::Victory) {
            CHECK(after.evolution.battles > before.evolution.battles);
            CHECK(std::ranges::any_of(aftermath->statGains.values, [](std::int32_t gain) { return gain > 0; }));
            ++tally_.victories;
        } else if (result.outcome == game::battle::Outcome::Defeat) {
            CHECK(after.life.lives == before.life.lives - result.livesLost);
        }
        ++tally_.battles;
    }

    HeadlessGame* game_;
    session::GameSession* session_;
    std::string startSpecies_;
    AimedForm aim_;
    Course course_;
    session::BattleRequest battle_;
    Medicines medicines_;
    LoopTally tally_;
};

// The real seconds of a carer's turn at the fast clock.
double turnSeconds(const ruleset::ClockRules& clock) {
    return static_cast<double>(CarerTurnMinutes * clock.framesPerMinute) / (clock.framesPerSecond * FastClock);
}

} // namespace

// Runs only when DWE_CONTENT names a content folder (scripts/dev.sh mounts it).
TEST_CASE("the user's disc, extracted: raising, training and battling the first partner evolves it as the original") {
    const std::optional<std::filesystem::path> mountedFolder = dwe::test_support::userContent();
    if (!mountedFolder) {
        MESSAGE("DWE_CONTENT is not set: skipping the game loop on extracted content");
        return;
    }
    const std::filesystem::path& folder = *mountedFolder;
    const auto mounted = content::GameContent::mount(folder, {});
    REQUIRE_MESSAGE(mounted.has_value(), errorOf(mounted));
    const assets::VirtualFileSystem& files = (*mounted)->files();
    auto store = ruleset::RulesetStore::load(files, ruleset::OriginalRuleset);
    REQUIRE_MESSAGE(store.has_value(), errorOf(store));
    const ruleset::Ruleset& rules = store->current();
    const ScenarioContext context{
        .files = &files, .rules = &rules, .days = 1, .speed = FastClock, .parameters = nullptr};
    const auto sessions = HeadlessSessions::prepare(context);
    REQUIRE_MESSAGE(sessions.has_value(), errorOf(sessions));
    REQUIRE(sessions->tables().evolution.has_value());
    REQUIRE(sessions->tables().digimon.has_value());
    const evolution::EvolutionTables& tables = *sessions->tables().evolution;
    const auto evolutionSection = rules.requireSection(evolution::EvolutionSection);
    REQUIRE_MESSAGE(evolutionSection.has_value(), errorOf(evolutionSection));
    const auto evolutionRules = evolution::readEvolutionRules(**evolutionSection);
    REQUIRE_MESSAGE(evolutionRules.has_value(), errorOf(evolutionRules));
    const auto clockRules = ruleset::readClockRules(rules);
    REQUIRE_MESSAGE(clockRules.has_value(), errorOf(clockRules));
    const auto warps = game::field::loadFieldWarps(files);
    REQUIRE_MESSAGE(warps.has_value(), errorOf(warps));
    const auto map = assets::loadMapDocument(files, warps->startMap);
    REQUIRE_MESSAGE(map.has_value(), errorOf(map));
    const session::BattleRequest battle = startMapBattle(*map, *sessions->tables().digimon);
    REQUIRE(sessions->tables().items.has_value());
    REQUIRE(sessions->tables().itemEffects.has_value());
    const auto bag = game::inventory::Inventory::fromRuleset(rules);
    REQUIRE_MESSAGE(bag.has_value(), errorOf(bag));
    const game::inventory::ItemEffects effects{*sessions->tables().itemEffects, bag->rules().effects,
                                               bag->rules().quirks};
    const auto sicknessCure = sureCureOf(*sessions->tables().items, effects, partner::Condition::Sick);
    const auto injuryCure = sureCureOf(*sessions->tables().items, effects, partner::Condition::Injured);
    REQUIRE(sicknessCure.has_value());
    REQUIRE(injuryCure.has_value());

    for (const std::uint64_t seed : LoopSeeds) {
        CAPTURE(seed);
        auto game = sessions->startGame(seed);
        REQUIRE_MESSAGE(game.has_value(), errorOf(game));
        session::GameSession& session = *game->session;

        // A new game: the partner of the new_game section (main.c: initializePartner).
        const std::string start = session.partner().state().species;
        CHECK(start == session.sessionRules().newGame.partner);
        const auto startForm = tables.find(start);
        REQUIRE(startForm.has_value());
        REQUIRE(tables.levelOf(*startForm) == evolution::Level::Rookie);
        const auto aim = aimedFormOf(tables, *startForm);
        REQUIRE(aim.has_value());
        const auto course = courseFor(session.partnerRules().training, aim->stat);
        REQUIRE(course.has_value());

        // The loop, a carer's turn at a time, until the partner evolves or the rookie fallback's hours are long past.
        const std::int64_t bound = session.clock().elapsedFrames() +
                                   ((evolutionRules->fallback.rookieHours + (SpareDays * ruleset::HoursPerDay)) *
                                    framesPerHour(session.clock()));
        LoopCarer carer{*game, *aim, *course, battle, {.sickness = *sicknessCure, .injury = *injuryCure}};
        partner::PartnerState beforeEvolution = session.partner().state();
        while (session.partner().state().species == start && session.clock().elapsedFrames() < bound) {
            beforeEvolution = session.partner().state();
            carer.turn(turnSeconds(*clockRules));
        }

        // Every part of the loop was played.
        const LoopTally& tally = carer.tally();
        CHECK(tally.meals > 0);
        CHECK(tally.toilets > 0);
        CHECK(tally.sleeps > 0);
        CHECK(tally.trainings > 0);
        CHECK(tally.battles > 0);

        // The partner evolved, at the periodic check, into the form the care aimed at: it scored its pass points
        // before the rookie fallback's hours (evolution.c: getRookieEvolutionTarget).
        const auto evolutions = game->evolution->takeEvolutions();
        const partner::PartnerState& ended = session.partner().state();
        CAPTURE(ended.species);
        CAPTURE(ended.evolution.hours);
        CAPTURE(ended.evolution.careMistakes);
        CAPTURE(ended.mood.discipline);
        CAPTURE(ended.stats.base[aim->stat]);
        REQUIRE(evolutions.size() == 1);
        const evolution::EvolutionResult& evolved = evolutions.front();
        CHECK(evolved.reason == evolution::EvolutionReason::Normal);
        const evolution::StatReference* reference = tables.statReference(aim->form);
        const evolution::Species loaded = reference == nullptr ? aim->form : reference->loads;
        CHECK(evolved.target == loaded);
        CHECK(session.partner().state().species == tables.idOf(loaded));
        CHECK(tables.levelOf(loaded) == evolution::Level::Champion);
        CHECK(beforeEvolution.evolution.hours < evolutionRules->fallback.rookieHours);
        CHECK(pointsFor(*aim->requirement, *evolutionRules, beforeEvolution, aim->stat) >=
              evolutionRules->requirements.passPoints);
        MESSAGE("seed " << seed << ": " << start << " -> " << session.partner().state().species << " after "
                        << beforeEvolution.evolution.hours << " h; meals " << tally.meals << ", toilets "
                        << tally.toilets << ", sleeps " << tally.sleeps << ", trainings " << tally.trainings
                        << ", scolds " << tally.scolds << ", cures " << tally.cures << ", battles " << tally.battles
                        << " (" << tally.victories << " won), care mistakes " << beforeEvolution.evolution.careMistakes
                        << ", discipline " << beforeEvolution.mood.discipline << ", weight " << beforeEvolution.weight);
    }
}

} // namespace dwe::sim
