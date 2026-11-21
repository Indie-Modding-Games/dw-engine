#include "coop_guest_return.hpp"

#include <dwe/core/log.hpp>
#include <dwe/game/partner/state_yaml.hpp>
#include <dwe/game/save/settings.hpp>

#include <format>
#include <utility>

#include "coop_guest_profile.hpp"

namespace dwe::app {
namespace {

constexpr std::string_view HostKey = "host";
constexpr std::string_view SpeciesKey = "species";
constexpr std::string_view PartnerSection = "partner";
constexpr std::string_view InventorySection = "inventory";
constexpr std::string_view LabelSeparator = " ";
constexpr std::int32_t MaxPool = 9999;
constexpr std::int32_t MaxStat = 999;

const game::save::SavedSection* sectionOf(const game::save::SaveFile& file, std::string_view name) {
  const auto found = file.sections.find(name);
  return found == file.sections.end() ? nullptr : &found->second;
}

bool sameSection(const game::save::SaveFile& a, const game::save::SaveFile& b,
                 std::string_view name) {
  const game::save::SavedSection* left = sectionOf(a, name);
  const game::save::SavedSection* right = sectionOf(b, name);
  if (left == nullptr || right == nullptr) {
    return left == right;
  }
  return left->version == right->version && left->state == right->state;
}

bool samePlaythrough(const game::save::SaveHeader& a, const game::save::SaveHeader& b) {
  return a.created == b.created && a.summary.tamer == b.summary.tamer && a.rulesetId == b.rulesetId;
}

} // namespace

game::save::SavedSection coopReturnSection(const CoopReturnNote& note) {
  core::YamlNode state = core::YamlNode::mapping();
  state.set(std::string{HostKey},
            core::YamlNode::scalar(note.host, core::YamlNode::ScalarStyle::Quoted));
  state.set(std::string{SpeciesKey},
            core::YamlNode::scalar(note.species, core::YamlNode::ScalarStyle::Quoted));
  return {.version = CoopReturnVersion, .state = std::move(state)};
}

std::optional<CoopReturnNote> coopReturnNoteOf(const game::save::SavedSection& section) {
  if (section.version != CoopReturnVersion || !section.state.isMapping()) {
    return std::nullopt;
  }
  const core::YamlNode* host = section.state.find(HostKey);
  const core::YamlNode* species = section.state.find(SpeciesKey);
  if (host == nullptr || species == nullptr || !host->isScalar() || !species->isScalar()) {
    return std::nullopt;
  }
  return CoopReturnNote{.host = host->text(), .species = species->text()};
}

core::Result<void> checkReturningPartner(const game::partner::PartnerState& partner) {
  using game::partner::Stat;
  if (partner.species.empty()) {
    return core::fail("the returning partner has no species");
  }
  for (const Stat stat : game::partner::AllStats) {
    const std::int32_t limit = stat == Stat::Hp || stat == Stat::Mp ? MaxPool : MaxStat;
    const std::int32_t value = partner.stats.base[stat];
    if (value < 0 || value > limit) {
      return core::fail(std::format("the returning partner's stat {} is {}, outside 0..{}",
                                    static_cast<int>(stat), value, limit));
    }
  }
  const auto& stats = partner.stats;
  if (stats.currentHp < 0 || stats.currentHp > stats.base[Stat::Hp] || stats.currentMp < 0 ||
      stats.currentMp > stats.base[Stat::Mp]) {
    return core::fail(std::format("the returning partner's HP {} or MP {} is past its stats",
                                  stats.currentHp, stats.currentMp));
  }
  if (partner.life.lives < 0 || partner.life.remainingLifetime < 0 || partner.life.age < 0 ||
      partner.weight < 0) {
    return core::fail("the returning partner has negative lives, lifetime, age or weight");
  }
  return {};
}

core::Result<game::save::SaveFile> coopGuestReturn(const game::save::SaveFile& home,
                                                   const game::save::SaveFile& brought,
                                                   const game::save::SaveFile& played,
                                                   std::string_view host,
                                                   std::chrono::sys_seconds now) {
  if (!samePlaythrough(home.header, brought.header)) {
    return core::fail("the guest's slot holds another game than the one it brought");
  }
  const game::save::SavedSection* partnerSection = sectionOf(played, PartnerSection);
  if (partnerSection == nullptr) {
    return core::fail("the guest's game has no partner to take home");
  }
  if (partnerSection->version != game::partner::PartnerStateVersion) {
    return core::fail(std::format("the guest's partner is at version {}, not {}",
                                  partnerSection->version, game::partner::PartnerStateVersion));
  }
  auto partner = game::partner::fromYaml(partnerSection->state);
  if (!partner) {
    return std::unexpected{partner.error()};
  }
  if (auto checked = checkReturningPartner(*partner); !checked) {
    return std::unexpected{checked.error()};
  }

  game::save::SaveFile result = home;
  for (const std::string_view name : CoopGuestOwnSections) {
    if (const game::save::SavedSection* own = sectionOf(played, name)) {
      result.sections.insert_or_assign(std::string{name}, *own);
    }
  }
  result.sections.insert_or_assign(
      std::string{CoopReturnSection},
      coopReturnSection({.host = std::string{host}, .species = partner->species}));
  result.header.summary.species = partner->species;
  result.header.label = partner->species;
  if (!result.header.summary.map.empty()) {
    result.header.label += LabelSeparator;
    result.header.label += result.header.summary.map;
  }
  result.header.updated = now;
  result.header.autosaveReasons.clear();
  return result;
}

CoopReturnChange coopReturnChange(const game::save::SaveFile& last,
                                  const game::save::SaveFile& played) {
  if (!sameSection(last, played, InventorySection)) {
    return CoopReturnChange::Bag;
  }
  for (const std::string_view name : CoopGuestOwnSections) {
    if (!sameSection(last, played, name)) {
      return CoopReturnChange::Partner;
    }
  }
  return CoopReturnChange::None;
}

core::Result<void> CoopGuestHome::begin(Home home, std::string host, double seconds) {
  auto brought = game::save::parseSaveFile(home.text);
  if (!brought) {
    return std::unexpected{brought.error()};
  }
  brought_ = *brought;
  written_ = std::move(*brought);
  home_ = std::move(home);
  host_ = std::move(host);
  writtenAt_ = seconds;
  failedAt_.reset();
  keptBefore_ = false;
  return {};
}

bool CoopGuestHome::due(const game::save::SaveFile& played, double seconds) const {
  if (!home_ || !written_ || (failedAt_ && seconds - *failedAt_ < PartnerEverySeconds)) {
    return false;
  }
  switch (coopReturnChange(*written_, played)) {
  case CoopReturnChange::None:
    return false;
  case CoopReturnChange::Bag:
    return true;
  case CoopReturnChange::Partner:
    return seconds - writtenAt_ >= PartnerEverySeconds;
  }
  return false;
}

bool CoopGuestHome::changed(const game::save::SaveFile& played) const {
  return home_ && written_ && coopReturnChange(*written_, played) != CoopReturnChange::None;
}

core::Result<std::string> CoopGuestHome::bringHome(game::save::SaveStorage& storage,
                                                   const game::save::SaveSettings& settings,
                                                   const game::save::SaveFile& played,
                                                   std::chrono::sys_seconds now, double seconds) {
  if (!home_ || !brought_) {
    return core::fail("no co-op guest game to take home");
  }
  game::save::SaveSlots slots{storage, settings};
  auto onDisk = home_->backup ? slots.loadBackup(home_->slot) : slots.load(home_->slot);
  if (!onDisk) {
    return core::fail(std::format("the guest's slot cannot be read: {}", onDisk.error().message));
  }
  auto returned = coopGuestReturn(*onDisk, *brought_, played, host_, now);
  if (!returned) {
    return std::unexpected{returned.error()};
  }
  const std::string slotName = game::save::slotFileName(home_->slot);
  if (!keptBefore_) {
    auto before = game::save::writeSaveFile(*onDisk);
    if (!before) {
      return std::unexpected{before.error()};
    }
    if (auto kept = storage.write(game::save::slotBeforeCoopName(home_->slot), *before, {});
        !kept) {
      return std::unexpected{kept.error()};
    }
    keptBefore_ = true;
  }
  if (auto saved = slots.save(home_->slot, *returned); !saved) {
    return std::unexpected{saved.error()};
  }
  auto text = game::save::writeSaveFile(*returned);
  if (!text) {
    return std::unexpected{text.error()};
  }
  home_->text = *text;
  home_->backup = false;
  brought_ = *returned;
  written_ = played;
  writtenAt_ = seconds;
  failedAt_.reset();
  core::logInfo("coop: the partner and the bag went home to {}", slotName);
  return std::move(*text);
}

void CoopGuestHome::end() {
  home_.reset();
  brought_.reset();
  written_.reset();
  host_.clear();
  failedAt_.reset();
  keptBefore_ = false;
}

} // namespace dwe::app
