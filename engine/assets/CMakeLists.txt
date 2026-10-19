find_package(lodepng CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(Opus CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)

dwe_add_module(assets
    LAYER systems
    SOURCES
        src/asset_id.cpp
        src/pack_manifest.cpp
        src/file_system.cpp
        src/virtual_file_system.cpp
        src/pak.cpp
        src/image.cpp
        src/model.cpp
        src/media_headers.cpp
        src/content_check.cpp
        src/ogg.cpp
        src/sound.cpp
        src/texture.cpp
    DEPS core
    PRIVATE_LIBS lodepng dwe_cgltf zstd::libzstd Opus::opus KTX::ktx
    TEST_LIBS lodepng Opus::opus KTX::ktx)

# The content check decodes every image of a pack; as in formats, debug builds link lodepng's release build, three
# times faster. Not on Windows, where debug and release code use different C runtimes.
if(NOT WIN32)
    set_target_properties(lodepng PROPERTIES MAP_IMPORTED_CONFIG_DEBUG Release)
endif()
