#pragma once

#include <dwe/assets/pack_manifest.hpp>
#include <dwe/core/error.hpp>

#include <string_view>

namespace dwe::assets {

// The version of the Lua API that the engine gives scripts (ADR 0004), in semver: a change that can
// break a pack's scripts raises the major version, an addition the minor one. It lives here, next
// to the `api` key of pack.yaml that it answers, because it is a data contract between packs and
// the engine: the tools check packs against it without the Lua runtime, and the script module
// re-exports it as script::LuaApiVersion.
inline constexpr Version SupportedLuaApi{.major = 1, .minor = 2, .patch = 0};

// Whether scripts written for an API run on an engine with the provided one: they need its major
// version and, when they name one, at least their minor version. The error says why not.
core::Result<void> checkApiCompatibility(const ApiRequirement& required,
                                         const Version& provided = SupportedLuaApi);

// The same for a requirement written as in pack.yaml, such as "1.x" or "1.3".
core::Result<void> checkApiCompatibility(std::string_view required,
                                         const Version& provided = SupportedLuaApi);

// Whether a pack's scripts run on this engine. A pack that declares no API is taken to be written
// for this one.
core::Result<void> checkPackApi(const PackManifest& pack,
                                const Version& provided = SupportedLuaApi);

} // namespace dwe::assets
