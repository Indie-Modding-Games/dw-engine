#pragma once

#include <dwe/game/tournament/match_show.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "field_scene.hpp"

namespace dwe::app {

// The 3D letters over an arena's match (story_tournament.hpp) and over a versus battle
// (versus_arena.hpp): a model whose objects are the letters, posed in the eye's own space.
struct LetterModel {
  std::string_view model;
  std::size_t objects = 0;
};

// std_hud.c: STD_loadWinLoseModel: STDDAT\WIN_LOSE.TMD, four objects for the won match and four for
// the lost one; STD_loadVersusSceneModel: STDDAT\DRAW.TMD.
inline constexpr LetterModel WinLoseLetters{.model = "model/stddat/win_lose", .objects = 8};
inline constexpr LetterModel DrawLetters{.model = "model/stddat/draw", .objects = 4};
// std_hud.c: STD_loadChampionModels: the champion's title (STDDAT\CHAMP.TMD, nine objects).
inline constexpr LetterModel ChampionLetters{.model = "model/stddat/champ", .objects = 9};
// vs_hud.c: VS_loadStageModels: STDDAT\1P2PWIN.TMD.
inline constexpr LetterModel VersusWinModel{.model = "model/stddat/1p2pwin",
                                            .objects = game::tournament::VersusWinLetters::Objects};

// Poses `letters` with `shown` 1000 in front of the eye of `view` (STD_MODEL_SCENE_MATRIX), seen
// with the letters' projection, and poses nothing of the other `models`; without letters, none
// shows.
void showLetterModel(FieldScene& scene, std::span<const LetterModel> models, LetterModel shown,
                     const std::optional<game::tournament::LetterScene>& letters,
                     const game::tournament::ShowView& view);

} // namespace dwe::app
