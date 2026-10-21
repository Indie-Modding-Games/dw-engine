#pragma once

#include <dwe/assets/lua_api.hpp>

namespace dwe::script {

// The version of the Lua API that scripts see. Its owner is assets::SupportedLuaApi, with the rule that checks a pack's
// `api` against it (assets::checkPackApi): both are a data contract with pack.yaml that tools check without Lua.
inline constexpr assets::Version LuaApiVersion = assets::SupportedLuaApi;

} // namespace dwe::script
