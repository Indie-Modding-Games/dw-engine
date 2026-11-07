dwe_add_app(game
    LAYER apps
    SOURCES
        src/autotest.cpp
        src/battle_ground.cpp
        src/battle_screen.cpp
        src/battle_screen_exit.cpp
        src/battle_screen_retreat.cpp
        src/battle_screen_throw.cpp
        src/battle_trigger.cpp
        src/care_commands.cpp
        src/chart_requirements.cpp
        src/common.cpp
        src/content_mount.cpp
        src/control_json.cpp
        src/control_log.cpp
        src/control_socket.cpp
        src/curling_recruit_bar.cpp
        src/curling_screen.cpp
        src/death_effects.cpp
        src/death_scene.cpp
        src/dev_assets_banner.cpp
        src/dev_views.cpp
        src/ending_effects.cpp
        src/ending_scene.cpp
        src/device_names.cpp
        src/evolution_effects.cpp
        src/field_effects.cpp
        src/field_mist.cpp
        src/flame_texture.cpp
        src/evolution_scene.cpp
        src/field_hud.cpp
        src/chibi_style.cpp
        src/field_scene.cpp
        src/field_scene_npcs.cpp
        src/field_scene_ground_items.cpp
        src/field_scene_graphics.cpp
        src/field_scene_effects.cpp
        src/field_scene_status.cpp
        src/field_scene_battle_visuals.cpp
        src/field_scene_effect_prims.cpp
        src/first_run_screen.cpp
        src/battle_effect_bodies.cpp
        src/battle_glide.cpp
        src/battle_motion.cpp
        src/field_scene_script.cpp
        src/field_scene_fighters.cpp
        src/field_scene_objects.cpp
        src/field_scene_sprite_characters.cpp
        src/sprite_characters.cpp
        src/field_scene_partner_body.cpp
        src/field_scene_fx.cpp
        src/scene_prims.cpp
        src/frame_stats.cpp
        src/game.cpp
        src/game_audio.cpp
        src/language.cpp
        src/main.cpp
        src/map_debug_overlay.cpp
        src/map_preview.cpp
        src/minimap_enemies.cpp
        src/minimap_items.cpp
        src/hd_scenery_setting.cpp
        src/volume_setting.cpp
        src/minimap_setting.cpp
        src/display_settings.cpp
        src/graphics_options.cpp
        src/walk_control.cpp
        src/battle_speed.cpp
        src/gym_batch.cpp
        src/title_options.cpp
        src/free_exploration.cpp
        src/game_saves.cpp
        src/model_viewer.cpp
        src/movie_rules.cpp
        src/movie_screen.cpp
        src/naming_box.cpp
        src/new_game_interview.cpp
        src/new_game_scene_rules.cpp
        src/options.cpp
        src/partner_events.cpp
        src/partner_menu.cpp
        src/player_chart.cpp
        src/player_city.cpp
        src/partner_portrait.cpp
        src/partner_tab.cpp
        src/pause_box.cpp
        src/place_command.cpp
        src/partner_set_command.cpp
        src/story_set_command.cpp
        src/story_state_json.cpp
        src/shop_job_bar.cpp
        src/play_session.cpp
        src/play_session_report.cpp
        src/remote_control.cpp
        src/bug_report.cpp
        src/bug_reporter.cpp
        src/story_scene_glue.cpp
        src/save_command.cpp
        src/save_flow.cpp
        src/saves.cpp
        src/slot_description.cpp
        src/screenshot.cpp
        src/session_start.cpp
        src/sign_dump.cpp
        src/medal_figure.cpp
        src/save_figure.cpp
        src/save_indicator.cpp
        src/save_machine.cpp
        src/save_machine_flow.cpp
        src/gym_screen.cpp
        src/card_shop_screen.cpp
        src/merit_shop_screen.cpp
        src/money_box_screen.cpp
        src/discard_screen.cpp
        src/trade_screen.cpp
        src/bird_transport_screen.cpp
        src/world_map.cpp
        src/world_map_screen.cpp
        src/item_keeper_screen.cpp
        src/jukebox_screen.cpp
        src/shop_screen.cpp
        src/sleep_screen.cpp
        src/sleep_steps.cpp
        src/state_trace.cpp
        src/story_field_link.cpp
        src/story_text_rows.cpp
        src/story_textbox.cpp
        src/story_stage.cpp
        src/story_map_edits.cpp
        src/egg_box_screen.cpp
        src/story_gym.cpp
        src/story_naming.cpp
        src/story_item_keeper.cpp
        src/story_shop.cpp
        src/story_tournament.cpp
        src/tournament_bracket_screen.cpp
        src/tournament_match_screen.cpp
        src/tournament_schedule_screen.cpp
        src/field_items_screen.cpp
        src/field_numbers.cpp
        src/award_screen.cpp
        src/fishing_screen.cpp
        src/fishing_look.cpp
        src/field_toilet.cpp
        src/field_poop.cpp
        src/field_story_props.cpp
        src/boss_arrival_scene.cpp
        src/partner_walk_up.cpp
        src/praise_scold_call.cpp
        src/rule_options.cpp
        src/feed_scene.cpp
        src/test_scene.cpp
        src/text_font_setting.cpp
        src/title_screen.cpp
        src/new_game_mode.cpp
        src/ui_demo.cpp
        src/ui_skin_setting.cpp
        src/user_settings.cpp
        src/field_scene_weather.cpp
        src/weather_particles.cpp
        src/weather_drawer.cpp
        src/weather_ambience.cpp
    DEPS core platform audio video rhi assets localization animation render signs input script ui debug scene ruleset field partner save content session battle inventory screens story evolution gym fishing tournament curling weather modes
    OUTPUT_NAME dwe-game)

