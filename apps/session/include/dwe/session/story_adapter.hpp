#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/new_game_story.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/save/sections.hpp>
#include <dwe/game/story/story_state.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/session/session_system.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::session {

// The save section of the story's triggers and variables.
inline constexpr std::string_view StorySaveName = "story";

// The first partner's raised trigger is 0x200 + its number, for numbers below 0x3f (main.c: main raises it before the
// new game's section; script_common.c: setDigimonRaised).
inline constexpr std::int32_t FirstRaisedTrigger = 0x200;
inline constexpr std::int32_t RaisedSpeciesLimit = 0x3f;

// The species another save section kept as raised (the evolution section), by number.
using RaisedSource = std::function<std::set<std::int32_t>()>;

struct StoryAdapterDesc {
    // The mounted packs, for data/new_game_story, field/warps and the digimon table; they must outlive the adapter.
    const assets::VirtualFileSystem* files = nullptr;
};

// Glues the story state (game/story) to the session: starts it as a new game leaves it, and saves it in the `story`
// section. The field reads its triggers and writes the ones a script warp sets (field_facts.hpp).
class StoryAdapter final : public SessionSystem {
public:
    explicit StoryAdapter(StoryAdapterDesc desc);
    ~StoryAdapter() override;
    StoryAdapter(const StoryAdapter&) = delete;
    StoryAdapter& operator=(const StoryAdapter&) = delete;
    StoryAdapter(StoryAdapter&&) = delete;
    StoryAdapter& operator=(StoryAdapter&&) = delete;

    // Reads data/new_game_story, field/warps' start and the ruleset's digimon table; any of them may be missing.
    core::Result<void> readRules(const ruleset::Ruleset& rules) override;
    core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
    // The story as a new game leaves it, with the session's partner as the first one.
    core::Result<void> startNewGame(GameSession& session) override;

    // The story as a new game with this first partner leaves it: the triggers data/new_game_story sets, the ones its
    // opening sets when field/warps starts after it, and the partner's raised trigger. A trigger the document does not
    // know at the start (its paths disagree, or game code may set it) starts clear, and the log says so.
    [[nodiscard]] game::story::StoryState newGameState(std::string_view firstPartner) const;

    // The raised species, by number: the story owns them as triggers 0x200 + number, as the original does
    // (script_common.c: setDigimonRaised). A number with no trigger is never raised, and marking it is logged.
    [[nodiscard]] bool raised(std::int32_t species) const;
    void markRaised(std::int32_t species);
    [[nodiscard]] std::set<std::int32_t> raisedSpecies() const;
    // Where a save from before the story section takes its raised species from (createGameSystems: the evolution
    // section's); without it such a save starts with none raised.
    void setSavedRaised(RaisedSource source) { savedRaised_ = std::move(source); }

    [[nodiscard]] game::story::StoryState& state() { return state_; }
    [[nodiscard]] const game::story::StoryState& state() const { return state_; }

private:
    class Serializer;

    StoryAdapterDesc desc_;
    std::optional<assets::NewGameStory> newGame_;
    std::string fieldStartMap_;
    std::int32_t fieldStartExit_ = 0;
    std::optional<assets::DigimonTable> digimon_;
    RaisedSource savedRaised_;
    game::story::StoryState state_;
    std::unique_ptr<Serializer> serializer_;
};

} // namespace dwe::session
