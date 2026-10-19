#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "game_tables.hpp"

// The asset ids of the base pack. They live here, and only here, so the scheme can change in one place. Names come
// from the executable's tables when the version is known (GameTables::ids); without them, numbers stand in.
namespace dwe::dwx::names {

// Turns a name from the disc into an id segment: lowercase, and '_' for anything that is not a letter or a digit.
std::string segment(std::string_view discName);

// The id segment of a Digimon type: its id from the executable's tables (made from its model file name), or the
// type as a 3-digit number when the tables are not known.
std::string digimonSlug(const GameTables* tables, std::size_t type);

// digimon/<slug>/<leaf>: an asset of a Digimon type, such as its texture ("texture", from CHDAT/ALLTIM.TIM) or its
// model ("model"). Every extractor names Digimon assets with this function.
core::Result<assets::AssetId> digimonAsset(const GameTables* tables, std::size_t type, std::string_view leaf);

// model/<folders>/<name>: a model file without a skeleton. DOOR/DOOR05.TMD is model/door/door05.
core::Result<assets::AssetId> discModel(std::string_view volumePath);

// card/<id>: the picture of a card, from CARD/CARD.ALL. The id is a 3-digit number.
core::Result<assets::AssetId> cardImage(std::size_t card);

// texture/<folders>/<name> for the only image of a disc file, texture/<folders>/<name>/<index> for one of several:
// STDDAT/GRADE/AN.TIM is texture/stddat/grade/an, the third image of STDDAT/T_TOGI.TIM is texture/stddat/t_togi/002.
core::Result<assets::AssetId> discImage(std::string_view volumePath, std::optional<std::size_t> index);

// The id of an entry of a known pack: Digimon textures and cards have their own ids, other packs use discImage.
core::Result<assets::AssetId> packEntry(const GameTables* tables, std::string_view volumePath, std::size_t index);

// data/<name>: a document of the executable's tables ("digimon", "moves"...).
core::Result<assets::AssetId> tableDocument(std::string_view name);

// disc/boot: the boot settings of SYSTEM.CNF.
core::Result<assets::AssetId> bootConfig();

// map/<slug>/<leaf>: an asset of a field map, such as its data ("data") or a background ("background_day"). The
// slug is the map's id from the executable's tables (made from its MAP_ENTRIES file name).
core::Result<assets::AssetId> mapAsset(std::string_view slug, std::string_view leaf);

// map/<slug>/texture<bits>/<index>: one of the 8-bit or 4-bit TIMs of a map, numbered from 0 in each kind.
core::Result<assets::AssetId> mapImage(std::string_view slug, int bitsPerPixel, std::size_t index);

// arena/<name>/collision: the collision grid of a tournament arena, STDDAT/B_TOGI.ATR is arena/b_togi/collision.
core::Result<assets::AssetId> arenaCollision(std::string_view volumePath);

// sound/<bank>/<leaf> for a bank alone in its file, sound/<bank>/<entry>/<leaf> for an entry of a packed one:
// SOUND/VHB/SS.VHB gives sound/ss/bank, the fourth entry of VBALL.VHB sound/vball/003/bank. The entry is a 3-digit
// number.
core::Result<assets::AssetId> soundAsset(std::string_view bank, std::optional<std::size_t> entry,
                                         std::string_view leaf);

// music/<font>/<leaf>: an asset of a music font (its SoundFont "font" or a SEQ "track_00"). The font is the
// original's 1-based number (sound.c: loadMusicFont) as a 3-digit number.
core::Result<assets::AssetId> musicAsset(std::size_t font, std::string_view leaf);

// <image>_meta: the metadata that goes next to an image.
core::Result<assets::AssetId> metadataOf(const assets::AssetId& image);

// script/<file>[/<id>]: the listing of an event script. SCN/MAPHEAD.SCN is script/maphead, script 12 of SCN/DG.SCN is
// script/dg/012.
core::Result<assets::AssetId> scriptListing(std::string_view volumePath, std::optional<std::size_t> id);

// text/<language>/<source>: the strings of the executable ("main") or of an overlay ("btl" for BTL_REL.BIN).
core::Result<assets::AssetId> textTable(std::string_view language, std::string_view source);

// font/main: the atlas of the executable's font; its metadata is metadataOf of it.
core::Result<assets::AssetId> fontImage();

// effect/<slug>/<leaf>: an asset of a battle effect slot of ETCHI/EFEDAT.EFE ("script", "texture", "model"...). A slot
// is the effect of the technique with its number, so the slug is the move's id from the executable's tables, or the
// slot as a 3-digit number when the tables are not known.
core::Result<assets::AssetId> effectAsset(const GameTables* tables, std::size_t slot, std::string_view leaf);

} // namespace dwe::dwx::names