dwe_add_shaders(game FILES shaders/test_scene.slang shaders/scene_prims.slang shaders/weather.slang)

# The first start's screen asks for the player's disc before the game's UI has any content to draw with, so it uses
# Dear ImGui on the game's window and device (first_run_screen.hpp).
find_package(imgui CONFIG REQUIRED)
target_link_libraries(game PRIVATE imgui::imgui)
# The control channel's TCP socket (control_socket.hpp) needs Winsock on Windows.
if(WIN32)
    target_link_libraries(game PRIVATE ws2_32)
endif()

# A package's layout (scripts/package_linux.sh): the game in bin/ next to dwx, and the team's packs in packs/, where
# content::teamPacksFolder finds them. Installing drops the build tree's runtime path; a Linux package ships its own
# Vulkan loader in lib/, so the player needs none installed.
install(TARGETS game RUNTIME DESTINATION bin)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/content/packs/ DESTINATION packs)
if(DWE_PACKAGE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(game PROPERTIES INSTALL_RPATH "\$ORIGIN/../lib")
endif()
# The macOS package's Vulkan loader is in the application bundle's Contents/Frameworks (scripts/package_macos.sh).
if(APPLE)
    set_target_properties(game PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
endif()
# A Windows package's game opens no console window of its own; its output still goes wherever the launcher sends it.
if(WIN32 AND DWE_PACKAGE)
    target_link_options(game PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

# A package's bug reports (F12) name the commit it was built from; a development build asks git when a report is
# written.
set(DWE_BUILD_COMMIT "" CACHE STRING "The commit a package's bug reports name; empty asks git")
if(DWE_BUILD_COMMIT)
    target_compile_definitions(game PRIVATE DWE_BUILD_COMMIT="${DWE_BUILD_COMMIT}")
endif()

# The version a save file names as the game that wrote it.
target_compile_definitions(game PRIVATE DWE_GAME_VERSION="${PROJECT_VERSION}")
# A development build reads from the source tree, so editing reloads live; a package build (DWE_PACKAGE) names no
# folder of the build machine: it finds the team's packs next to the executable (content::teamPacksFolder) and its
# bug reports name DWE_BUILD_COMMIT.
if(NOT DWE_PACKAGE)
    # A bug report names the commit of the source the game was built from (git describe in this folder).
    target_compile_definitions(game PRIVATE DWE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    # The test scene's script, which a package does not ship.
    target_compile_definitions(game PRIVATE DWE_GAME_SCRIPTS="${CMAKE_CURRENT_SOURCE_DIR}/scripts")
endif()

# The game is an executable, so its tests build the sources they check into a test program of their own: the player's
# settings (settings.yaml), how the UI skin is chosen at start and changed, the minimap mode, its enemies and its items, the sprite characters drawn as models, the save and place debug
# commands, the state trace's lines, the control channel's requests, the title screen's and the game's save flows,
# the battle's root steps of a model, a map battle's enemies, the discard menu's list, the trade menu's cursor and the player page's City tab.
if(BUILD_TESTING)
    file(GLOB game_test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
    add_executable(dwe_game_tests ${game_test_sources} src/battle_glide.cpp src/battle_motion.cpp src/battle_trigger.cpp src/bug_report.cpp src/chibi_style.cpp src/chart_requirements.cpp src/control_json.cpp src/control_log.cpp src/control_socket.cpp src/curling_recruit_bar.cpp
                                  src/evolution_effects.cpp src/field_effects.cpp src/field_mist.cpp src/field_numbers.cpp src/flame_texture.cpp
                                  src/death_effects.cpp src/ending_effects.cpp
                                  src/device_names.cpp src/discard_screen.cpp src/game_saves.cpp src/money_box_screen.cpp src/hd_scenery_setting.cpp src/map_debug_overlay.cpp src/minimap_enemies.cpp src/minimap_items.cpp src/minimap_setting.cpp src/display_settings.cpp
                                  src/pause_box.cpp src/place_command.cpp src/player_chart.cpp src/player_city.cpp src/partner_set_command.cpp src/story_set_command.cpp src/story_state_json.cpp src/shop_job_bar.cpp src/remote_control.cpp src/save_command.cpp src/save_flow.cpp
                                  src/save_indicator.cpp src/save_machine_flow.cpp src/award_screen.cpp src/sleep_steps.cpp src/slot_description.cpp src/state_trace.cpp src/story_text_rows.cpp src/ui_skin_setting.cpp src/user_settings.cpp src/walk_control.cpp src/battle_speed.cpp src/gym_batch.cpp src/rule_options.cpp src/world_map.cpp
                                  src/graphics_options.cpp src/weather_particles.cpp src/volume_setting.cpp src/sprite_characters.cpp
                                  src/title_options.cpp src/new_game_mode.cpp)
    target_link_libraries(dwe_game_tests PRIVATE dwe::core dwe::assets dwe::platform dwe::localization dwe::ui dwe::content
                                                 dwe::script dwe::save dwe::partner dwe::session dwe::field dwe::evolution
                                                 dwe::inventory dwe::screens dwe::animation dwe::battle dwe::render dwe::signs
                                                 dwe::ruleset dwe::curling dwe::weather dwe::audio dwe::debug dwe::modes dwe_test_main)
    # The session's test packs and made-up species (session_fixture.hpp), for the save debug command.
    target_include_directories(dwe_game_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                                                      ${PROJECT_SOURCE_DIR}/packages/session/tests
                                                      # The systems' made-up tables, as
                                                      # "packages/game/<system>/tests/fixture.hpp".
                                                      ${PROJECT_SOURCE_DIR})
    target_compile_definitions(dwe_game_tests PRIVATE
        DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game"
        DWE_SKIN_TEST_PACK="${PROJECT_SOURCE_DIR}/tests/mods/ui_skin/test_contrast")
    if(WIN32)
        target_link_libraries(dwe_game_tests PRIVATE ws2_32)
    endif()
    dwe_apply_options(dwe_game_tests)
    dwe_add_to_package(dwe_game_tests)
    _dwe_add_tests(game dwe_game_tests ${game_test_sources})
endif()
