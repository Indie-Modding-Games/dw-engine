# Shader helpers. Shaders are written in Slang (ADR 0003, ADR 0018) and compiled to SPIR-V at build time by slangc, a
# host tool from vcpkg (the shader-slang port). The SPIR-V is embedded in the binary as a generated header, so the
# runtime never looks for shader files.

# Only the slangc of the vcpkg host triplet: a Vulkan SDK on PATH may carry another version.
find_program(DWE_SLANGC slangc REQUIRED NO_DEFAULT_PATH
             HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/shader-slang")
execute_process(COMMAND "${DWE_SLANGC}" -v OUTPUT_VARIABLE slangc_version ERROR_VARIABLE slangc_version
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
message(STATUS "slangc ${slangc_version}: ${DWE_SLANGC}")

# Shared Slang modules every shader may import, such as `import dwe.bindless;`: they live with the rhi, which owns
# the descriptor layout they mirror.
set(DWE_SHADER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/packages/engine/rhi/shaders")
# Every shader also depends on all of them, in case a generator ignores the depfiles.
file(GLOB_RECURSE DWE_SHARED_SHADERS CONFIGURE_DEPENDS "${DWE_SHADER_INCLUDE_DIR}/*.slang")

# dwe_add_shaders(<target> FILES <files.slang...>)
# Compiles each file, with every entry point it marks with [shader(...)], into one SPIR-V module, and embeds it in the
# generated header <dwe/shaders/<stem>.hpp> as `dwe::shaders::<Stem>`, a std::array<std::uint32_t, N> (the stem in
# PascalCase: textured_quad.slang gives TexturedQuad). The header is private to <target>. Slang warnings are errors.
function(dwe_add_shaders target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "FILES")
    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    foreach(file IN LISTS arg_FILES)
        cmake_path(ABSOLUTE_PATH file BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE source)
        cmake_path(GET source STEM stem)
        string(REPLACE "_" ";" words "${stem}")
        set(name "")
        foreach(word IN LISTS words)
            string(SUBSTRING "${word}" 0 1 first)
            string(SUBSTRING "${word}" 1 -1 rest)
            string(TOUPPER "${first}" first)
            string(APPEND name "${first}${rest}")
        endforeach()

        set(spirv "${CMAKE_CURRENT_BINARY_DIR}/shaders/${stem}.spv")
        set(header "${generated_dir}/dwe/shaders/${stem}.hpp")
        add_custom_command(
            OUTPUT "${spirv}"
            COMMAND "${DWE_SLANGC}" "${source}" -target spirv -I "${DWE_SHADER_INCLUDE_DIR}"
                    -fvk-use-entrypoint-name -matrix-layout-column-major -warnings-as-errors all
                    -depfile "${spirv}.d" -o "${spirv}"
            DEPENDS "${source}" ${DWE_SHARED_SHADERS}
            DEPFILE "${spirv}.d"
            COMMENT "Compiling shader ${stem}.slang"
            VERBATIM)
        add_custom_command(
            OUTPUT "${header}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT=${spirv}" "-DOUTPUT=${header}" "-DNAME=${name}"
                    -P "${PROJECT_SOURCE_DIR}/tooling/cmake/embed_spirv.cmake"
            DEPENDS "${spirv}" "${PROJECT_SOURCE_DIR}/tooling/cmake/embed_spirv.cmake"
            COMMENT "Embedding shader ${stem}.spv"
            VERBATIM)
        target_sources(${target} PRIVATE "${header}")
    endforeach()
    target_include_directories(${target} PRIVATE "${generated_dir}")
endfunction()
