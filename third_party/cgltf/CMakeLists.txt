# cgltf is two headers without a CMake package. This library compiles them once, so the engine, which reads glTF,
# and formats, which also writes it, can both link it.
find_path(CGLTF_INCLUDE_DIR cgltf.h REQUIRED)
add_library(dwe_cgltf STATIC cgltf.cpp)
target_include_directories(dwe_cgltf SYSTEM PUBLIC ${CGLTF_INCLUDE_DIR})
# The project's options too: the same sanitizers and standard library checks as the code that calls it.
dwe_apply_options(dwe_cgltf)
