#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/ruleset/messages.hpp>
#include <dwe/ruleset/species_extra.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <format>
#include <utility>
#include <variant>

namespace dwe::ruleset {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view SpeciesKey = "species";
// Read by the tournaments (packages/game/tournament: parseExtraPrizes), in the format of
// tournament.yaml.
constexpr std::string_view ExtraPrizesKey = "extra_prizes";
constexpr std::string_view CheckHoursKey = "check_hours";
constexpr std::string_view LifetimeHoursKey = "lifetime_hours";
constexpr std::string_view TamerLevelChanceKey = "tamer_level_chance";
constexpr std::string_view MegaKey = "mega";

constexpr std::string_view RequirementKey = "requirement";
constexpr std::string_view GateKey = "gate";
constexpr std::string_view EvolvesFromKey = "evolves_from";
constexpr std::string_view EvolvesToKey = "evolves_to";
constexpr std::string_view StatsGainsKey = "stats_gains";
constexpr std::string_view RaiseKey = "raise";
constexpr std::string_view MovesKey = "moves";
constexpr std::string_view PartnerTypeKey = "partner_type";
constexpr std::string_view PartnerMovesKey = "partner_moves";
constexpr std::string_view EffectTypeKey = "effect_type";
constexpr std::string_view ClipsKey = "clips";
constexpr std::string_view FieldScaleKey = "field_scale";
constexpr std::string_view ChartKey = "chart";

constexpr std::int32_t NoRequirement = -1;
// The flags of a requirement row (EvoRequirements.flags).
constexpr std::int32_t BattlesAreMaximumFlag = 0x01;
constexpr std::int32_t CareIsMaximumFlag = 0x10;

constexpr std::int32_t HoursPerDay = 24;

std::int32_t optionalValue(YamlReader& reader, const YamlNode& node, std::string_view key) {
  return reader.optionalInteger<std::int32_t>(node, key).value_or(NoRequirement);
}

bool flag(YamlReader& reader, const YamlNode& node, std::string_view key) {
  const YamlNode* value = node.find(key);
  return value != nullptr && reader.boolean(*value);
}

std::vector<std::string> ids(YamlReader& reader, const YamlNode& node, std::string_view what) {
  std::vector<std::string> list;
  for (const YamlNode& item : reader.sequence(node, what)) {
    list.push_back(reader.text(item));
  }
  return list;
}

assets::EvolutionRequirementEntry readRequirement(YamlReader& reader, const YamlNode& node,
                                                  std::string_view species) {
  reader.expectKeys(node, RequirementKey, {},
                    {"from", "hp", "mp", "offense", "defense", "speed", "brains", "care",
                     "care_is_maximum", "weight", "discipline", "happiness", "battles",
                     "battles_are_maximum", "techs"});
  assets::EvolutionRequirementEntry row;
  row.target = std::string{species};
  if (const YamlNode* from = node.find("from")) {
    row.digimon = reader.text(*from);
  }
  row.hp = optionalValue(reader, node, "hp");
  row.mp = optionalValue(reader, node, "mp");
  row.offense = optionalValue(reader, node, "offense");
  row.defense = optionalValue(reader, node, "defense");
  row.speed = optionalValue(reader, node, "speed");
  row.brain = optionalValue(reader, node, "brains");
  row.care = optionalValue(reader, node, "care");
  row.weight = optionalValue(reader, node, "weight");
  row.discipline = optionalValue(reader, node, "discipline");
  row.happiness = optionalValue(reader, node, "happiness");
  row.battles = optionalValue(reader, node, "battles");
  row.techs = optionalValue(reader, node, "techs");
  row.flags = (flag(reader, node, "care_is_maximum") ? CareIsMaximumFlag : 0) |
              (flag(reader, node, "battles_are_maximum") ? BattlesAreMaximumFlag : 0);
  return row;
}

std::int32_t hourOf(YamlReader& reader, const YamlNode& node) {
  const auto hour = reader.integer<std::int32_t>(node);
  if (hour < 0 || hour >= HoursPerDay) {
    reader.fail(node, messages::formatMessage(messages::OutOfRange, "an hour", HoursPerDay - 1));
  }
  return hour;
}

SpeciesGate readGate(YamlReader& reader, const YamlNode& node) {
  reader.expectKeys(node, GateKey, {}, {"triggers", "hours", "cup"});
  SpeciesGate gate;
  if (const YamlNode* triggers = node.find("triggers")) {
    for (const YamlNode& item : reader.sequence(*triggers, "triggers")) {
      gate.triggers.push_back(reader.integer<std::int32_t>(item));
    }
  }
  if (const YamlNode* hours = node.find("hours")) {
    reader.expectKeys(*hours, "hours", {"from", "to"});
    gate.hours = HourRange{.from = hourOf(reader, reader.field(*hours, "from")),
                           .to = hourOf(reader, reader.field(*hours, "to"))};
  }
  if (const YamlNode* cup = node.find("cup")) {
    gate.cup = reader.integer<std::int32_t>(*cup);
  }
  return gate;
}

assets::EvolutionStatsGainEntry readStats(YamlReader& reader, const YamlNode& node,
                                          std::string_view species) {
  reader.expectKeys(node, StatsGainsKey, {"hp", "mp", "offense", "defense", "speed", "brains"});
  return {.target = std::string{species},
          .index = std::nullopt,
          .hp = reader.integer<std::int32_t>(reader.field(node, "hp")),
          .mp = reader.integer<std::int32_t>(reader.field(node, "mp")),
          .offense = reader.integer<std::int32_t>(reader.field(node, "offense")),
          .defense = reader.integer<std::int32_t>(reader.field(node, "defense")),
          .speed = reader.integer<std::int32_t>(reader.field(node, "speed")),
          .brains = reader.integer<std::int32_t>(reader.field(node, "brains")),
          .targetDigimon = std::string{species}};
}

RaisePatch readRaise(YamlReader& reader, const YamlNode& node) {
  reader.expectKeys(
      node, RaiseKey, {"copy"},
      {"favorite_food", "default_weight", "energy_cap", "energy_usage", "hunger_times"});
  RaisePatch patch{.copy = reader.text(reader.field(node, "copy"))};
  if (const YamlNode* food = node.find("favorite_food")) {
    patch.favoriteFood = reader.text(*food);
  }
  patch.defaultWeight = reader.optionalInteger<std::int32_t>(node, "default_weight");
  patch.energyCap = reader.optionalInteger<std::int32_t>(node, "energy_cap");
  patch.energyUsage = reader.optionalInteger<std::int32_t>(node, "energy_usage");
  if (const YamlNode* hunger = node.find("hunger_times")) {
    std::array<std::int32_t, 8> times{};
    const auto items = reader.sequence(*hunger, "hunger_times", times.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
      times.at(i) = reader.integer<std::int32_t>(items[i]);
    }
    patch.hungerTimes = times;
  }
  return patch;
}

std::uint32_t clipId(YamlReader& reader, const YamlNode& at, std::string_view text) {
  std::uint32_t value = 0;
  const auto [end, problem] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (problem != std::errc{} || end != text.data() + text.size()) {
    reader.fail(at, "a clip must be an animation id");
  }
  return value;
}

void readPatch(YamlReader& reader, const YamlNode& node, std::string_view species,
               SpeciesPatch& patch) {
  reader.expectKeys(node, species, {},
                    {RequirementKey, GateKey, EvolvesFromKey, EvolvesToKey, StatsGainsKey, RaiseKey,
                     MovesKey, PartnerTypeKey, PartnerMovesKey, EffectTypeKey, ClipsKey,
                     FieldScaleKey, ChartKey});
  if (const YamlNode* value = node.find(RequirementKey)) {
    patch.requirement = readRequirement(reader, *value, species);
  }
  if (const YamlNode* value = node.find(GateKey)) {
    patch.gate = readGate(reader, *value);
  }
  if (const YamlNode* value = node.find(EvolvesFromKey)) {
    patch.evolvesFrom = ids(reader, *value, EvolvesFromKey);
  }
  if (const YamlNode* value = node.find(EvolvesToKey)) {
    patch.evolvesTo = ids(reader, *value, EvolvesToKey);
  }
  if (const YamlNode* value = node.find(StatsGainsKey)) {
    patch.statsGains = readStats(reader, *value, species);
  }
  if (const YamlNode* value = node.find(RaiseKey)) {
    patch.raise = readRaise(reader, *value);
  }
  if (const YamlNode* value = node.find(MovesKey)) {
    patch.moves = ids(reader, *value, MovesKey);
  }
  if (const YamlNode* value = node.find(PartnerTypeKey)) {
    patch.partnerType = reader.integer<std::int32_t>(*value);
  }
  if (const YamlNode* value = node.find(PartnerMovesKey); value != nullptr && value->isMapping()) {
    for (const core::YamlEntry& entry : value->entries()) {
      reader.expectKeys(entry.value, entry.key, {}, {"power", "iframes"});
      patch.partnerMoves.insert_or_assign(
          entry.key,
          MoveOverride{.power = reader.optionalInteger<std::int32_t>(entry.value, "power"),
                       .iframes = reader.optionalInteger<std::int32_t>(entry.value, "iframes")});
    }
  }
  if (const YamlNode* value = node.find(EffectTypeKey)) {
    patch.effectType = reader.text(*value);
  }
  if (const YamlNode* value = node.find(ClipsKey); value != nullptr && value->isMapping()) {
    for (const core::YamlEntry& entry : value->entries()) {
      patch.clips.insert_or_assign(clipId(reader, entry.value, entry.key),
                                   reader.integer<std::uint32_t>(entry.value));
    }
  }
  if (const YamlNode* value = node.find(FieldScaleKey)) {
    patch.fieldScale = reader.number(*value);
  }
  if (const YamlNode* value = node.find(ChartKey)) {
    reader.expectKeys(*value, ChartKey, {"x", "y", "icon", "palette"});
    patch.chart =
        ChartCell{.x = reader.integer<std::int32_t>(reader.field(*value, "x")),
                  .y = reader.integer<std::int32_t>(reader.field(*value, "y")),
                  .icon = reader.text(reader.field(*value, "icon")),
                  .palette = reader.integer<std::int32_t>(reader.field(*value, "palette"))};
  }
}

// A field of the Mega step's entry replaces the same field of species_extra's.
void overlay(SpeciesPatch& base, SpeciesPatch over) {
  if (over.requirement) {
    base.requirement = std::move(over.requirement);
  }
  if (over.gate) {
    base.gate = std::move(over.gate);
  }
  if (over.evolvesFrom) {
    base.evolvesFrom = std::move(over.evolvesFrom);
  }
  if (over.evolvesTo) {
    base.evolvesTo = std::move(over.evolvesTo);
  }
  if (over.statsGains) {
    base.statsGains = std::move(over.statsGains);
  }
  if (over.raise) {
    base.raise = std::move(over.raise);
  }
  if (!over.moves.empty()) {
    base.moves = std::move(over.moves);
  }
  if (over.partnerType) {
    base.partnerType = over.partnerType;
  }
  for (auto& [move, value] : over.partnerMoves) {
    base.partnerMoves.insert_or_assign(move, value);
  }
  if (over.effectType) {
    base.effectType = std::move(over.effectType);
  }
  for (const auto& [clip, value] : over.clips) {
    base.clips.insert_or_assign(clip, value);
  }
  if (over.fieldScale) {
    base.fieldScale = over.fieldScale;
  }
  if (over.chart) {
    base.chart = std::move(over.chart);
  }
}

void readSpecies(YamlReader& reader, const YamlNode& root, SpeciesExtra& extra) {
  const YamlNode* species = root.find(SpeciesKey);
  if (species == nullptr) {
    return;
  }
  if (!species->isMapping()) {
    reader.fail(*species, "species must be a mapping of species ids");
    return;
  }
  for (const core::YamlEntry& entry : species->entries()) {
    SpeciesPatch patch;
    readPatch(reader, entry.value, entry.key, patch);
    if (const auto found = extra.species.find(entry.key); found != extra.species.end()) {
      overlay(found->second, std::move(patch));
    } else {
      extra.species.emplace(entry.key, std::move(patch));
    }
  }
}

core::Result<void> readExtraSection(const Section& section, SpeciesExtra& extra) {
  YamlReader reader;
  const YamlNode& root = section.node;
  reader.expectKeys(root, section.name, {}, {SpeciesKey, ExtraPrizesKey});
  readSpecies(reader, root, extra);
  extra.extraPrizes = root.find(ExtraPrizesKey) != nullptr;
  return reader.finish(std::monostate{}, section.file).transform([](auto) {});
}

core::Result<void> readMegaSection(const Section& section, SpeciesExtra& extra) {
  YamlReader reader;
  const YamlNode& root = section.node;
  reader.expectKeys(root, section.name,
                    {CheckHoursKey, LifetimeHoursKey, TamerLevelChanceKey, MegaKey}, {SpeciesKey});
  MegaStep step{.checkHours = reader.integer<std::int32_t>(reader.field(root, CheckHoursKey)),
                .lifetimeHours = reader.integer<std::int32_t>(reader.field(root, LifetimeHoursKey)),
                .tamerLevelChance =
                    reader.integer<std::int32_t>(reader.field(root, TamerLevelChanceKey)),
                .mega = ids(reader, reader.field(root, MegaKey), MegaKey)};
  readSpecies(reader, root, extra);
  extra.megaStep = std::move(step);
  return reader.finish(std::monostate{}, section.file).transform([](auto) {});
}

core::Error tableError(std::string_view table, std::string_view problem) {
  return core::Error{std::format("species_extra: {}: {}", table, problem)};
}

// The first free `to` slot after the last target of a path row, or nothing.
std::optional<std::size_t> slotAfterLast(const assets::EvolutionPathEntry& row) {
  std::size_t next = 0;
  for (std::size_t slot = 0; slot < row.to.size(); ++slot) {
    if (row.to.at(slot)) {
      next = slot + 1;
    }
  }
  return next < row.to.size() ? std::optional{next} : std::nullopt;
}

assets::EvolutionPathEntry& pathRow(assets::EvolutionTable& table, std::string_view species) {
  const auto found = std::ranges::find_if(
      table.paths, [&](const assets::EvolutionPathEntry& row) { return row.digimon == species; });
  if (found != table.paths.end()) {
    return *found;
  }
  table.paths.push_back({.digimon = std::string{species}, .index = std::nullopt});
  return table.paths.back();
}

template <typename Row>
void replaceOrAdd(std::vector<Row>& rows, Row row) {
  const auto found = std::ranges::find_if(
      rows, [&](const Row& existing) { return existing.target == row.target; });
  if (found != rows.end()) {
    row.index = found->index;
    *found = std::move(row);
  } else {
    rows.push_back(std::move(row));
  }
}

core::Result<void> addSource(assets::EvolutionPathEntry& row, std::string_view source) {
  if (std::ranges::find(row.from, std::optional<std::string>{source}) != row.from.end()) {
    return {};
  }
  const auto free = std::ranges::find_if(row.from, [](const auto& slot) { return !slot; });
  if (free == row.from.end()) {
    return std::unexpected{tableError("paths", std::format("{} has no free 'from' slot for {}",
                                                           row.digimon.value_or(""), source))};
  }
  *free = std::string{source};
  return {};
}

} // namespace

