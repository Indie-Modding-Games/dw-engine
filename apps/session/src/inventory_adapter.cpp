#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/game/inventory/inventory_yaml.hpp>
#include <dwe/game/inventory/stacking.hpp>
#include <dwe/game/partner/effects.hpp>
#include <dwe/game/partner/partner.hpp>
#include <dwe/game/save/sections.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/messages.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace dwe::session {

namespace {

using game::inventory::ItemEffect;
using game::inventory::UseContext;
using game::partner::RefusalCheck;
using game::partner::Stat;
using game::partner::StatBlock;
using messages::formatMessage;

constexpr std::int32_t Percent = 100;

StatBlock statBlockOf(const game::inventory::StatGain& gain) {
    StatBlock block;
    block[Stat::Hp] = gain.maxHp;
    block[Stat::Mp] = gain.maxMp;
    block[Stat::Offense] = gain.offense;
    block[Stat::Defense] = gain.defense;
    block[Stat::Speed] = gain.speed;
    block[Stat::Brain] = gain.brain;
    return block;
}

// The chance in 100 of a medicine's cure: a roll below `roll` under `cure` (item.c: handleMedicineHealing).
std::int32_t curePercent(std::int32_t cure, std::int32_t roll) {
    return roll > 0 ? cure * Percent / roll : 0;
}

} // namespace

// Writes and reads the bag with the inventory module's YAML form.
class InventoryAdapter::Serializer final : public game::save::SectionSerializer {
public:
    explicit Serializer(InventoryAdapter& adapter) : adapter_(&adapter) {}

    [[nodiscard]] core::Result<core::YamlNode> write() const override {
        if (!adapter_->inventory_) {
            return core::fail(std::string{messages::InventoryNotRead});
        }
        return game::inventory::toYaml(*adapter_->inventory_);
    }

    core::Result<void> read(const core::YamlNode& state) override {
        if (!adapter_->inventory_) {
            return core::fail(std::string{messages::InventoryNotRead});
        }
        return game::inventory::fromYaml(*adapter_->inventory_, state);
    }

private:
    InventoryAdapter* adapter_;
};

// The partner as the inventory sees it in the field: it may refuse an item, and takes its effect.
class InventoryAdapter::PartnerTarget final : public game::inventory::ItemEffectTarget {
public:
    PartnerTarget(GameSession& session, const InventoryAdapter& adapter) : session_(&session), adapter_(&adapter) {}

    bool accepts(const ItemEffect& effect, UseContext /*context*/) override {
        return !session_->partner().refuses({.item = itemId(effect.item), .check = refusalCheck(effect)});
    }

    void apply(const ItemEffect& effect, UseContext /*context*/) override {
        game::partner::Partner& partner = session_->partner();
        // The items that need the partner awake do nothing at 0 HP (item.c: handleHPHealingItem and others).
        if (effect.consciousOnly && partner.state().stats.currentHp <= 0) {
            return;
        }
        if (effect.handler == assets::ItemHandler::Food) {
            partner.eat(foodOf(effect), session_->clock());
            return;
        }
        heal(effect);
        if (effect.medicine) {
            partner.cure(
                {.injuryPercent = curePercent(effect.medicine->injuryCure, effect.medicine->injuryRoll),
                 .sicknessPercent = curePercent(effect.medicine->sicknessCure, effect.medicine->sicknessRoll)});
        }
        if (effect.statGain) {
            partner.addStatGains(statBlockOf(*effect.statGain));
        }
        if (effect.care) {
            partner.addLifetime(effect.care->lifetimeHours);
        }
        if (effect.tamerLevel) {
            session_->addTamerLevel(effect.tamerLevel->chancePercent, effect.tamerLevel->amount);
        }
        if (effect.relievesToilet) {
            partner.usePortableToilet();
        }
        if (effect.evolution && adapter_->desc_.evolution != nullptr) {
            adapter_->desc_.evolution->useItem(*session_, itemId(effect.item));
        }
        // Gap: a script chip's section waits for the script VM (phase 4).
    }

private:
    [[nodiscard]] std::string itemId(std::int32_t item) const {
        const assets::ItemEntry* entry = adapter_->items().findByIndex(item);
        return entry != nullptr ? entry->id : std::string{};
    }

    [[nodiscard]] std::int32_t partnerLevel() const {
        return static_cast<std::int32_t>(session_->partner().species().level);
    }

    // partner_impl.c: partnerWillRefuseItem.
    [[nodiscard]] RefusalCheck refusalCheck(const ItemEffect& effect) const {
        const ItemRefusalRules& rules = adapter_->itemUse_.refusal;
        if (inRanges(rules.neverTaken, effect.item) || refusesEvolution(effect)) {
            return RefusalCheck::Always;
        }
        return effect.item < rules.disciplineCheckedBelow ? RefusalCheck::Discipline : RefusalCheck::FavoriteOnly;
    }

