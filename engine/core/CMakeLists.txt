find_package(yaml CONFIG REQUIRED)

dwe_add_module(core
    LAYER core
    SOURCES
        src/log.cpp
        src/sha1.cpp
        src/yaml.cpp
    PRIVATE_LIBS yaml)
