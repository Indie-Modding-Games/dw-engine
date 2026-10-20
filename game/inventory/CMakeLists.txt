dwe_add_module(inventory
    LAYER game
    SOURCES
        src/rules.cpp
        src/stacking.cpp
        src/inventory.cpp
        src/item_effects.cpp
        src/use.cpp
        src/shop.cpp
        src/inventory_yaml.cpp
    DEPS core assets ruleset)

# The tests read the game pack from the source tree.
if(TARGET dwe_inventory_tests)
    target_compile_definitions(dwe_inventory_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
