#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::versus {

// The asset with the online versus' servers (content/packs/game/versus/network.yaml).
inline constexpr std::string_view NetworkAssetId = "versus/network";

// Where the online versus finds the other machine: the signaling server that pairs the two by a
// room code, and the STUN/TURN servers that help them open a direct route.
struct NetworkConfig {
  // wss://... Empty: no server is set up, and «Versus en línea» says so instead of connecting.
  std::string signalServer;
  // "stun:host:port" or "turn:..." URLs.
  std::vector<std::string> iceServers;
};

core::Result<NetworkConfig> parseNetworkConfig(std::string_view yaml);
core::Result<NetworkConfig> loadNetworkConfig(const assets::VirtualFileSystem& files);

} // namespace dwe::game::versus
