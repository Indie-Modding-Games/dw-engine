# How the apps mount the packs: the content folder, its packs/ folder, the team's packs and the mods, as one virtual
# file system. The game and dwe-sim mount them the same way.
dwe_add_module(content
    LAYER apps
    SOURCES
        src/dev_assets.cpp
        src/dev_disc.cpp
        src/disc_install.cpp
        src/game_content.cpp
        src/user_ui_addons.cpp
    DEPS core assets platform)

# The team's packs (game rules, menus, HUD, fonts) are read from the source tree when the content folder has no game
# pack of its own and no package's packs/ folder is next to the executable's, so editing them reloads them live.
# A package build (DWE_PACKAGE) names no folder of the build machine: its packs are next to the executable.
# It ships them as .pak archives and loads only those (content::teamPacksFolder).
if(NOT DWE_PACKAGE)
    target_compile_definitions(dwe_content PRIVATE DWE_TEAM_PACKS="${PROJECT_SOURCE_DIR}/content/packs")
else()
    target_compile_definitions(dwe_content PRIVATE DWE_PACKAGE)
endif()

if(BUILD_TESTING)
    # The banner's document and texts in the game pack name the banner key that content owns.
    target_compile_definitions(dwe_content_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
    if(DWE_PACKAGE)
        target_compile_definitions(dwe_content_tests PRIVATE DWE_PACKAGE)
    endif()
endif()
