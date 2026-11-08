#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/save/sections.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/battle_registry.hpp>
#include <dwe/session/game_session.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view SaveName = "battle_registry";
constexpr std::string_view SaveText = "the battle_registry save section";
constexpr std::string_view EntryText = "a registered partner";
constexpr std::string_view MissingInSave =
    "battle registry: the save has no battle_registry section; no partner is registered";
constexpr std::string_view NoSuchSlot = "battle registry: there is no slot {} (the last is {})";
constexpr std::string_view SlotTaken = "battle registry: slot {} is listed twice";

constexpr std::string_view PartnersKey = "partners";
constexpr std::string_view SlotKey = "slot";
constexpr std::string_view NameKey = "name";
constexpr std::string_view SpeciesKey = "species";
constexpr std::string_view HpKey = "hp";
constexpr std::string_view MpKey = "mp";
constexpr std::string_view OffenseKey = "offense";
constexpr std::string_view DefenseKey = "defense";
constexpr std::string_view SpeedKey = "speed";
constexpr std::string_view BrainsKey = "brains";
constexpr std::string_view DisciplineKey = "discipline";
constexpr std::string_view TechniquesKey = "techniques";
constexpr std::string_view NullText = "null";

YamlNode integerNode(std::int64_t value) {
  return YamlNode::scalar(std::to_string(value));
}

YamlNode entryNode(std::size_t slot, const RegisteredPartner& partner) {
  YamlNode entry = YamlNode::mapping();
  entry.set(std::string{SlotKey}, integerNode(static_cast<std::int64_t>(slot)));
  entry.set(std::string{NameKey}, YamlNode::scalar(partner.name, YamlNode::ScalarStyle::Quoted));
  entry.set(std::string{SpeciesKey}, YamlNode::scalar(partner.species));
  entry.set(std::string{HpKey}, integerNode(partner.hp));
  entry.set(std::string{MpKey}, integerNode(partner.mp));
  entry.set(std::string{OffenseKey}, integerNode(partner.offense));
  entry.set(std::string{DefenseKey}, integerNode(partner.defense));
  entry.set(std::string{SpeedKey}, integerNode(partner.speed));
  entry.set(std::string{BrainsKey}, integerNode(partner.brains));
  entry.set(std::string{DisciplineKey}, integerNode(partner.discipline));
  YamlNode techniques = YamlNode::sequence();
  for (const auto& technique : partner.techniques) {
    techniques.append(YamlNode::scalar(technique.value_or(std::string{NullText})));
  }
  entry.set(std::string{TechniquesKey}, std::move(techniques));
  return entry;
}

RegisteredPartner partnerOf(YamlReader& reader, const YamlNode& entry) {
  RegisteredPartner partner{
      .name = reader.text(reader.field(entry, NameKey)),
      .species = reader.text(reader.field(entry, SpeciesKey)),
      .hp = reader.integer<std::int32_t>(reader.field(entry, HpKey)),
      .mp = reader.integer<std::int32_t>(reader.field(entry, MpKey)),
      .offense = reader.integer<std::int32_t>(reader.field(entry, OffenseKey)),
      .defense = reader.integer<std::int32_t>(reader.field(entry, DefenseKey)),
      .speed = reader.integer<std::int32_t>(reader.field(entry, SpeedKey)),
      .brains = reader.integer<std::int32_t>(reader.field(entry, BrainsKey)),
      .discipline = reader.integer<std::int32_t>(reader.field(entry, DisciplineKey))};
  const auto techniques = reader.sequence(reader.field(entry, TechniquesKey), TechniquesKey);
  for (std::size_t place = 0; place < techniques.size(); ++place) {
    if (place >= RegisteredPartner::Techniques) {
      reader.fail(techniques[place], "more techniques than three");
      break;
    }
    partner.techniques.at(place) = reader.optionalText(techniques[place]);
  }
  return partner;
}

} // namespace

class BattleRegistry::Serializer final : public game::save::SectionSerializer {
public:
  explicit Serializer(BattleRegistry& registry) : registry_(&registry) {}

  [[nodiscard]] core::Result<YamlNode> write() const override {
    YamlNode partners = YamlNode::sequence();
    for (std::size_t slot = 0; slot < Slots; ++slot) {
      if (const auto& partner = registry_->slots_.at(slot)) {
        partners.append(entryNode(slot, *partner));
      }
    }
    YamlNode root = YamlNode::mapping();
    root.set(std::string{PartnersKey}, std::move(partners));
    return root;
  }

