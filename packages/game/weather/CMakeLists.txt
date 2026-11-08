dwe_add_module(weather
    LAYER game
    SOURCES
        src/climate.cpp
        src/look.cpp
    DEPS core assets)

# The tests read the game pack and its schemas from the source tree.
if(TARGET dwe_weather_tests)
    target_compile_definitions(dwe_weather_tests PRIVATE
        DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game"
        DWE_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/content/schemas")
endif()
