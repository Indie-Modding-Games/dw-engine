# The tool's code is a module so its tests can link it; the command line is a thin app on top.
dwe_add_module(dwx
    LAYER apps
    SOURCES
        src/asset_names.cpp
        src/audio_extractors.cpp
        src/base_pack.cpp
        src/cook.cpp
        src/disc_description.cpp
        src/effect_extractors.cpp
        src/extract.cpp
        src/extract_disc.cpp
        src/extractors.cpp
        src/game_tables.cpp
        src/ktx2.cpp
        src/image_extractors.cpp
        src/map_extractors.cpp
        src/overlay_extractor.cpp
        src/padding_extractor.cpp
        src/script_extractors.cpp
        src/table_extractors.cpp
        src/model_extractors.cpp
        src/model_textures.cpp
        src/pack.cpp
        src/text_extractors.cpp
        src/texture_cooker.cpp
        src/verify.cpp
    DEPS core formats assets
    TEST_LIBS dwe_cgltf)

if(BUILD_TESTING)
    # The tests build synthetic discs and TIMs with the fixtures of the formats tests, use the temporary folder of the
    # assets tests and check the documents against content/schemas.
    target_include_directories(dwe_dwx_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/formats/tests
        ${PROJECT_SOURCE_DIR}/engine/assets/tests)
    target_compile_definitions(dwe_dwx_tests PRIVATE DWE_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/content/schemas")
endif()

dwe_add_app(dwx_cli
    LAYER apps
    SOURCES
        src/main.cpp
    DEPS dwx
    OUTPUT_NAME dwx)
