#pragma once

#include <dwe/assets/item_effect_table.hpp>

#include <cstdint>
#include <optional>
#include <string>

// What using an item does, as a plain description: the inventory resolves it from data/item_effects and the ruleset,
// and an ItemEffectTarget (the partner, in the field or in battle) applies it. Nothing here touches the partner.
namespace dwe::game::inventory {

// Where an item is used: GAME_STATE 0 or 1 in the original.
enum class UseContext : std::uint8_t { Field, Battle };

// Current HP and MP healed, each capped at its maximum.
struct Healing {
    std::int32_t hp = 0;
    std::int32_t mp = 0;
};

// A revive (item.c: handleRestore): HP set back to this share of the maximum; in battle it also removes the death
// countdown.
struct Revive {
    std::int32_t hpPercent = 0;
};

// The injury and sickness cures of a medicine (item.c: handleMedicineHealing): each condition is cured when a roll
// below its `roll` falls under its `cure`.
struct Medicine {
    std::int32_t injuryRoll = 0;
    std::int32_t injuryCure = 0;
    std::int32_t sicknessRoll = 0;
    std::int32_t sicknessCure = 0;
};

// A battle disk (main.c: handleBuffDisks): stats raised for the battle, each up to limitPercent of its value when the
// battle started and never past limitCap, unless ignoresLimit.
struct BattleBoost {
    std::int32_t offense = 0;
    std::int32_t defense = 0;
    std::int32_t speed = 0;
    std::int32_t limitPercent = 0;
    std::int32_t limitCap = 0;
    bool ignoresLimit = false;
};

// Permanent stat gains (chips and food), each capped: statLimit for the four stats, hpMpLimit for maximum HP and MP.
struct StatGain {
    std::int32_t maxHp = 0;
    std::int32_t maxMp = 0;
    std::int32_t offense = 0;
    std::int32_t defense = 0;
    std::int32_t speed = 0;
    std::int32_t brain = 0;
    std::int32_t statLimit = 0;
    std::int32_t hpMpLimit = 0;
};

// A training boost (item.c: setTrainingBoost): `flag` 0 gives none.
struct TrainingBoost {
    std::int32_t flag = 0;
    std::int32_t value = 0;
    std::int32_t hours = 0;
};

// The care changes of food and chips; the partner's care caps them (docs/reference/care.md).
struct CareChange {
    std::int32_t energy = 0;
    std::int32_t tirednessRelief = 0;
    std::int32_t happiness = 0;
    std::int32_t discipline = 0;
    std::int32_t weight = 0;
    std::int32_t lifetimeHours = 0;
    // Chips go through modifyLifetime, which stops at 0; food adds to the lifetime as it is.
    bool lifetimeStopsAtZero = false;
    std::int32_t poopLevelChange = 0;
    // The chance in 100 of getting sick (item.c: handleItemSickness).
    std::int32_t sicknessChance = 0;
    TrainingBoost trainingBoost{};
    // When the item is the partner's favourite food (RAISE_DATA.favoriteFood): energy × percent / 100 and extra
    // happiness; 0 and 0 for anything that is not food.
    std::int32_t favouriteEnergyPercent = 0;
    std::int32_t favouriteHappiness = 0;
};

// A tamer level change (clock.c: addTamerLevel): `amount` levels with a chance in 100.
struct TamerLevelChange {
    std::int32_t chancePercent = 0;
    std::int32_t amount = 0;
};

// An evolution (item.c: handleEvoItems).
struct Evolution {
    // The Digimon id in data/digimon, or its raw number as text.
    std::string digimon;
    // Whether it only works when the partner's level is the target's level - 1.
    bool requiresPreviousLevel = false;
};

struct ItemEffect {
    // The item's id in data/items.
    std::string item;
    assets::ItemHandler handler = assets::ItemHandler::None;
    // Whether nothing happens while the partner has 0 HP.
    bool consciousOnly = false;
    Healing healing{};
    // In battle, BTL_healStatusEffect with this argument.
    std::optional<std::int32_t> battleStatusCure = std::nullopt;
    // In battle, the partner is protected (FIGHTER_FLAG_PROTECTED).
    bool protects = false;
    std::optional<Medicine> medicine = std::nullopt;
    std::optional<Revive> revive = std::nullopt;
    std::optional<BattleBoost> battleBoost = std::nullopt;
    std::optional<StatGain> statGain = std::nullopt;
    std::optional<CareChange> care = std::nullopt;
    std::optional<TamerLevelChange> tamerLevel = std::nullopt;
    // A script section run when the script is paused (item.c: handleChips case 0x16).
    std::optional<std::int32_t> scriptSection = std::nullopt;
    // The partner relieves itself if it needs to (item.c: handlePortaPotty).
    bool relievesToilet = false;
    std::optional<Evolution> evolution = std::nullopt;

    // Whether using the item changes anything.
    [[nodiscard]] bool doesSomething() const;
};

// What an item is used on: the partner, which the session gives the inventory. game/inventory never includes the
// partner's module.
class ItemEffectTarget {
public:
    ItemEffectTarget() = default;
    virtual ~ItemEffectTarget() = default;
    ItemEffectTarget(const ItemEffectTarget&) = delete;
    ItemEffectTarget& operator=(const ItemEffectTarget&) = delete;
    ItemEffectTarget(ItemEffectTarget&&) = delete;
    ItemEffectTarget& operator=(ItemEffectTarget&&) = delete;

    // Whether the partner takes the item (partner_impl.c: partnerWillRefuseItem). Asked in the field only: in battle
    // the item is thrown and always lands.
    virtual bool accepts(const ItemEffect& effect, UseContext context) = 0;

    // Applies the effect.
    virtual void apply(const ItemEffect& effect, UseContext context) = 0;
};

} // namespace dwe::game::inventory
