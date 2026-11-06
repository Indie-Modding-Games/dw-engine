# The libraries, in dependency order: a module is declared before its users (tooling/cmake/DweModule.cmake).
# vendor first: thin build wrappers for external libraries, which the engine and formats link.
add_subdirectory(vendor)
add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(formats)
add_subdirectory(content)
add_subdirectory(session)
add_subdirectory(screens)
