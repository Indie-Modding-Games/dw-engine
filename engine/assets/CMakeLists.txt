find_package(lodepng CONFIG REQUIRED)

dwe_add_module(assets
    LAYER systems
    SOURCES
        src/asset_id.cpp
        src/pack_manifest.cpp
        src/file_system.cpp
        src/virtual_file_system.cpp
        src/pak.cpp
        src/image.cpp
    DEPS core
    PRIVATE_LIBS lodepng
    TEST_LIBS lodepng)