    [[nodiscard]] bool refusesEvolution(const ItemEffect& effect) const {
        if (effect.handler != assets::ItemHandler::Evolution) {
            return false;
        }
        const ItemRefusalRules& rules = adapter_->itemUse_.refusal;
        const auto fixed = std::ranges::find(rules.fixedLevelEvolutions, effect.item, &FixedLevelEvolution::item);
        if (fixed != rules.fixedLevelEvolutions.end()) {
            return partnerLevel() != fixed->level;
        }
        if (adapter_->desc_.evolution == nullptr) {
            return false;
        }
        const auto target = adapter_->desc_.evolution->itemTargetLevel(itemId(effect.item));
        if (!target) {
            return false;
        }
        const std::int32_t difference = static_cast<std::int32_t>(*target) - partnerLevel();
        return std::ranges::contains(rules.refusedLevelDifferences, difference);
    }

    void heal(const ItemEffect& effect) const {
        game::partner::Partner& partner = session_->partner();
        game::partner::PartnerStats stats = partner.state().stats;
        std::int32_t hp = effect.healing.hp;
        if (effect.revive) {
            // item.c: handleRestore adds a share of the maximum, within it.
            hp += stats.base[Stat::Hp] * effect.revive->hpPercent / Percent;
        }
        stats.currentHp = std::min(stats.currentHp + hp, stats.base[Stat::Hp]);
        stats.currentMp = std::min(stats.currentMp + effect.healing.mp, stats.base[Stat::Mp]);
        partner.setStats(stats);
    }

    // item.c: handleFood, and partnerHandleFoodFeed for the meals.
    [[nodiscard]] game::partner::FoodEffect foodOf(const ItemEffect& effect) const {
        const game::inventory::CareChange care = effect.care.value_or(game::inventory::CareChange{});
        return {.item = itemId(effect.item),
                .meal = inRanges(adapter_->itemUse_.meals, effect.item),
                .energy = care.energy,
                .tirednessRelief = care.tirednessRelief,
                .happiness = care.happiness,
                .discipline = care.discipline,
                .weight = care.weight,
                .lifetimeHours = care.lifetimeHours,
                .permanentGain = effect.statGain ? statBlockOf(*effect.statGain) : StatBlock{},
                .hpHeal = effect.healing.hp,
                .mpHeal = effect.healing.mp,
                .boost = boostOf(care.trainingBoost),
                .sicknessPercent = care.sicknessChance};
    }

    [[nodiscard]] game::partner::TrainingBoostEffect boostOf(const game::inventory::TrainingBoost& boost) const {
        game::partner::TrainingBoostEffect effect{.stats = {}, .value = boost.value, .hours = boost.hours};
        for (const Stat stat : game::partner::AllStats) {
            if ((boost.flag & adapter_->itemUse_.trainingBoostBits[stat]) != 0) {
                effect.stats.add(stat);
            }
        }
        return effect;
    }

    GameSession* session_;
    const InventoryAdapter* adapter_;
};

InventoryAdapter::InventoryAdapter(InventoryAdapterDesc desc)
    : desc_(std::move(desc)), serializer_(std::make_unique<Serializer>(*this)) {}

InventoryAdapter::~InventoryAdapter() = default;

ruleset::SectionChecks InventoryAdapter::sectionChecks() const {
    return {{std::string{game::inventory::SectionName}, game::inventory::inventorySectionCheck()},
            {std::string{ItemUseSection}, checkItemUseSection}};
}

core::Result<void> InventoryAdapter::loadTables() {
    if (desc_.items && desc_.effects) {
        return {};
    }
    if (desc_.files == nullptr) {
        return core::fail(std::string{messages::InventoryNeedsTables});
    }
    if (!desc_.items) {
        auto items = assets::loadItemTable(*desc_.files);
        if (!items) {
            return std::unexpected{items.error()};
        }
        desc_.items = std::move(*items);
    }
    if (!desc_.effects) {
        auto effects = assets::loadItemEffectTable(*desc_.files);
        if (!effects) {
            return std::unexpected{effects.error()};
        }
        desc_.effects = std::move(*effects);
    }
    return {};
}

core::Result<void> InventoryAdapter::readRules(const ruleset::Ruleset& rules) {
    if (auto loaded = loadTables(); !loaded) {
        return loaded;
    }
    auto section = rules.requireSection(game::inventory::SectionName);
    if (!section) {
        return std::unexpected{section.error()};
    }
    auto itemUseSection = rules.requireSection(ItemUseSection);
    if (!itemUseSection) {
        return std::unexpected{itemUseSection.error()};
    }
    auto itemUse = readItemUseRules(**itemUseSection);
    if (!itemUse) {
        return std::unexpected{itemUse.error()};
    }
    section_ = **section;
    auto fresh = freshInventory();
    if (!fresh) {
        return std::unexpected{fresh.error()};
    }
    if (!desc_.effects) {
        return core::fail(std::string{messages::InventoryNeedsTables});
    }
    const game::inventory::InventoryRules& inventoryRules = fresh->rules();
    effects_.emplace(*desc_.effects, inventoryRules.effects, inventoryRules.quirks);
    if (inventory_) {
        auto stacking = game::inventory::stackingRules().create(*section_, game::inventory::StackingPath);
        if (!stacking) {
            return std::unexpected{stacking.error()};
        }
        inventory_->setRules(inventoryRules, std::move(*stacking));
    } else {
        inventory_.emplace(std::move(*fresh));
    }
    itemUse_ = std::move(*itemUse);
    return {};
}

