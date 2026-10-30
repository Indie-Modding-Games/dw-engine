#include <dwe/core/log.hpp>
#include <dwe/game/partner/area_response.hpp>
#include <dwe/game/partner/rules.hpp>
#include <dwe/game/save/settings.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/session_music.hpp>
#include <dwe/session/session_sound.hpp>
#include <dwe/session/slot_names.hpp>
#include <dwe/session/story_adapter.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "save_sections.hpp"
#include "session_state.hpp"

namespace dwe::session {

namespace {

using game::partner::EventKind;
using game::partner::PartnerEvent;
using game::save::AutosaveReason;
using messages::formatMessage;

// The slot list's label of a save: the partner's species, then the map.
constexpr std::string_view LabelSeparator = " ";

// The roll of a chance in percent (tamer.c: addTamerLevel rolls randomLimit(100)).
constexpr std::int32_t PercentRoll = 100;

// The checks of every section the session's systems read.
ruleset::SectionChecks allChecks(const std::vector<std::unique_ptr<SessionSystem>>& systems) {
  ruleset::SectionChecks checks = game::partner::partnerSectionChecks();
  checks.insert_or_assign(std::string{game::save::SaveSectionName}, game::save::saveSectionCheck());
  for (auto& [name, check] : sessionSectionChecks()) {
    checks.insert_or_assign(name, std::move(check));
  }
  for (const std::unique_ptr<SessionSystem>& system : systems) {
    for (auto& [name, check] : system->sectionChecks()) {
      checks.insert_or_assign(name, std::move(check));
    }
  }
  return checks;
}

core::Result<ruleset::RulesetStore> loadStore(SessionDesc& desc) {
  ruleset::SectionChecks checks = allChecks(desc.systems);
  if (desc.rules) {
    return ruleset::RulesetStore::adopt(*desc.files, std::move(*desc.rules), std::move(checks));
  }
  return ruleset::RulesetStore::load(*desc.files, desc.rulesetId, std::move(checks));
}

core::Result<std::shared_ptr<const game::partner::SpeciesTable>>
speciesOf(const SessionDesc& desc, const ruleset::Ruleset& rules) {
  if (desc.species) {
    return desc.species;
  }
  auto species = game::partner::loadSpeciesTable(*desc.files, rules);
  if (!species) {
    return core::fail(formatMessage(messages::SpeciesFailed, species.error().message));
  }
  return std::make_shared<const game::partner::SpeciesTable>(std::move(*species));
}

// The music mode is the story's variable 245 (script_interp.c: handleMusicOverride, PSTAT_245);
// without a story it is 0, the day and night songs.
std::function<std::int64_t()>
musicModeOf(const std::vector<std::unique_ptr<SessionSystem>>& systems) {
  constexpr std::int32_t MusicModeVariable = 245;
  const StoryAdapter* story = nullptr;
  for (const auto& system : systems) {
    if (const auto* found = dynamic_cast<const StoryAdapter*>(system.get()); found != nullptr) {
      story = found;
    }
  }
  return [story]() -> std::int64_t {
    return story == nullptr ? 0 : story->state().variable(MusicModeVariable).value_or(0);
  };
}

} // namespace

std::optional<FieldPlace> SessionState::currentPlace() const {
  if (field != nullptr && !field->mapId().empty()) {
    const game::field::CharacterState player = field->player();
    return FieldPlace{.map = field->mapId(),
                      .spawn = {.position = player.position, .yaw = player.yaw}};
  }
  return savedPlace;
}

core::Result<std::unique_ptr<GameSession>> GameSession::create(SessionDesc desc) {
  if (desc.files == nullptr) {
    return core::fail(std::string{messages::NoFiles});
  }
  const std::string rulesetId = desc.rules ? desc.rules->id() : desc.rulesetId;
  auto store = loadStore(desc);
  if (!store) {
    return core::fail(formatMessage(messages::RulesetFailed, rulesetId, store.error().message));
  }
  std::unique_ptr<GameSession> session{new GameSession{std::move(desc), std::move(*store)}};
  if (auto ready = session->setUp(); !ready) {
    return std::unexpected{ready.error()};
  }
  return session;
}

GameSession::GameSession(SessionDesc desc, ruleset::RulesetStore store)
    : desc_(std::move(desc)), store_(std::move(store)), seed_(desc_.seed) {
  if (desc_.sound != nullptr && desc_.files != nullptr) {
    sound_ = std::make_unique<SessionSound>(*desc_.sound, *desc_.files);
    sound_->attachPartner(
        [this] { return state_ ? state_->partner.state().species : std::string{}; });
    music_ = std::make_unique<SessionMusic>(
        *desc_.sound, *desc_.files, MusicFacts{.mode = musicModeOf(desc_.systems), .hour = [this] {
                                                 return state_ ? state_->clock.time().hour : 0;
                                               }});
  }
}

GameSession::~GameSession() = default;

game::story::SoundPort* GameSession::sound() {
  return sound_.get();
}

SessionMusic* GameSession::music() {
  return music_.get();
}

void GameSession::useSceneSounds(std::optional<std::int32_t> soundId) {
  if (sound_) {
    sound_->useSceneBank(soundId);
  }
}

void GameSession::useBattleSounds(bool inBattle) {
  if (sound_) {
    sound_->useBattleBank(inBattle);
  }
}

void GameSession::useEnemySounds(std::span<const std::string> species) {
  if (sound_) {
    sound_->useEnemyBanks(species);
  }
}

core::Result<void> GameSession::setUp() {
  const ruleset::Ruleset& current = store_.current();
  auto species = speciesOf(desc_, current);
  if (!species) {
    return std::unexpected{species.error()};
  }
  species_ = std::move(*species);
  if (desc_.files != nullptr) {
    auto starting = assets::loadNewGameStats(*desc_.files);
    if (!starting) {
      return core::fail(formatMessage(messages::NewGameStatsFailed, starting.error().message));
    }
    newGameStats_ = std::move(*starting);
  }
  auto clockRules = ruleset::readClockRules(current);
  if (!clockRules) {
    return std::unexpected{clockRules.error()};
  }
  auto partnerRules = game::partner::loadPartnerRules(current);
  if (!partnerRules) {
    return std::unexpected{partnerRules.error()};
  }
  state_ = std::make_unique<SessionState>(*clockRules, std::move(*partnerRules), *species_, seed_);
  if (desc_.storage != nullptr) {
    auto settings = game::save::loadSaveSettings(*desc_.files);
    if (!settings) {
      return core::fail(formatMessage(messages::SaveSettingsFailed, settings.error().message));
    }
    slots_.emplace(*desc_.storage, *settings);
    autosave_.emplace(game::save::AutosaveRules{}, settings->autosaveSlots);
  }
  if (auto read = readRules(); !read) {
    return read;
  }
  rulesRevision_ = store_.revision();
  return registerSaveSections();
}

core::Result<void> GameSession::readRules() {
  const ruleset::Ruleset& current = store_.current();
  auto clockRules = ruleset::readClockRules(current);
  if (!clockRules) {
    return std::unexpected{clockRules.error()};
  }
  auto partnerRules = game::partner::loadPartnerRules(current);
  if (!partnerRules) {
    return std::unexpected{partnerRules.error()};
  }
  auto sessionRules = readSessionRules(current);
  if (!sessionRules) {
    return std::unexpected{sessionRules.error()};
  }
  auto autosaveRules = game::save::readAutosaveRules(current);
  if (!autosaveRules) {
    return std::unexpected{autosaveRules.error()};
  }
  state_->clock.setRules(*clockRules);
  state_->partnerRules = std::move(*partnerRules);
  state_->partner.setRules(state_->partnerRules);
  state_->rules = std::move(*sessionRules);
  if (autosave_) {
    autosave_->setRules(*autosaveRules);
  }
  for (const std::unique_ptr<SessionSystem>& system : desc_.systems) {
    if (auto read = system->readRules(current); !read) {
      return read;
    }
  }
  return {};
}

void GameSession::followRules() {
  if (store_.revision() == rulesRevision_) {
    return;
  }
  rulesRevision_ = store_.revision();
  if (auto read = readRules(); !read) {
    core::logError("{}", formatMessage(messages::RulesRefused, read.error().message));
    return;
  }
  core::logInfo("{}", formatMessage(messages::RulesChanged, rulesRevision_));
}

core::Result<void> GameSession::registerSaveSections() {
  serializers_ = std::make_unique<sections::SessionSerializers>(*state_);
  if (auto added = serializers_->addTo(sections_); !added) {
    return added;
  }
  for (const std::unique_ptr<SessionSystem>& system : desc_.systems) {
    if (auto added = system->addSaveSections(sections_); !added) {
      return added;
    }
  }
  return {};
}

game::partner::PartnerStats GameSession::startingStats(const NewGameRules& start) const {
  using game::partner::Stat;
  if (!newGameStats_) {
    // No extracted data: the ruleset's own stats, with HP and MP full.
    return {.base = start.stats,
            .currentHp = start.stats[Stat::Hp],
            .currentMp = start.stats[Stat::Mp]};
  }
  // What the new-game script sets (dg 193: SET_STAT for the partner's type, or the other run).
  const assets::StartingStats& set = newGameStats_->statsOf(start.partner);
  game::partner::PartnerStats stats{.base = {}, .currentHp = set.hp, .currentMp = set.mp};
  stats.base[Stat::Hp] = set.maxHp;
  stats.base[Stat::Mp] = set.maxMp;
  stats.base[Stat::Offense] = set.offense;
  stats.base[Stat::Defense] = set.defense;
  stats.base[Stat::Speed] = set.speed;
  stats.base[Stat::Brain] = set.brains;
  return stats;
}

core::Result<void> GameSession::startNewGame(const NewGameChoice& choice) {
  NewGameRules start = state_->rules.newGame;
  if (!choice.partner.empty()) {
    // main.c: main raises 3 or 0x11 as the opening's answer picked (readPStat(0xFE)).
    core::logDebug("[port:story] main (main.c:3445): the opening picked partner {}",
                   choice.partner);
    start.partner = choice.partner;
  }
  ruleset::GameClock& clock = state_->clock;
  const double speed = clock.speed();
  const bool running = clock.running();
  clock = ruleset::GameClock{clock.rules()};
  clock.setSpeed(speed);
  clock.setRunning(running);

  const game::partner::PartnerStats stats = startingStats(start);
  if (species_->find(start.partner) == nullptr) {
    return core::fail(formatMessage(messages::UnknownStartSpecies, start.partner));
  }
  // The same seed starts the same playthrough, whatever came before in this session.
  state_->partner.restoreRandomState(ruleset::Random{seed_}.state());
  state_->random.reseed(seed_ + SystemsSeedOffset);
  if (auto started = state_->partner.start(start.partner, stats, clock); !started) {
    return started;
  }
  state_->tamer = {.level = start.tamerLevel};
  state_->savedPlace.reset();
  file_ = {};
  file_.header.created = now();
  homeSlot_.reset();
  playSeconds_ = 0.0;
  names_ = {.tamer = choice.tamerName, .partner = choice.partnerName};
  partnerEvents_.clear();
  tamerLevelAward_.reset();
  state_->playing = true;
  for (const std::unique_ptr<SessionSystem>& system : desc_.systems) {
    if (auto started = system->startNewGame(*this); !started) {
      state_->playing = false;
      return started;
    }
  }
  routePartnerEvents();
  return {};
}

core::Result<void> GameSession::load(game::save::SlotId slot, bool fromBackup) {
  if (!slots_) {
    return core::fail(std::string{messages::NoStorage});
  }
  auto file = fromBackup ? slots_->loadBackup(slot) : slots_->load(slot);
  if (!file) {
    const std::string name =
        fromBackup ? game::save::slotBackupName(slot) : game::save::slotFileName(slot);
    return core::fail(formatMessage(messages::LoadFailed, name, file.error().message));
  }
  if (auto loaded = loadFile(*file); !loaded) {
    return loaded;
  }
  if (slot.kind == game::save::SlotKind::Manual) {
    homeSlot_ = slot;
  }
  return {};
}

core::Result<void> GameSession::loadFile(const game::save::SaveFile& file) {
  if (file.header.rulesetId != store_.current().id()) {
    if (auto selected = store_.select(file.header.rulesetId); !selected) {
      return core::fail(
          formatMessage(messages::RulesetFailed, file.header.rulesetId, selected.error().message));
    }
    followRules();
  }
  state_->playing = false;
  if (auto read = sections_.read(file); !read) {
    return read;
  }
  file_ = file;
  homeSlot_ = manualSlotFromName(file.header.summary.slot);
  names_ = {.tamer = file.header.summary.tamer, .partner = file.header.summary.nickname};
  playSeconds_ = static_cast<double>(file.header.playSeconds);
  partnerEvents_.clear();
  tamerLevelAward_.reset();
  state_->playing = true;
  for (const std::unique_ptr<SessionSystem>& system : desc_.systems) {
    system->afterLoad(*this);
  }
  return {};
}

core::Result<game::save::SaveFile> GameSession::saveFile() const {
  if (!state_->playing) {
    return core::fail(std::string{messages::NoPartner});
  }
  game::save::SaveFile file = file_;
  file.header = headerNow();
  if (auto written = sections_.write(file); !written) {
    return std::unexpected{written.error()};
  }
  return file;
}

core::Result<void> GameSession::save(game::save::SlotId slot) {
  if (!slots_) {
    return core::fail(std::string{messages::NoStorage});
  }
  const std::optional<game::save::SlotId> previousHome = homeSlot_;
  if (slot.kind == game::save::SlotKind::Manual) {
    homeSlot_ = slot;
  }
  if (state_->playing) {
    for (const std::unique_ptr<SessionSystem>& system : desc_.systems) {
      system->beforeSave(*this);
    }
  }
  auto file = saveFile();
  if (!file) {
    homeSlot_ = previousHome;
    return core::fail(
        formatMessage(messages::SaveFailed, game::save::slotFileName(slot), file.error().message));
  }
  if (auto saved = slots_->save(slot, *file); !saved) {
    homeSlot_ = previousHome;
    return core::fail(
        formatMessage(messages::SaveFailed, game::save::slotFileName(slot), saved.error().message));
  }
  file_ = std::move(*file);
  return {};
}

bool GameSession::playing() const {
  return state_->playing;
}

void GameSession::tick(double realSeconds) {
  partnerEvents_.clear();
  followRules();
  followMap();
  advanceGameTime(realSeconds);
  routePartnerEvents();
  advanceAutosaveWindow(realSeconds);
  writeRequestedAutosave();
}

void GameSession::advanceGameTime(double realSeconds) {
  // The field stops game time during a map change (tamer.c: stopGameTime); without a field it
  // always runs.
  const bool timeRuns = state_->field == nullptr || state_->field->gameTimeRan();
  const ruleset::ClockAdvance advance =
      timeRuns ? state_->clock.advance(realSeconds) : ruleset::ClockAdvance{};
  if (!state_->playing) {
    return;
  }
  playSeconds_ += realSeconds;
  // clock.c: tickGameClock ends with updateBGM while game time runs.
  if (timeRuns && music_) {
    music_->updateBgm();
  }
  state_->partner.tick(state_->clock, advance, careContext());
  for (const std::unique_ptr<SessionSystem>& system : desc_.systems) {
    system->tick(*this, advance);
  }
}

bool GameSession::playerFree() const {
  const game::field::Field* field = state_->field;
  return field == nullptr || (field->gameTimeRan() && !field->mapChangePending() &&
                              !field->heldByScript() && !field->heldForCare());
}

game::partner::CareContext GameSession::careContext() const {
  // partner_impl.c: tickDeathCondition and tickConditions wait for tamerGetState() == 0 and
  // IS_SCRIPT_PAUSED == 1, and run only in the partner's normal state, not in a care scene
  // (partner.c: partnerTickNormal). A field plays the partner's pile scene (partner.c:
  // tickWildPoop).
  const game::field::Field* field = state_->field;
  return {.tamerIdle = playerFree(),
          .scriptIdle = !scriptRunning_,
          .area = area_,
          .partnerNormal = field == nullptr || !field->heldForCare(),
          .fieldScenes = field != nullptr};
}

void GameSession::attachField(const game::field::Field& field) {
  state_->field = &field;
  lastMap_ = field.mapId();
  // followMap only sees later maps: slot 8 takes the bank of the map the field starts on here.
  if (sound_) {
    sound_->enterMap(lastMap_);
  }
  area_ = areaOfFieldMap();
}

void GameSession::detachField() {
  state_->savedPlace = state_->currentPlace();
  state_->field = nullptr;
  lastMap_.clear();
  area_ = game::partner::AreaResponse::Neutral;
}

const std::optional<FieldPlace>& GameSession::savedPlace() const {
  return state_->savedPlace;
}

std::optional<FieldPlace> GameSession::currentPlace() const {
  return state_->currentPlace();
}

void GameSession::followMap() {
  if (state_->field == nullptr || state_->field->mapId() == lastMap_) {
    return;
  }
  const bool changed = !lastMap_.empty();
  lastMap_ = state_->field->mapId();
  if (sound_) {
    sound_->enterMap(lastMap_);
  }
  // The original works the area out on every map load (map.c: loadMap), also when a script loads
  // the map the player is on again; only a new map id gets here, so a species that changed on the
  // map keeps the old response until the next different map.
  area_ = areaOfFieldMap();
  if (!changed || !state_->playing) {
    return;
  }
  // partner.c: setupPartnerOnWarp starts the area effects again on every map.
  state_->partner.enterMap();
  requestAutosave(AutosaveReason::MapChange);
}

game::partner::AreaResponse GameSession::areaOfFieldMap() const {
  if (state_->field == nullptr) {
    return game::partner::AreaResponse::Neutral;
  }
  return game::partner::areaResponseOf(state_->field->regions(),
                                       state_->partner.species().favoredRegion);
}

void GameSession::routePartnerEvents() {
  if (!state_->playing) {
    return;
  }
  // Routing can cause more events (a death's rebirth): they are routed in turn.
  for (std::vector<PartnerEvent> events = state_->partner.takeEvents(); !events.empty();
       events = state_->partner.takeEvents()) {
    for (const PartnerEvent& event : events) {
      routePartnerEvent(event);
      for (const std::unique_ptr<SessionSystem>& system : desc_.systems) {
        system->routePartnerEvent(*this, event);
      }
      partnerEvents_.push_back(event);
    }
  }
}

void GameSession::routePartnerEvent(const PartnerEvent& event) {
  switch (event.kind) {
  case EventKind::Slept:
    requestAutosave(AutosaveReason::PartnerSleep);
    break;
  case EventKind::NewDay:
    requestAutosave(AutosaveReason::NewDay);
    break;
  case EventKind::TamerLevelChanged:
    changeTamerLevel(event.value);
    break;
  case EventKind::Died:
    if (rebirthByStory_) {
      core::logDebug("[port:death] tickDeathCondition (partner_impl.c:1918): the story plays the "
                     "death and asks for the rebirth");
    } else {
      rebirthPartner();
    }
    break;
  default:
    break;
  }
}

void GameSession::addTamerLevel(std::int32_t chancePercent, std::int32_t amount) {
  if (state_->random.below(PercentRoll) >= chancePercent) {
    return;
  }
  // tamer.c: addTamerLevel announces the change when the new level, before it is kept within the
  // limits, is still within them (TAMER_LEVEL_AWARD_PENDING; up for a positive amount).
  const TamerRules& rules = state_->rules.tamer;
  const std::int32_t level = state_->tamer.level + amount;
  if (level >= rules.levelMin && level <= rules.levelMax) {
    tamerLevelAward_ = amount >= 1 ? TamerLevelChange::Up : TamerLevelChange::Down;
    core::logDebug("[port:medals] addTamerLevel (tamer.c:1309): level {} -> {}, announced",
                   state_->tamer.level, level);
  }
  changeTamerLevel(amount);
}

void GameSession::setTamerLevel(std::int32_t level) {
  changeTamerLevel(level - state_->tamer.level);
}

void GameSession::setTamerRecord(const TamerRecord& record) {
  state_->tamer.record = record;
}

void GameSession::changeTamerLevel(std::int32_t change) {
  // tamer.c: addTamerLevel keeps the level within its limits.
  const TamerRules& rules = state_->rules.tamer;
  state_->tamer.level = std::clamp(state_->tamer.level + change, rules.levelMin, rules.levelMax);
}

void GameSession::rebirthPartner(std::optional<std::size_t> egg) {
  if (auto reborn = state_->partner.rebirth(state_->clock, state_->tamer.level, egg); !reborn) {
    core::logError("{}", formatMessage(messages::RebirthFailed, reborn.error().message));
    return;
  }
  state_->tamer.raised = std::min(state_->tamer.raised + 1, MaxRaisedCount);
  requestAutosave(AutosaveReason::PartnerReborn);
}

void GameSession::afterCommand() {
  routePartnerEvents();
  writeRequestedAutosave();
}

void GameSession::eatPoop(std::int32_t pileSize) {
  state_->partner.eatPoop(pileSize);
  afterCommand();
}

void GameSession::leaveWildPile() {
  state_->partner.leaveWildPile();
  afterCommand();
}

void GameSession::finishWildPoop() {
  state_->partner.finishWildPoop();
  afterCommand();
}

void GameSession::afterSystemCommand() {
  afterCommand();
}

PraiseOrScold GameSession::praiseOrScold(bool scolded) const {
  const game::partner::MoodState& mood = state_->partner.state().mood;
  return {
      .scolded = scolded, .disciplineBefore = mood.discipline, .happinessBefore = mood.happiness};
}

void GameSession::afterPraiseOrScold(const PraiseOrScold& event) {
  for (const std::unique_ptr<SessionSystem>& system : desc_.systems) {
    system->afterPraiseOrScold(*this, event);
  }
  afterCommand();
}

void GameSession::requestAutosave(AutosaveReason reason) {
  if (autosave_) {
    autosave_->requestAutosave(reason);
  }
}

std::optional<Autosave> GameSession::takeAutosaved() {
  return std::exchange(autosaved_, std::nullopt);
}

void GameSession::advanceAutosaveWindow(double realSeconds) {
  if (autosave_) {
    autosave_->advance(static_cast<float>(realSeconds));
  }
}

void GameSession::writeRequestedAutosave() {
  if (!autosave_) {
    return;
  }
  std::optional<std::vector<AutosaveReason>> reasons = autosave_->takeRequest();
  if (!reasons || !slots_ || !state_->playing) {
    return;
  }
  auto file = saveFile();
  if (!file) {
    core::logError("{}", formatMessage(messages::AutosaveFailed, file.error().message));
    return;
  }
  file->header.autosaveReasons = game::save::autosaveReasonNames(*reasons);
  auto written = slots_->autosave(*file);
  if (!written) {
    core::logError("{}", formatMessage(messages::AutosaveFailed, written.error().message));
    return;
  }
  file_ = std::move(*file);
  autosaved_ = Autosave{.slot = *written, .reasons = std::move(*reasons)};
  core::logInfo("{}", formatMessage(messages::Autosaved, game::save::slotFileName(*written)));
}

game::save::SaveHeader GameSession::headerNow() const {
  const std::optional<FieldPlace> place = state_->currentPlace();
  game::save::SaveSummary summary{.slot = homeSlot_ ? slotName(*homeSlot_) : std::string{},
                                  .tamer = names_.tamer,
                                  .species = state_->partner.state().species,
                                  .nickname = names_.partner,
                                  .map = place ? place->map : std::string{}};
  std::string label = summary.species;
  if (place) {
    label += LabelSeparator;
    label += place->map;
  }
  return {.gameVersion = desc_.gameVersion,
          .rulesetId = store_.current().id(),
          .created = file_.header.created,
          .updated = now(),
          .playSeconds = static_cast<std::int64_t>(std::floor(playSeconds_)),
          .label = std::move(label),
          .summary = std::move(summary),
          .autosaveReasons = {}};
}

std::chrono::sys_seconds GameSession::now() const {
  return desc_.wallClock ? desc_.wallClock() : std::chrono::sys_seconds{};
}

ruleset::GameClock& GameSession::clock() {
  return state_->clock;
}

const ruleset::GameClock& GameSession::clock() const {
  return state_->clock;
}

const SessionRules& GameSession::sessionRules() const {
  return state_->rules;
}

const game::partner::PartnerRules& GameSession::partnerRules() const {
  return state_->partnerRules;
}

game::partner::Partner& GameSession::partner() {
  return state_->partner;
}

const game::partner::Partner& GameSession::partner() const {
  return state_->partner;
}

PartnerView GameSession::partnerView() const {
  PartnerView view = viewOf(state_->partner, state_->partnerRules.care);
  view.nickname = names_.partner;
  view.area = area_;
  return view;
}

const TamerState& GameSession::tamer() const {
  return state_->tamer;
}

ruleset::Random& GameSession::random() {
  return state_->random;
}

std::vector<PartnerEvent> GameSession::takePartnerEvents() {
  return std::exchange(partnerEvents_, {});
}

bool GameSession::refuses(const game::partner::ItemOffer& offer) {
  const bool refused = state_->partner.refuses(offer);
  afterCommand();
  return refused;
}

void GameSession::feed(const game::partner::FoodEffect& food) {
  state_->partner.eat(food, state_->clock);
  afterCommand();
}

bool GameSession::cure(const game::partner::CureEffect& cure) {
  const bool cured = state_->partner.cure(cure);
  afterCommand();
  return cured;
}

void GameSession::scold() {
  const PraiseOrScold event = praiseOrScold(true);
  state_->partner.scold();
  afterPraiseOrScold(event);
}

void GameSession::praise() {
  const PraiseOrScold event = praiseOrScold(false);
  state_->partner.praise();
  afterPraiseOrScold(event);
}

void GameSession::useToilet() {
  state_->partner.useToilet();
  afterCommand();
}

core::Result<void> GameSession::sleep(const game::partner::SleepContext& context) {
  if (auto slept = fallAsleep(context); !slept) {
    return slept;
  }
  wakeUp();
  return {};
}

core::Result<void> GameSession::fallAsleep(const game::partner::SleepContext& context) {
  if (!state_->partner.canSleep()) {
    return core::fail(std::string{messages::NotSleepy});
  }
  state_->partner.sleep(state_->clock, context);
  afterCommand();
  return {};
}

void GameSession::wakeUp() {
  for (const std::unique_ptr<SessionSystem>& system : desc_.systems) {
    system->afterWakeUp(*this);
  }
  afterCommand();
}

core::Result<game::partner::TrainingResult>
GameSession::train(const game::partner::TrainingRequest& request) {
  auto trained = state_->partner.train(request, state_->clock);
  afterCommand();
  return trained;
}

} // namespace dwe::session
