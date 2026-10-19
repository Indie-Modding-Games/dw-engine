# The tool's code is a module so its tests can link it; the command line is a thin app on top.
dwe_add_module(dwx
    LAYER apps
    SOURCES
        src/asset_names.cpp
        src/base_pack.cpp
        src/extract.cpp
        src/extract_disc.cpp
        src/extractors.cpp
        src/image_extractors.cpp
        src/verify.cpp
    DEPS core formats assets)

if(BUILD_TESTING)
    # The tests build synthetic discs and TIMs with the fixtures of the formats tests and use the temporary folder of
    # the assets tests.
    target_include_directories(dwe_dwx_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/formats/tests
        ${PROJECT_SOURCE_DIR}/engine/assets/tests)
endif()

dwe_add_app(dwx_cli
    LAYER apps
    SOURCES
        src/main.cpp
    DEPS dwx
    OUTPUT_NAME dwx)
