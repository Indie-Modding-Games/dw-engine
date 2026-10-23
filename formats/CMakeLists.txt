find_package(lodepng CONFIG REQUIRED)
# The Lua emitter's tests run the modules it writes.
find_package(Lua REQUIRED)

dwe_add_module(formats
    LAYER base
    SOURCES
        src/audio/adpcm.cpp
        src/audio/envelope.cpp
        src/audio/midi.cpp
        src/audio/seq.cpp
        src/audio/sf2.cpp
        src/audio/vab.cpp
        src/audio/vab_sf2.cpp
        src/audio/vhb.cpp
        src/audio/wav.cpp
        src/disc/boot.cpp
        src/disc/cue_sheet.cpp
        src/disc/disc_image.cpp
        src/disc/file_table.cpp
        src/disc/iso9660.cpp
        src/disc/known_releases.cpp
        src/disc/sector.cpp
        src/effect/efe_analysis.cpp
        src/effect/efe_file.cpp
        src/effect/efe_script.cpp
        src/effect/efe_subops.cpp
        src/effect/efe_yaml.cpp
        src/exe/psx_exe.cpp
        src/exe/constant_switch.cpp
        src/exe/executable_layout.cpp
        src/exe/item_effects.cpp
        src/exe/music_code.cpp
        src/exe/memory_image.cpp
        src/exe/overlay.cpp
        src/exe/overlay_tables.cpp
        src/exe/overlay_yaml.cpp
        src/exe/tables.cpp
        src/exe/tables_yaml.cpp
        src/image/png.cpp
        src/image/tim.cpp
        src/image/tim_packs.cpp
        src/map/map_file.cpp
        src/map/map_yaml.cpp
        src/map/tfs_file.cpp
        src/model/animation.cpp
        src/model/gltf.cpp
        src/model/mmd.cpp
        src/model/skeleton.cpp
        src/model/tmd.cpp
        src/script/control_flow.cpp
        src/script/disassembly.cpp
        src/script/dominance.cpp
        src/script/guarded_walk.cpp
        src/script/routine_writes.cpp
        src/script/lua_emitter.cpp
        src/script/story_api_names.cpp
        src/script/map_changes.cpp
        src/script/new_game_stats.cpp
        src/script/routine_graph.cpp
        src/script/scn_file.cpp
        src/script/script_listing.cpp
        src/script/script_warps.cpp
        src/script/script_yaml.cpp
        src/script/structure_statistics.cpp
        src/script/structuring.cpp
        src/text/charset.cpp
        src/text/font.cpp
        src/text/game_text.cpp
        src/text/string_manifest.cpp
        src/video/mdec.cpp
        src/video/media_files.cpp
        src/video/movie.cpp
        src/video/str.cpp
        src/video/xa_adpcm.cpp
    DEPS core
    PRIVATE_LIBS lodepng dwe_cgltf
    TEST_LIBS lodepng dwe_cgltf ${LUA_LIBRARIES})

if(TARGET dwe_formats_tests)
    target_include_directories(dwe_formats_tests SYSTEM PRIVATE ${LUA_INCLUDE_DIR})
endif()

# lodepng compresses every image dwx writes, and its debug build is three times slower; nobody steps into it, so
# debug builds link its release build. Not on Windows, where debug and release code use different C runtimes.
if(NOT WIN32)
    set_target_properties(lodepng PROPERTIES MAP_IMPORTED_CONFIG_DEBUG Release)
endif()

# dwx decodes every frame of the movies, about 13 000 of them, and the unoptimised MDEC decoder takes minutes for that,
# so this file is optimised in debug builds too; it keeps its debug info and sanitizers.
set_source_files_properties(src/video/mdec.cpp PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")

# The tests check the YAML documents against the JSON Schemas of content/schemas.
if(TARGET dwe_formats_tests)
    target_compile_definitions(dwe_formats_tests PRIVATE DWE_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/content/schemas")
endif()