const SpeciesPatch* SpeciesExtra::find(std::string_view id) const {
  const auto found = species.find(id);
  return found == species.end() ? nullptr : &found->second;
}

bool SpeciesExtra::isMega(std::string_view id) const {
  return megaStep && std::ranges::find(megaStep->mega, id) != megaStep->mega.end();
}

core::Result<SpeciesExtra> readSpeciesExtra(const Ruleset& ruleset) {
  SpeciesExtra extra;
  if (const Section* section = ruleset.section(SpeciesExtraSection)) {
    if (auto read = readExtraSection(*section, extra); !read) {
      return std::unexpected{read.error()};
    }
  }
  if (const Section* section = ruleset.section(MegaStepSection)) {
    if (auto read = readMegaSection(*section, extra); !read) {
      return std::unexpected{read.error()};
    }
  }
  return extra;
}

core::Result<void> checkSpeciesExtraSection(const Section& section) {
  SpeciesExtra extra;
  return section.name == MegaStepSection ? readMegaSection(section, extra)
                                         : readExtraSection(section, extra);
}

core::Result<void> applySpeciesExtra(const SpeciesExtra& extra, assets::DigimonTable& table) {
  for (const auto& [id, patch] : extra.species) {
    if (patch.moves.empty()) {
      continue;
    }
    const auto row = std::ranges::find(table.digimon, id, &assets::DigimonRow::id);
    if (row == table.digimon.end()) {
      return std::unexpected{tableError("digimon", std::format("no species {}", id))};
    }
    for (const std::string& move : patch.moves) {
      if (std::ranges::find(row->moves, std::optional<std::string>{move}) != row->moves.end()) {
        continue;
      }
      const auto free = std::ranges::find_if(row->moves, [](const auto& slot) { return !slot; });
      if (free == row->moves.end()) {
        return std::unexpected{
            tableError("digimon", std::format("{} has no empty move slot for {}", id, move))};
      }
      *free = move;
    }
  }
  return {};
}

