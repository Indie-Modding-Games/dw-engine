# The libraries, in dependency order: a module is declared before its users (tooling/cmake/DweModule.cmake).
add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(formats)
add_subdirectory(content)
add_subdirectory(session)
add_subdirectory(screens)
