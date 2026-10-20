# The ruleset format: what every game system reads its rules from (docs/architecture/rulesets.md). A service, so the
# game modules can all use it without using each other.
dwe_add_module(ruleset
    LAYER services
    SOURCES
        src/ruleset.cpp
        src/ruleset_store.cpp
        src/game_clock.cpp
        src/ruleset_values.cpp
        src/section_file.cpp
    DEPS core assets)

# The tests read the game pack and the schemas from the source tree.
if(TARGET dwe_ruleset_tests)
    target_compile_definitions(dwe_ruleset_tests PRIVATE
        DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game"
        DWE_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/content/schemas")
    # Saving a section writes to a pack on disk, in the temporary folder of the assets tests.
    target_include_directories(dwe_ruleset_tests PRIVATE ${PROJECT_SOURCE_DIR}/engine/assets/tests)
endif()