core::Result<void> applySpeciesExtra(const SpeciesExtra& extra, assets::RaiseTable& table) {
  for (const auto& [id, patch] : extra.species) {
    if (!patch.raise) {
      continue;
    }
    const assets::RaiseRow* source = table.find(patch.raise->copy);
    if (source == nullptr) {
      return std::unexpected{tableError(
          "raise", std::format("{} copies {}, which has no row", id, patch.raise->copy))};
    }
    assets::RaiseRow row = *source;
    row.digimon = id;
    row.index = std::nullopt;
    if (patch.raise->favoriteFood) {
      row.favoriteFood = patch.raise->favoriteFood;
    }
    if (patch.raise->defaultWeight) {
      row.defaultWeight = *patch.raise->defaultWeight;
    }
    if (patch.raise->energyCap) {
      row.energyCap = *patch.raise->energyCap;
    }
    if (patch.raise->energyUsage) {
      row.energyUsage = *patch.raise->energyUsage;
    }
    if (patch.raise->hungerTimes) {
      row.hungerTimes = *patch.raise->hungerTimes;
    }
    const auto found =
        std::ranges::find(table.raise, std::optional<std::string>{id}, &assets::RaiseRow::digimon);
    if (found != table.raise.end()) {
      row.index = found->index;
      *found = std::move(row);
    } else {
      table.raise.push_back(std::move(row));
    }
  }
  return {};
}

