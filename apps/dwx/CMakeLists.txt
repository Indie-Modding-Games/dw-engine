find_package(Opus CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
# libopusenc ships no CMake package: its library and header, from the vcpkg tree.
find_library(DWE_OPUSENC_LIBRARY NAMES opusenc REQUIRED)
find_path(DWE_OPUSENC_INCLUDE_DIR opusenc.h PATH_SUFFIXES opus REQUIRED)
add_library(dwe_opusenc INTERFACE)
target_link_libraries(dwe_opusenc INTERFACE ${DWE_OPUSENC_LIBRARY} Opus::opus)
target_include_directories(dwe_opusenc SYSTEM INTERFACE ${DWE_OPUSENC_INCLUDE_DIR})

# The tool's code is a module so its tests can link it; the command line is a thin app on top.
dwe_add_module(dwx
    LAYER apps
    SOURCES
        src/asset_names.cpp
        src/audio_cooker.cpp
        src/audio_extractors.cpp
        src/base_pack.cpp
        src/cook.cpp
        src/disc_description.cpp
        src/effect_extractors.cpp
        src/extract.cpp
        src/extract_disc.cpp
        src/extractors.cpp
        src/game_tables.cpp
        src/image_extractors.cpp
        src/map_extractors.cpp
        src/overlay_extractor.cpp
        src/padding_extractor.cpp
        src/script_extractors.cpp
        src/script_maps.cpp
        src/table_extractors.cpp
        src/model_extractors.cpp
        src/model_textures.cpp
        src/pack.cpp
        src/text_extractors.cpp
        src/texture_cooker.cpp
        src/validate.cpp
        src/verify.cpp
        src/video_extractors.cpp
    DEPS core formats assets
    PRIVATE_LIBS dwe_opusenc KTX::ktx
    TEST_LIBS dwe_cgltf)

if(BUILD_TESTING)
    # The tests build synthetic discs and TIMs with the fixtures of the formats tests, use the media files of the
    # assets tests and check the documents and the schemas of content/schemas, and the game pack on the extracted base
    # pack.
    target_include_directories(dwe_dwx_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/formats/tests
        ${PROJECT_SOURCE_DIR}/engine/assets/tests)
    target_compile_definitions(dwe_dwx_tests PRIVATE DWE_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/content/schemas"
                                                     DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game"
                                                     DWE_EXAMPLE_MOD="${PROJECT_SOURCE_DIR}/tests/mods/example_mod")
endif()

dwe_add_app(dwx_cli
    LAYER apps
    SOURCES
        src/main.cpp
    DEPS dwx
    OUTPUT_NAME dwx)
