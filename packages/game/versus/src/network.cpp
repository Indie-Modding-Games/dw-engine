#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/versus/network.hpp>

#include <format>

namespace dwe::game::versus {

namespace {

constexpr std::string_view Context = "versus/network.yaml";
constexpr std::string_view SignalServerKey = "signal_server";
constexpr std::string_view IceServersKey = "ice_servers";

bool urlWithScheme(std::string_view url, std::initializer_list<std::string_view> schemes) {
  for (const std::string_view scheme : schemes) {
    if (url.starts_with(scheme) && url.size() > scheme.size()) {
      return true;
    }
  }
  return false;
}

} // namespace

core::Result<NetworkConfig> parseNetworkConfig(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return core::fail(std::format("{}: {}", Context, tree.error().message));
  }
  assets::YamlReader reader;
  if (!reader.expectKeys(*tree, "the versus network", {SignalServerKey, IceServersKey})) {
    return std::unexpected{reader.error(Context)};
  }
  NetworkConfig config;
  const core::YamlNode& server = reader.field(*tree, SignalServerKey);
  config.signalServer = reader.optionalText(server).value_or("");
  if (!config.signalServer.empty() && !urlWithScheme(config.signalServer, {"wss://", "ws://"})) {
    reader.fail(server, "the signaling server is a ws:// or wss:// address");
  }
  for (const core::YamlNode& item :
       reader.sequence(reader.field(*tree, IceServersKey), "the ICE servers")) {
    std::string url = reader.text(item);
    if (reader.ok() && !urlWithScheme(url, {"stun:", "turn:", "turns:"})) {
      reader.fail(item, "an ICE server is a stun:, turn: or turns: address");
    }
    config.iceServers.push_back(std::move(url));
  }
  return reader.finish(std::move(config), Context);
}

core::Result<NetworkConfig> loadNetworkConfig(const assets::VirtualFileSystem& files) {
  auto id = assets::AssetId::parse(NetworkAssetId);
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto bytes = files.read(*id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  return parseNetworkConfig(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}

} // namespace dwe::game::versus