core::Result<void> applySpeciesExtra(const SpeciesExtra& extra, assets::EvolutionTable& table) {
  for (const auto& [id, patch] : extra.species) {
    if (patch.requirement) {
      replaceOrAdd(table.requirements, *patch.requirement);
    }
    if (patch.statsGains) {
      replaceOrAdd(table.statsGains, *patch.statsGains);
    }
    if (patch.evolvesTo) {
      assets::EvolutionPathEntry& own = pathRow(table, id);
      for (const std::string& target : *patch.evolvesTo) {
        if (std::ranges::find(own.to, std::optional<std::string>{target}) != own.to.end()) {
          continue;
        }
        const auto slot = slotAfterLast(own);
        if (!slot) {
          return std::unexpected{
              tableError("paths", std::format("{} has no free slot for {}", id, target))};
        }
        own.to.at(*slot) = target;
      }
    }
  }
  // The targets go in after every own row exists, so a new species can be a source.
  for (const auto& [id, patch] : extra.species) {
    if (!patch.evolvesFrom) {
      continue;
    }
    for (const std::string& source : *patch.evolvesFrom) {
      assets::EvolutionPathEntry& row = pathRow(table, source);
      if (std::ranges::find(row.to, std::optional<std::string>{id}) == row.to.end()) {
        const auto slot = slotAfterLast(row);
        if (!slot) {
          return std::unexpected{
              tableError("paths", std::format("{} has no free slot for {}", source, id))};
        }
        row.to.at(*slot) = id;
      }
      if (auto added = addSource(pathRow(table, id), source); !added) {
        return added;
      }
    }
  }
  return {};
}

