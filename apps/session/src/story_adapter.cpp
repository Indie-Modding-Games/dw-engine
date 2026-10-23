#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/evolution/tables.hpp>
#include <dwe/game/field/rules.hpp>
#include <dwe/game/field/script_warps.hpp>
#include <dwe/game/story/story_state_yaml.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/session_music.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_music_api.hpp>

#include <cstdint>
#include <limits>
#include <set>
#include <utility>

namespace dwe::session {

using messages::formatMessage;

namespace {

bool isRaisedTrigger(std::int32_t trigger) {
  return trigger >= FirstRaisedTrigger && trigger < FirstRaisedTrigger + RaisedSpeciesLimit;
}

constexpr std::string_view TicksPerFrameKey = "ticks_per_frame";
constexpr std::string_view VariableMaxKey = "variable_max";
constexpr std::string_view FrozenVariableKey = "frozen_variable";
constexpr std::string_view FrozenValueKey = "frozen_value";
constexpr std::string_view TalkFacingTriggerKey = "talk_facing_trigger";
constexpr std::string_view ShowNameVariableKey = "show_name_variable";
constexpr std::string_view ShowNameByEventKey = "show_name_by_event";
constexpr std::string_view ShowNameAlwaysKey = "show_name_always";
constexpr std::string_view ShowNameNeverKey = "show_name_never";
// An engine guard, not a rule: past a second per frame of the original the story would crawl.
constexpr std::int64_t MaxTicksPerFrame = 60;
// A story variable is one byte in the state and in saves (script.h:158: ScriptState.pstats).
constexpr std::int64_t MaxVariableMax = std::numeric_limits<std::uint8_t>::max();

// loadSavegame copies back only the saved variables (main_menu.c:5058) and leaves the others as
// they were in memory. The engine starts them at 0, as a freshly started original holds them
// (script_interp.c:2208: initializeScripts), so a load is the same whatever was played before: an
// optional improvement. false keeps the original's carry-over.
constexpr bool ResetUnsavedVariablesOnLoad = true;

// A number of the story state from the section: 0 to `count` - 1, or a value 0 to the byte's.
std::int64_t stateNumber(assets::YamlReader& reader, const core::YamlNode& root,
                         std::string_view key, std::int64_t count) {
  const core::YamlNode& node = reader.field(root, key);
  const auto value = reader.integer<std::int64_t>(node);
  if (reader.ok() && (value < 0 || value >= count)) {
    reader.fail(node, formatMessage(messages::StoryNumberOutside, key, value, count - 1));
  }
  return value;
}

bool sameFlow(const game::story::StoryFlowRules& a, const game::story::StoryFlowRules& b) {
  return a.frozenVariable == b.frozenVariable && a.frozenValue == b.frozenValue &&
         a.talkFacingTrigger == b.talkFacingTrigger && a.showNameVariable == b.showNameVariable &&
         a.showNameByEvent == b.showNameByEvent && a.showNameAlways == b.showNameAlways &&
         a.showNameNever == b.showNameNever;
}

// A trigger or variable number the state can hold; any other becomes -1, which it refuses.
std::int32_t stateIndex(std::int64_t index) {
  return std::in_range<std::int32_t>(index) ? static_cast<std::int32_t>(index) : -1;
}

} // namespace

core::Result<StorySectionRules> readStoryRules(const ruleset::Section& section) {
  assets::YamlReader reader;
  const core::YamlNode& root = section.node;
  if (!reader.expectKeys(root, messages::StorySectionText,
                         {TicksPerFrameKey, VariableMaxKey, FrozenVariableKey, FrozenValueKey,
                          TalkFacingTriggerKey, ShowNameVariableKey, ShowNameByEventKey,
                          ShowNameAlwaysKey, ShowNameNeverKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  const core::YamlNode& ticksNode = reader.field(root, TicksPerFrameKey);
  const auto ticks = reader.integer<std::int64_t>(ticksNode);
  if (reader.ok() && (ticks < 1 || ticks > MaxTicksPerFrame)) {
    reader.fail(ticksNode, formatMessage(messages::StoryTicksPerFrame, ticks, MaxTicksPerFrame));
  }
  const core::YamlNode& maxNode = reader.field(root, VariableMaxKey);
  const auto variableMax = reader.integer<std::int64_t>(maxNode);
  if (reader.ok() && (variableMax < 1 || variableMax > MaxVariableMax)) {
    reader.fail(maxNode, formatMessage(messages::StoryVariableMax, variableMax, MaxVariableMax));
  }
  constexpr std::int64_t Variables = game::story::VariableCount;
  constexpr std::int64_t Values = MaxVariableMax + 1;
  const game::story::StoryFlowRules flow{
      .frozenVariable = stateNumber(reader, root, FrozenVariableKey, Variables),
      .frozenValue = stateNumber(reader, root, FrozenValueKey, Values),
      .talkFacingTrigger =
          stateNumber(reader, root, TalkFacingTriggerKey, game::story::TriggerCount),
      .showNameVariable = stateNumber(reader, root, ShowNameVariableKey, Variables),
      .showNameByEvent = stateNumber(reader, root, ShowNameByEventKey, Values),
      .showNameAlways = stateNumber(reader, root, ShowNameAlwaysKey, Values),
      .showNameNever = stateNumber(reader, root, ShowNameNeverKey, Values)};
  const StorySectionRules rules{.ticksPerFrame = static_cast<std::uint32_t>(ticks),
                                .rules = {.variableMax = variableMax},
                                .flow = flow};
  return reader.finish(rules, section.file);
}

// The story orders' view of the adapter's state (script_interp.c: isTriggerSet, setTrigger,
// unsetTrigger, readPStat and writePStat).
class StoryAdapter::StatePort final : public game::story::StoryStatePort {
public:
  explicit StatePort(game::story::StoryState& state) : state_(&state) {}

  [[nodiscard]] std::int64_t triggerCount() const override { return game::story::TriggerCount; }
  [[nodiscard]] std::int64_t variableCount() const override { return game::story::VariableCount; }

  [[nodiscard]] core::Result<bool> trigger(std::int64_t trigger) const override {
    return state_->trigger(stateIndex(trigger));
  }

  core::Result<void> setTrigger(std::int64_t trigger, bool set) override {
    core::logDebug("[port:story] {} (script_interp.c:{}): trigger {}",
                   set ? "setTrigger" : "unsetTrigger", set ? 2430 : 2441, trigger);
    return state_->setTrigger(stateIndex(trigger), set);
  }

  [[nodiscard]] core::Result<std::int64_t> variable(std::int64_t variable) const override {
    const auto value = state_->variable(stateIndex(variable));
    if (!value) {
      return std::unexpected{value.error()};
    }
    return std::int64_t{*value};
  }

  core::Result<void> setVariable(std::int64_t variable, std::int64_t value) override {
    // StoryOrders keeps a value within variable_max, which fits a byte.
    DWE_ASSERT(std::in_range<std::uint8_t>(value));
    core::logDebug("[port:story] writePStat (script_interp.c:2775): variable {} = {}", variable,
                   value);
    return state_->setVariable(stateIndex(variable), static_cast<std::uint8_t>(value));
  }

private:
  game::story::StoryState* state_;
};

// The session's random numbers as the original's rand() gave them, 0 to 0x7FFF (math.c:5:
// randomLimit draws from it). Bound to the session at each tick, the only time scripts run.
class StoryAdapter::RandomPort final : public game::story::StoryRandomPort {
public:
  void bind(ruleset::Random& source) { source_ = &source; }

  std::int64_t next() override {
    DWE_ASSERT(source_ != nullptr);
    // The high bits of the session's generator are its best ones.
    constexpr std::uint32_t DroppedBits = 17;
    const auto drawn = std::int64_t{source_->next() >> DroppedBits};
    core::logDebug("[port:story] rand (math.c:5): {}", drawn);
    return drawn;
  }

private:
  ruleset::Random* source_ = nullptr;
};

// The VM is built before any session, so its sound orders go through here to the session's sound
// slots of the tick (GameSession::sound()); none bound, or a silent session, plays nothing.
class StoryAdapter::SoundRelay final : public game::story::SoundPort {
public:
  void bind(game::story::SoundPort* target) { target_ = target; }

  void playSound(std::int64_t slot, std::int64_t sound) override {
    if (target_ != nullptr) {
      target_->playSound(slot, sound);
    }
  }

private:
  game::story::SoundPort* target_ = nullptr;
};

// The same for the music orders and the session's music (GameSession::music()).
class StoryAdapter::MusicRelay final : public game::story::MusicPort {
public:
  void bind(game::story::MusicPort* target) { target_ = target; }

  void playBgm(std::int64_t font) override {
    if (target_ != nullptr) {
      target_->playBgm(font);
    }
  }

  void resetBgm() override {
    if (target_ != nullptr) {
      target_->resetBgm();
    }
  }

private:
  game::story::MusicPort* target_ = nullptr;
};

class StoryAdapter::Serializer final : public game::save::SectionSerializer {
public:
  explicit Serializer(StoryAdapter& adapter) : adapter_(&adapter) {}

  [[nodiscard]] core::Result<core::YamlNode> write() const override {
    // writeSavegame saves the frozen variable (0) as 0 while a script holds the player still (3),
    // and puts the value back for play (main_menu.c:5080; script_interp.c:2735 sets it).
    game::story::StoryState saved = adapter_->state_;
    const game::story::StoryFlowRules& flow = adapter_->flow_;
    const auto frozen = static_cast<std::int32_t>(flow.frozenVariable);
    if (std::cmp_equal(saved.variable(frozen).value_or(0), flow.frozenValue)) {
      core::logDebug("[port:story] writeSavegame (main_menu.c:5080): variable {} is {}, saved as 0",
                     frozen, flow.frozenValue);
      if (auto written = saved.setVariable(frozen, 0); !written) {
        return std::unexpected{written.error()};
      }
    }
    return game::story::toYaml(saved);
  }

  core::Result<void> read(const core::YamlNode& state) override {
    auto read = game::story::fromYaml(state);
    if (!read) {
      return std::unexpected{read.error()};
    }
    if constexpr (!ResetUnsavedVariablesOnLoad) {
      for (std::int32_t variable = game::story::SavedVariableCount;
           variable < game::story::VariableCount; ++variable) {
        if (auto kept =
                read->setVariable(variable, adapter_->state_.variable(variable).value_or(0));
            !kept) {
          return std::unexpected{kept.error()};
        }
      }
    }
    core::logDebug(
        "[port:story] loadSavegame (main_menu.c:5058): variables 0 to {} read, the rest {}",
        game::story::SavedVariableCount - 1, ResetUnsavedVariablesOnLoad ? "start at 0" : "kept");
    adapter_->state_ = *read;
    return {};
  }

  // A save from before the story was kept: the story starts as a new game's, without inventing what
  // the player did since, and the species the evolution section kept as raised (read before this
  // section) raise their triggers, which is what the original keeps them as.
  core::Result<void> readMissing() override {
    core::logWarning("{}", std::string{messages::StoryMissingInSave});
    adapter_->state_ = adapter_->newGameState({});
    if (adapter_->savedRaised_) {
      const std::set<std::int32_t> raised = adapter_->savedRaised_();
      for (const std::int32_t species : raised) {
        adapter_->markRaised(species);
      }
      core::logInfo("{}", formatMessage(messages::StoryRaisedFromSave, raised.size()));
    }
    return {};
  }

private:
  StoryAdapter* adapter_;
};

StoryAdapter::StoryAdapter(StoryAdapterDesc desc)
    : desc_(desc), serializer_(std::make_unique<Serializer>(*this)) {}

StoryAdapter::~StoryAdapter() = default;

ruleset::SectionChecks StoryAdapter::sectionChecks() const {
  return {{std::string{StorySection},
           [](const ruleset::Section& section) -> core::Result<void> {
             if (auto read = readStoryRules(section); !read) {
               return std::unexpected{read.error()};
             }
             return {};
           }},
          {std::string{DialogueSection}, [](const ruleset::Section& section) -> core::Result<void> {
             if (auto read = readDialogueRules(section); !read) {
               return std::unexpected{read.error()};
             }
             return {};
           }}};
}

core::Result<void> StoryAdapter::readRules(const ruleset::Ruleset& rules) {
  if (desc_.files == nullptr) {
    return core::fail(std::string{messages::NoFiles});
  }
  auto section = rules.requireSection(StorySection);
  if (!section) {
    return std::unexpected{section.error()};
  }
  const auto storyRules = readStoryRules(**section);
  if (!storyRules) {
    return std::unexpected{storyRules.error()};
  }
  auto dialogueSection = rules.requireSection(DialogueSection);
  if (!dialogueSection) {
    return std::unexpected{dialogueSection.error()};
  }
  const auto dialogueRules = readDialogueRules(**dialogueSection);
  if (!dialogueRules) {
    return std::unexpected{dialogueRules.error()};
  }
  if (vm_ == nullptr) {
    if (auto wired = wire(*storyRules, *dialogueRules); !wired) {
      return wired;
    }
  } else {
    followRules(*storyRules, *dialogueRules);
  }
  auto story = assets::loadNewGameStory(*desc_.files);
  if (!story) {
    return core::fail(formatMessage(messages::StoryDocumentFailed, story.error().message));
  }
  newGame_ = std::move(*story);
  if (auto warps = game::field::loadFieldWarps(*desc_.files)) {
    fieldStartMap_ = warps->startMap;
    fieldStartExit_ = warps->startExit;
  }
  digimon_.reset();
  if (const assets::AssetId* table = rules.table(game::evolution::DigimonTableRole)) {
    if (auto digimon = assets::loadDigimonTable(*desc_.files, *table)) {
      digimon_ = std::move(*digimon);
    }
  }
  return {};
}

bool StoryAdapter::raised(std::int32_t species) const {
  return species >= 0 && species < RaisedSpeciesLimit &&
         state_.trigger(FirstRaisedTrigger + species).value_or(false);
}

void StoryAdapter::markRaised(std::int32_t species) {
  if (species < 0 || species >= RaisedSpeciesLimit) {
    core::logWarning("{}",
                     formatMessage(messages::StoryRaisedUnnumbered, species, RaisedSpeciesLimit));
    return;
  }
  if (auto written = state_.setTrigger(FirstRaisedTrigger + species, true); !written) {
    core::logWarning("{}", written.error().message);
  }
}

std::set<std::int32_t> StoryAdapter::raisedSpecies() const {
  std::set<std::int32_t> species;
  for (std::int32_t number = 0; number < RaisedSpeciesLimit; ++number) {
    if (raised(number)) {
      species.insert(number);
    }
  }
  return species;
}

core::Result<void> StoryAdapter::addSaveSections(game::save::SaveSections& sections) {
  return sections.add(std::string{StorySaveName}, *serializer_, game::save::MigrationChain{});
}

core::Result<void> StoryAdapter::wire(const StorySectionRules& story,
                                      const game::story::DialogueRules& dialogue) {
  core::logDebug("[port:story] initializeScripts (script_interp.c:2190): the game's script VM, "
                 "ticks_per_frame {}, variable_max {}",
                 story.ticksPerFrame, story.rules.variableMax);
  auto vm = script::ScriptVm::create();
  if (!vm) {
    return std::unexpected{vm.error()};
  }
  vm_ = std::move(*vm);
  statePort_ = std::make_unique<StatePort>(state_);
  randomPort_ = std::make_unique<RandomPort>();
  sound_ = std::make_unique<SoundRelay>();
  music_ = std::make_unique<MusicRelay>();
  orders_ = std::make_unique<game::story::StoryOrders>(*statePort_, *randomPort_, story.rules);
  host_ = std::make_unique<StoryLuaHost>(*vm_, *desc_.files);
  if (auto published = host_->publishApi(*statePort_, *orders_, *sound_); !published) {
    return core::fail(formatMessage(messages::StoryApiFailed, published.error().message));
  }
  dialogue_ = std::make_unique<StoryDialogue>(*statePort_, dialogue);
  if (auto published = host_->publishDialogue(dialogue_->dialogue()); !published) {
    return published;
  }
  if (auto npcs = wireNpcs(); !npcs) {
    return npcs;
  }
  dialogue_->setSpeakerHold([this](std::int64_t speaker) {
    if (npcOrders_ != nullptr) {
      npcOrders_->holdOwner(speaker);
    }
  });
  if (auto flow = wireFlow(); !flow) {
    return flow;
  }
  if (auto music = publishStoryMusic(*vm_, *music_); !music) {
    return music;
  }
  if (auto checked = host_->checkModules(); !checked) {
    return checked;
  }
  if (auto started = startRunner(story); !started) {
    return started;
  }
  return publishMotion();
}

core::Result<void> StoryAdapter::startRunner(const StorySectionRules& story) {
  game::story::StoryFlowPorts ports = flowPorts();
  ports.state = statePort_.get();
  auto runner =
      game::story::StoryRunner::create(*host_, story.ticksPerFrame, std::move(ports), story.flow);
  if (!runner) {
    return std::unexpected{runner.error()};
  }
  runner_.emplace(std::move(*runner));
  dialogue_->attach(*runner_);
  ticksPerFrame_ = story.ticksPerFrame;
  flow_ = story.flow;
  return {};
}

void StoryAdapter::followRules(const StorySectionRules& story,
                               const game::story::DialogueRules& dialogue) {
  *orders_ = game::story::StoryOrders{*statePort_, *randomPort_, story.rules};
  dialogue_->dialogue().setRules(dialogue);
  if (story.ticksPerFrame == ticksPerFrame_ && sameFlow(story.flow, flow_)) {
    return;
  }
  if (runner_.has_value() && runner_->busy()) {
    core::logWarning("{}", messages::StoryRunnerKept);
    return;
  }
  if (auto started = startRunner(story); !started) {
    core::logWarning("{}", started.error().message);
  }
}

game::story::Dialogue& StoryAdapter::dialogue() {
  DWE_ASSERT(dialogue_ != nullptr);
  return dialogue_->dialogue();
}

void StoryAdapter::tick(GameSession& session, const ruleset::ClockAdvance& /*advance*/) {
  if (!runner_) {
    return;
  }
  randomPort_->bind(session.random());
  dialogue_->dialogue().bindSound(session.sound());
  sound_->bind(session.sound());
  music_->bind(session.music());
  playUnplayedChange();
  if (runner_) {
    runner_->tick();
  }
  followNpcHolds();
}

game::story::RequestOutcome StoryAdapter::request(game::story::SectionRef section,
                                                  game::story::StartTiming timing) {
  DWE_ASSERT(runner_.has_value());
  return runner_->request(section, timing);
}

core::Result<void> StoryAdapter::startNewGame(GameSession& session) {
  randomPort_->bind(session.random());
  state_ = newGameState(session.partner().state().species);
  core::logInfo("{}", formatMessage(messages::StoryNewGame, state_.triggersSet()));
  return {};
}

game::story::StoryStatePort* StoryAdapter::statePort() {
  return statePort_.get();
}

game::story::StoryState StoryAdapter::newGameState(std::string_view firstPartner) const {
  game::story::StoryState state;
  std::optional<std::int32_t> raised;
  if (!firstPartner.empty()) {
    const assets::DigimonRow* row = digimon_ ? digimon_->find(firstPartner) : nullptr;
    if (row != nullptr && row->index >= 0 && row->index < RaisedSpeciesLimit) {
      raised = FirstRaisedTrigger + row->index;
    } else {
      core::logWarning("{}", formatMessage(messages::StoryPartnerUnnumbered, firstPartner));
    }
  }
  if (newGame_ && seedNewGame_) {
    const game::field::TriggerState start =
        game::field::newGameTriggers(*newGame_, fieldStartMap_, fieldStartExit_);
    for (std::int32_t trigger = 0; trigger < game::story::TriggerCount; ++trigger) {
      const std::optional<bool> set = start(trigger);
      if (set) {
        if (auto written = state.setTrigger(trigger, *set); !written) {
          core::logWarning("{}", written.error().message);
        }
      } else if (!isRaisedTrigger(trigger)) {
        // The raised triggers are game code's (main.c: main), set below; any other is the
        // document's doubt.
        core::logInfo("{}", formatMessage(messages::StoryUnknownAtStart, trigger));
      }
    }
  }
  if (raised) {
    if (auto written = state.setTrigger(*raised, true); !written) {
      core::logWarning("{}", written.error().message);
    }
  }
  return state;
}

} // namespace dwe::session