  core::Result<void> read(const YamlNode& state) override {
    YamlReader reader;
    if (!reader.expectKeys(state, SaveText, {PartnersKey})) {
      return std::unexpected{reader.error(SaveName)};
    }
    std::array<std::optional<RegisteredPartner>, Slots> slots{};
    for (const YamlNode& entry : reader.sequence(reader.field(state, PartnersKey), PartnersKey)) {
      if (!reader.expectKeys(entry, EntryText,
                             {SlotKey, NameKey, SpeciesKey, HpKey, MpKey, OffenseKey, DefenseKey,
                              SpeedKey, BrainsKey, DisciplineKey, TechniquesKey})) {
        break;
      }
      const YamlNode& slotNode = reader.field(entry, SlotKey);
      const auto slot = reader.integer<std::size_t>(slotNode);
      RegisteredPartner partner = partnerOf(reader, entry);
      if (!reader.ok()) {
        break;
      }
      if (slot >= Slots) {
        reader.fail(slotNode, core::formatMessage(NoSuchSlot, slot, Slots - 1));
        break;
      }
      if (slots.at(slot)) {
        reader.fail(slotNode, core::formatMessage(SlotTaken, slot));
        break;
      }
      slots.at(slot) = std::move(partner);
    }
    auto read = reader.finish(std::move(slots), SaveName);
    if (!read) {
      return std::unexpected{read.error()};
    }
    registry_->slots_ = std::move(*read);
    return {};
  }

  core::Result<void> readMissing() override {
    core::logInfo("{}", MissingInSave);
    registry_->slots_ = {};
    return {};
  }

private:
  BattleRegistry* registry_;
};

BattleRegistry::BattleRegistry() : serializer_{std::make_unique<Serializer>(*this)} {}

BattleRegistry::~BattleRegistry() = default;

core::Result<void> BattleRegistry::addSaveSections(game::save::SaveSections& sections) {
  return sections.add(std::string{SaveName}, *serializer_, game::save::MigrationChain{});
}

core::Result<void> BattleRegistry::startNewGame(GameSession& /*session*/) {
  slots_ = {};
  return {};
}

const std::optional<RegisteredPartner>& BattleRegistry::at(std::size_t slot) const {
  static const std::optional<RegisteredPartner> none;
  return slot < Slots ? slots_.at(slot) : none;
}

core::Result<void> BattleRegistry::registerAt(std::size_t slot, RegisteredPartner partner) {
  if (slot >= Slots) {
    return core::fail(core::formatMessage(NoSuchSlot, slot, Slots - 1));
  }
  core::logDebug("[port:save] registerBattleData (main_menu.c:5230): slot {} {} ({})", slot,
                 partner.name, partner.species);
  slots_.at(slot) = std::move(partner);
  return {};
}

void BattleRegistry::clear(std::size_t slot) {
  if (slot < Slots) {
    slots_.at(slot).reset();
  }
}

std::size_t BattleRegistry::registeredCount() const {
  return static_cast<std::size_t>(std::ranges::count_if(
      slots_, [](const std::optional<RegisteredPartner>& slot) { return slot.has_value(); }));
}

core::Result<RegisteredPartner> registeredPartnerOf(const GameSession& session,
                                                    const BattleAdapter& battle) {
  auto fighter = battle.partnerCombatant(session);
  if (!fighter) {
    return std::unexpected{fighter.error()};
  }
  RegisteredPartner partner{.name = session.names().partner,
                            .species = fighter->species,
                            .hp = fighter->stats.maxHp,
                            .mp = fighter->stats.maxMp,
                            .offense = fighter->stats.offense,
                            .defense = fighter->stats.defense,
                            .speed = fighter->stats.speed,
                            .brains = fighter->stats.brain,
                            .discipline = session.partnerView().discipline.value};
  for (std::size_t place = 0; place < RegisteredPartner::Techniques; ++place) {
    partner.techniques.at(place) = fighter->moves.at(place);
  }
  return partner;
}

core::Result<std::vector<RegisteredPartner>>
registeredPartnersIn(const game::save::SaveFile& file) {
  BattleRegistry registry;
  game::save::SaveSections sections;
  if (auto added = registry.addSaveSections(sections); !added) {
    return std::unexpected{added.error()};
  }
  if (auto read = sections.read(file); !read) {
    return std::unexpected{read.error()};
  }
  std::vector<RegisteredPartner> partners;
  for (std::size_t slot = 0; slot < BattleRegistry::Slots; ++slot) {
    if (const auto& partner = registry.at(slot)) {
      partners.push_back(*partner);
    }
  }
  return partners;
}

} // namespace dwe::session
