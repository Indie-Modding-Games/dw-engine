find_package(lodepng CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(Opus CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)

dwe_add_module(assets
    LAYER systems
    SOURCES
        src/asset_id.cpp
        src/content_id.cpp
        src/table_composition.cpp
        src/table_kinds.cpp
        src/table_handle.cpp
        src/entry_class.cpp
        src/pack_manifest.cpp
        src/lua_api.cpp
        src/file_system.cpp
        src/virtual_file_system.cpp
        src/pak.cpp
        src/image.cpp
        src/model.cpp
        src/model_data.cpp
        src/media_headers.cpp
        src/content_check.cpp
        src/text_table.cpp
        src/ogg.cpp
        src/sound.cpp
        src/texture.cpp
        src/yaml_reader.cpp
        src/map_document.cpp
        src/map_table.cpp
        src/digimon_table.cpp
        src/raise_table.cpp
        src/item_table.cpp
        src/item_effect_table.cpp
        src/new_game_stats.cpp
        src/shop_table.cpp
        src/indexed_image.cpp
        src/map_images.cpp
        src/png_only_images.cpp
        src/image_palettes.cpp
        src/character_model.cpp
        src/game_table_document.cpp
        src/move_table.cpp
        src/evolution_table.cpp
        src/dev_asset_sources.cpp
        src/dev_pack_selection.cpp
    DEPS core
    PRIVATE_LIBS lodepng dwe_cgltf zstd::libzstd Opus::opus KTX::ktx
    TEST_LIBS lodepng Opus::opus KTX::ktx)

# The content check decodes every image of a pack; as in formats, debug builds link lodepng's release build, three
# times faster. Not on Windows, where debug and release code use different C runtimes.
if(NOT WIN32)
    set_target_properties(lodepng PROPERTIES MAP_IMPORTED_CONFIG_DEBUG Release)
endif()
