#pragma once

#include <dwe/session/session_system.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace dwe::session {

class StoryAdapter;

// A pack's entry script: scripts/main.lua, run once in the pack's own environment.
inline constexpr std::string_view ModEntryScript = "scripts/main";

// The mods' scripts on the game's script VM (the story's): once the VM exists, it binds the
// session's game events (GameHooks) there and runs every pack's entry script in that pack's
// sandboxed environment, bottom of the stack first, so the handlers it registers with hooks.on hear
// the game from the first event on (docs/modding/api-lua.md). A script that fails is logged with
// its pack and the others still run.
class ModScripts final : public SessionSystem {
public:
  // The story adapter owns the VM; it must outlive this system.
  explicit ModScripts(StoryAdapter& story) : story_(&story) {}

  core::Result<void> startNewGame(GameSession& session) override;
  void afterLoad(GameSession& session) override;
  void tick(GameSession& session, const ruleset::ClockAdvance& advance) override;

  // The packs whose entry script ran, in the order they ran.
  [[nodiscard]] const std::vector<std::string>& ran() const { return ran_; }

private:
  void bind(GameSession& session);
  void runEntryScripts();

  StoryAdapter* story_;
  bool bound_ = false;
  std::vector<std::string> ran_;
};

} // namespace dwe::session
