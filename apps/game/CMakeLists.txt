dwe_add_app(game
    LAYER apps
    SOURCES
        src/autotest.cpp
        src/battle_screen.cpp
        src/battle_trigger.cpp
        src/care_commands.cpp
        src/common.cpp
        src/content_mount.cpp
        src/death_scene.cpp
        src/dev_assets_banner.cpp
        src/dev_views.cpp
        src/device_names.cpp
        src/evolution_scene.cpp
        src/field_hud.cpp
        src/field_scene.cpp
        src/field_scene_npcs.cpp
        src/field_scene_script.cpp
        src/field_scene_objects.cpp
        src/frame_stats.cpp
        src/game.cpp
        src/game_audio.cpp
        src/language.cpp
        src/main.cpp
        src/map_preview.cpp
        src/minimap_setting.cpp
        src/free_exploration.cpp
        src/game_saves.cpp
        src/model_viewer.cpp
        src/movie_rules.cpp
        src/movie_screen.cpp
        src/options.cpp
        src/partner_events.cpp
        src/partner_menu.cpp
        src/partner_tab.cpp
        src/play_session.cpp
        src/story_scene_glue.cpp
        src/save_command.cpp
        src/save_flow.cpp
        src/saves.cpp
        src/session_start.cpp
        src/save_machine.cpp
        src/gym_screen.cpp
        src/shop_screen.cpp
        src/sleep_screen.cpp
        src/story_field_link.cpp
        src/story_textbox.cpp
        src/story_stage.cpp
        src/story_map_edits.cpp
        src/test_scene.cpp
        src/text_font_setting.cpp
        src/title_screen.cpp
        src/ui_demo.cpp
        src/ui_skin_setting.cpp
        src/user_settings.cpp
    DEPS core platform audio video rhi assets localization animation render input script ui debug scene ruleset field partner save content session battle inventory screens story evolution gym
    OUTPUT_NAME dwe-game)

dwe_add_shaders(game FILES shaders/test_scene.slang)

# The version a save file names as the game that wrote it.
target_compile_definitions(game PRIVATE DWE_GAME_VERSION="${PROJECT_VERSION}")

# Until the asset pipeline exists, the scripts are read from the source tree, so editing them reloads them live.
target_compile_definitions(game PRIVATE DWE_GAME_SCRIPTS="${CMAKE_CURRENT_SOURCE_DIR}/scripts")

# The UI demo reads the team's packs (menus, HUD, fonts) from the source tree, so editing them reloads them live.
target_compile_definitions(game PRIVATE DWE_TEAM_PACKS="${PROJECT_SOURCE_DIR}/content/packs")
# The game pack (models/*.yaml), which the model viewer mounts over the extracted content.
target_compile_definitions(game PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")

# The game is an executable, so its tests build the sources they check into a test program of their own: the player's
# settings (settings.yaml), how the UI skin is chosen at start and changed, the minimap mode, the save debug command and
# the title screen's and the game's save flows.
if(BUILD_TESTING)
    file(GLOB game_test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
    add_executable(dwe_game_tests ${game_test_sources} src/game_saves.cpp src/minimap_setting.cpp
                                  src/save_command.cpp src/save_flow.cpp src/ui_skin_setting.cpp src/user_settings.cpp)
    target_link_libraries(dwe_game_tests PRIVATE dwe::core dwe::assets dwe::platform dwe::localization dwe::ui dwe::content
                                                 dwe::script dwe::save dwe::partner dwe::session dwe::evolution
                                                 dwe::inventory dwe::screens dwe_test_main)
    # The session's test packs and made-up species (session_fixture.hpp), for the save debug command.
    target_include_directories(dwe_game_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                                                      ${PROJECT_SOURCE_DIR}/apps/session/tests
                                                      # The systems' made-up tables, as "game/<system>/tests/fixture.hpp".
                                                      ${PROJECT_SOURCE_DIR})
    target_compile_definitions(dwe_game_tests PRIVATE
        DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game"
        DWE_SKIN_TEST_PACK="${PROJECT_SOURCE_DIR}/tests/mods/ui_skin/test_contrast")
    dwe_apply_options(dwe_game_tests)
    _dwe_add_tests(game dwe_game_tests ${game_test_sources})
endif()
