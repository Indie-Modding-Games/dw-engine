# The view models of the game's screens (battle, the partner's status, the item bag): plain values built from the
# session and the battle, published to the UI as the settings its documents read. No rule is decided here.
dwe_add_module(screens
    LAYER apps
    SOURCES
        src/bag_view.cpp
        src/battle_view.cpp
        src/name_keys.cpp
        src/partner_status_view.cpp
        src/screen_settings.cpp
    DEPS core assets localization ui battle inventory partner session)

if(TARGET dwe_screens_tests)
    # The made-up moves of the battle tests (game/battle/tests/fixture.hpp) and the game pack they read the rules from.
    target_include_directories(dwe_screens_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_compile_definitions(dwe_screens_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
