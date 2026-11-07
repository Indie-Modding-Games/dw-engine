dwe_add_module(partner
    LAYER game
    SOURCES
        src/section_reading.cpp
        src/care_rules.cpp
        src/life_rules.cpp
        src/training_rules.cpp
        src/strategies.cpp
        src/rules.cpp
        src/area_response.cpp
        src/species.cpp
        src/partner.cpp
        src/partner_tick.cpp
        src/partner_commands.cpp
        src/partner_activities.cpp
        src/partner_story.cpp
        src/poop_piles.cpp
        src/need_deadlines.cpp
        src/field_animation.cpp
        src/state_yaml.cpp
    DEPS core assets ruleset)

# The tests read the game pack and its schemas from the source tree.
if(TARGET dwe_partner_tests)
    target_compile_definitions(dwe_partner_tests PRIVATE
        DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game"
        DWE_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/content/schemas")
endif()
