#pragma once

#include <dwe/rhi/types.hpp>

#include <cstdint>

namespace dwe::app {

// map_object.c: activateMedalTexture: the medal model (ETCNA/MEDAL.TMD) draws its face from a
// 24 x 32 slot of its image, and the image holds every medal's face beside it, nine to a row
// (VRAM words 518 + 6 (n % 9), row 432 + 32 (n / 9), four texels a word, the image starting at
// word 512 and row 256). Showing medal n copies its face into the slot (MoveImage).
inline constexpr std::uint32_t MedalFaceWidth = 24;
inline constexpr std::uint32_t MedalFaceHeight = 32;
inline constexpr std::int32_t MedalSlotTop = 176;

// The medals of the medals page (game_menu.c: renderMedalView), 5 x 3.
inline constexpr std::int32_t MedalCount = 15;

// The slot the model's face reads.
inline constexpr rhi::Rect MedalSlot{
    .x = 0, .y = MedalSlotTop, .width = MedalFaceWidth, .height = MedalFaceHeight};

// Medal n's face in the image (n from 0 to 14).
[[nodiscard]] constexpr rhi::Rect medalFace(std::int32_t medal) {
  constexpr std::int32_t PerRow = 9;
  return {.x = static_cast<std::int32_t>(MedalFaceWidth) * (1 + (medal % PerRow)),
          .y = MedalSlotTop + static_cast<std::int32_t>(MedalFaceHeight) * (medal / PerRow),
          .width = MedalFaceWidth,
          .height = MedalFaceHeight};
}

// game_menu.c: renderDigiviceMedals turns the medal 0x64 (of 4096 a turn) about its y axis every
// frame it draws it.
inline constexpr std::int32_t MedalTurnPerFrame = 0x64;

} // namespace dwe::app
