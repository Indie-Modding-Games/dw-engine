#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/evolution/tables.hpp>
#include <dwe/game/field/rules.hpp>
#include <dwe/game/field/script_warps.hpp>
#include <dwe/game/story/story_state_yaml.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/story_adapter.hpp>

#include <set>
#include <utility>

namespace dwe::session {

using messages::formatMessage;

namespace {

bool isRaisedTrigger(std::int32_t trigger) {
    return trigger >= FirstRaisedTrigger && trigger < FirstRaisedTrigger + RaisedSpeciesLimit;
}

} // namespace

class StoryAdapter::Serializer final : public game::save::SectionSerializer {
public:
    explicit Serializer(StoryAdapter& adapter) : adapter_(&adapter) {}

    [[nodiscard]] core::Result<core::YamlNode> write() const override { return game::story::toYaml(adapter_->state_); }

    core::Result<void> read(const core::YamlNode& state) override {
        auto read = game::story::fromYaml(state);
        if (!read) {
            return std::unexpected{read.error()};
        }
        adapter_->state_ = *read;
        return {};
    }

    // A save from before the story was kept: the story starts as a new game's, without inventing what the player
    // did since, and the species the evolution section kept as raised (read before this section) raise their
    // triggers, which is what the original keeps them as.
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

StoryAdapter::StoryAdapter(StoryAdapterDesc desc) : desc_(desc), serializer_(std::make_unique<Serializer>(*this)) {}

StoryAdapter::~StoryAdapter() = default;

core::Result<void> StoryAdapter::readRules(const ruleset::Ruleset& rules) {
    if (desc_.files == nullptr) {
        return core::fail(std::string{messages::NoFiles});
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
    return species >= 0 && species < RaisedSpeciesLimit && state_.trigger(FirstRaisedTrigger + species).value_or(false);
}

void StoryAdapter::markRaised(std::int32_t species) {
    if (species < 0 || species >= RaisedSpeciesLimit) {
        core::logWarning("{}", formatMessage(messages::StoryRaisedUnnumbered, species, RaisedSpeciesLimit));
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

core::Result<void> StoryAdapter::startNewGame(GameSession& session) {
    state_ = newGameState(session.partner().state().species);
    core::logInfo("{}", formatMessage(messages::StoryNewGame, state_.triggersSet()));
    return {};
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
    if (newGame_) {
        const game::field::TriggerState start =
            game::field::newGameTriggers(*newGame_, fieldStartMap_, fieldStartExit_);
        for (std::int32_t trigger = 0; trigger < game::story::TriggerCount; ++trigger) {
            const std::optional<bool> set = start(trigger);
            if (set) {
                if (auto written = state.setTrigger(trigger, *set); !written) {
                    core::logWarning("{}", written.error().message);
                }
            } else if (!isRaisedTrigger(trigger)) {
                // The raised triggers are game code's (main.c: main), set below; any other is the document's doubt.
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
