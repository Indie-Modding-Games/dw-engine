# Build wrappers for external libraries that vcpkg installs without a CMake package. They are not modules: no layer,
# no dependency rules, no pkg_ target.
add_subdirectory(cgltf)
