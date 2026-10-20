#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::assets {

class VirtualFileSystem;
struct ModelData;

// Which model a character draws and which of its clips each animation the engine asks for is: the game pack's
// models/<name>.yaml (content/schemas/character_model.schema.json). The field asks for "idle", "walk" or "run"; the
// clip ids are the original's animation ids, as the extracted models name their clips.
struct CharacterModel {
    AssetId model;
    std::map<std::string, std::uint32_t, std::less<>> animations;

    // The clip id of an animation name, if the character has it.
    [[nodiscard]] std::optional<std::uint32_t> clipOf(std::string_view animation) const;
};

// Parses a models/<name>.yaml document. Errors name the line and column.
core::Result<CharacterModel> parseCharacterModel(std::string_view yaml);

// Reads and parses the document with this asset id (for example `models/tamer`).
core::Result<CharacterModel> loadCharacterModel(const VirtualFileSystem& vfs, const AssetId& id);

// Checks that the model has a clip for every animation of the character.
core::Result<void> checkCharacterModel(const CharacterModel& character, const ModelData& model);

} // namespace dwe::assets