core::Result<game::inventory::Inventory> InventoryAdapter::freshInventory() const {
    if (!section_) {
        return core::fail(std::string{messages::InventoryNotRead});
    }
    auto rules = game::inventory::parseInventoryRules(*section_);
    if (!rules) {
        return std::unexpected{rules.error()};
    }
    auto stacking = game::inventory::stackingRules().create(*section_, game::inventory::StackingPath);
    if (!stacking) {
        return std::unexpected{stacking.error()};
    }
    return game::inventory::Inventory{std::move(*rules), std::move(*stacking)};
}

core::Result<void> InventoryAdapter::addSaveSections(game::save::SaveSections& sections) {
    static_assert(game::inventory::SectionVersion == 1, "the inventory section needs its migrations here");
    return sections.add(std::string{InventorySaveName}, *serializer_, game::save::MigrationChain{});
}

core::Result<void> InventoryAdapter::startNewGame(GameSession& /*session*/) {
    if (!section_) {
        return core::fail(std::string{messages::InventoryNotRead});
    }
    auto fresh = freshInventory();
    if (!fresh) {
        return std::unexpected{fresh.error()};
    }
    // Script dg 193 empties the boot state's bag; the ruleset's list is what a new game starts with.
    for (const game::inventory::StartingItem& item : fresh->rules().bag.newGameItems) {
        if (items().findByIndex(item.item) == nullptr) {
            return core::fail(formatMessage(messages::UnknownStartingItem, item.item));
        }
        fresh->add(item.item, item.count);
    }
    inventory_.emplace(std::move(*fresh));
    return {};
}

core::Result<game::inventory::UseResult> InventoryAdapter::use(GameSession& session, std::int32_t item) {
    if (!inventory_ || !effects_) {
        return core::fail(std::string{messages::InventoryNotRead});
    }
    PartnerTarget target{session, *this};
    auto used = game::inventory::useItem(*inventory_, item, UseContext::Field, items(), *effects_, target);
    if (!used) {
        return used;
    }
    if (used->outcome == game::inventory::UseOutcome::Used && used->keyItem) {
        session.requestAutosave(game::save::AutosaveReason::KeyItem);
    }
    session.afterSystemCommand();
    return used;
}

core::Result<game::inventory::UseResult> InventoryAdapter::useInBattle(std::int32_t item,
                                                                       game::inventory::ItemEffectTarget& target) {
    if (!inventory_ || !effects_) {
        return core::fail(std::string{messages::InventoryNotRead});
    }
    return game::inventory::useItem(*inventory_, item, UseContext::Battle, items(), *effects_, target);
}

core::Result<game::inventory::Purchase> InventoryAdapter::buy(GameSession& session, std::int32_t item,
                                                              std::int32_t amount,
                                                              const game::inventory::StockTriggers& triggers) {
    if (!inventory_) {
        return core::fail(std::string{messages::InventoryNotRead});
    }
    const assets::ItemEntry* entry = items().findByIndex(item);
    if (entry == nullptr) {
        return core::fail(formatMessage(messages::UnknownItem, item));
    }
    const auto level = static_cast<std::int32_t>(session.partner().species().level);
    const game::inventory::Buyer buyer{.partnerIsBaby = std::ranges::contains(itemUse_.shopDiscountLevels, level)};
    auto bought = game::inventory::buy(*inventory_, items(), *entry, amount, buyer, triggers);
    if (!bought) {
        return bought;
    }
    if (bought->keyItem) {
        session.requestAutosave(game::save::AutosaveReason::KeyItem);
    }
    session.afterSystemCommand();
    return bought;
}

std::vector<game::inventory::Stack> InventoryAdapter::loseOnDefeat(GameSession& session) {
    return inventory_ ? inventory_->loseOnDefeat(session.random()) : std::vector<game::inventory::Stack>{};
}

game::inventory::Inventory& InventoryAdapter::inventory() {
    DWE_ASSERT(inventory_.has_value());
    return *inventory_;
}

const game::inventory::Inventory& InventoryAdapter::inventory() const {
    DWE_ASSERT(inventory_.has_value());
    return *inventory_;
}

const assets::ItemTable& InventoryAdapter::items() const {
    DWE_ASSERT(desc_.items.has_value());
    return *desc_.items;
}

} // namespace dwe::session
