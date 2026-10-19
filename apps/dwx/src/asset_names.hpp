#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

// The asset ids of the base pack. They live here, and only here, so the scheme can change in one place: when the
// executable's tables are extracted, Digimon and cards get names instead of numbers.
namespace dwe::dwx::names {

// Turns a name from the disc into an id segment: lowercase, and '_' for anything that is not a letter or a digit.
std::string segment(std::string_view discName);

// digimon/<type>/texture: the texture of a Digimon type, from CHDAT/ALLTIM.TIM. The type is a 3-digit number.
core::Result<assets::AssetId> digimonTexture(std::size_t type);

// card/<id>: the picture of a card, from CARD/CARD.ALL. The id is a 3-digit number.
core::Result<assets::AssetId> cardImage(std::size_t card);

// texture/<folders>/<name> for the only image of a disc file, texture/<folders>/<name>/<index> for one of several:
// STDDAT/GRADE/AN.TIM is texture/stddat/grade/an, the third image of STDDAT/T_TOGI.TIM is texture/stddat/t_togi/002.
core::Result<assets::AssetId> discImage(std::string_view volumePath, std::optional<std::size_t> index);

// The id of an entry of a known pack: Digimon textures and cards have their own ids, other packs use discImage.
core::Result<assets::AssetId> packEntry(std::string_view volumePath, std::size_t index);

// <image>_meta: the metadata that goes next to an image.
core::Result<assets::AssetId> metadataOf(const assets::AssetId& image);

} // namespace dwe::dwx::names
