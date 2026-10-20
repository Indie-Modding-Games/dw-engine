# How the apps mount the packs: the content folder, its packs/ folder, the team's packs and the mods, as one virtual
# file system. The game and dwe-sim mount them the same way.
dwe_add_module(content
    LAYER apps
    SOURCES
        src/game_content.cpp
    DEPS core assets)

# The team's packs (game rules, menus, HUD, fonts) are read from the source tree when the content folder has no game
# pack of its own, so editing them reloads them live.
target_compile_definitions(dwe_content PRIVATE DWE_TEAM_PACKS="${PROJECT_SOURCE_DIR}/content/packs")
