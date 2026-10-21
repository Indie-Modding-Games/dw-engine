# dwe-sim (R3.5): plays scenarios of the game's systems many times without graphics, with seeds, and reports the
# distributions of what they measure (docs/guides/simulaciones.md).

# Any game module may bring a scenario, so the simulations may use them all.
get_property(game_modules GLOBAL PROPERTY DWE_GAME_MODULES)

# The tool's code is a module so its tests can link it; the command line is a thin app on top.
dwe_add_module(sim
    LAYER apps
    SOURCES
        src/headless_session.cpp
        src/report.cpp
        src/runner.cpp
        src/scenario.cpp
        src/sim_options.cpp
        src/simulation.cpp
    DEPS core assets ruleset content session ${game_modules})

# Each file of scenarios/ registers one scenario: adding a scenario is adding its file. An object library, so the
# linker keeps every registration although nothing calls into its file.
file(GLOB scenario_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/*.cpp)
add_library(dwe_sim_scenarios OBJECT ${scenario_sources})
target_link_libraries(dwe_sim_scenarios PUBLIC dwe::sim)
dwe_apply_options(dwe_sim_scenarios)

dwe_add_app(sim_cli
    LAYER apps
    SOURCES
        src/main.cpp
    DEPS sim content
    OUTPUT_NAME dwe-sim)
target_link_libraries(sim_cli PRIVATE dwe_sim_scenarios)

if(BUILD_TESTING)
    target_link_libraries(dwe_sim_tests PRIVATE dwe_sim_scenarios)
    # The game pack depends on base; a stand-in base pack with the original ruleset's tables lets the tests and the
    # smoke runs mount it without the disc. The tables are empty but digimon, raise, moves and items, which have a few
    # made-up species, moves and items for the care scenario and the session's systems (tests/stand-in), and
    # evolution, which has no rows.
    set(stand_in ${CMAKE_CURRENT_BINARY_DIR}/stand-in-content)
    file(WRITE ${stand_in}/base/pack.yaml "id: base\nversion: 0.1.0\nlayer: base\n")
    foreach(role training item_effects shops)
        file(WRITE ${stand_in}/base/data/${role}.yaml "${role}: []\n")
    endforeach()
    file(WRITE ${stand_in}/base/data/evolution.yaml "requirements: []\nstats_gains: []\npaths: []\n")
    foreach(role digimon raise moves items)
        configure_file(tests/stand-in/${role}.yaml ${stand_in}/base/data/${role}.yaml COPYONLY)
    endforeach()
    target_compile_definitions(dwe_sim_tests PRIVATE DWE_STAND_IN_CONTENT="${stand_in}")
    # The smoke run of check-full: ten runs of the clock scenario on the game pack, none of them off.
    add_test(NAME sim.smoke COMMAND sim_cli --content ${stand_in} --scenario clock --runs 10)
    set_tests_properties(sim.smoke PROPERTIES PASS_REGULAR_EXPRESSION "\\| mismatch \\| 10 \\| 0 \\| 0 \\|")
    # The care scenario's smoke run: a few raisings of a week on the made-up species, every run counted.
    add_test(NAME sim.care.smoke COMMAND sim_cli --content ${stand_in} --scenario care --runs 4 --days 7 --seed 3)
    set_tests_properties(sim.care.smoke PROPERTIES PASS_REGULAR_EXPRESSION "\\| deaths \\| 4 \\|")
endif()