namespace {

template <typename Table>
core::Result<Table>
loadPatched(const assets::VirtualFileSystem& vfs, const Ruleset& ruleset, std::string_view role,
            core::Result<Table> (*load)(const assets::VirtualFileSystem&, const assets::AssetId&)) {
  const assets::AssetId* id = ruleset.table(role);
  if (id == nullptr) {
    return core::fail(std::format("ruleset {} names no {} table", ruleset.id(), role));
  }
  auto table = load(vfs, *id);
  if (!table) {
    return table;
  }
  auto extra = readSpeciesExtra(ruleset);
  if (!extra) {
    return std::unexpected{extra.error()};
  }
  if (auto applied = applySpeciesExtra(*extra, *table); !applied) {
    return std::unexpected{applied.error()};
  }
  return table;
}

} // namespace

core::Result<assets::DigimonTable> loadPatchedDigimonTable(const assets::VirtualFileSystem& vfs,
                                                           const Ruleset& ruleset) {
  return loadPatched<assets::DigimonTable>(vfs, ruleset, "digimon", assets::loadDigimonTable);
}

core::Result<assets::RaiseTable> loadPatchedRaiseTable(const assets::VirtualFileSystem& vfs,
                                                       const Ruleset& ruleset) {
  return loadPatched<assets::RaiseTable>(vfs, ruleset, "raise", assets::loadRaiseTable);
}

core::Result<assets::EvolutionTable> loadPatchedEvolutionTable(const assets::VirtualFileSystem& vfs,
                                                               const Ruleset& ruleset) {
  return loadPatched<assets::EvolutionTable>(vfs, ruleset, "evolution", assets::loadEvolutionTable);
}

} // namespace dwe::ruleset
