#include <dwe/core/yaml.hpp>
#include <dwe/formats/disc/known_releases.hpp>

#include <fstream>
#include <print>
#include <vector>

#include "dwx.hpp"
#include "game_tables.hpp"

namespace dwe::dwx {

int extractUiChart(const std::filesystem::path& executable, const std::filesystem::path& output) {
  if (std::filesystem::exists(output)) {
    std::println(stderr, "refusing to overwrite {}", output.string());
    return ExitFailed;
  }
  const auto digest = hashFile(executable);
  if (!digest) {
    std::println(stderr, "{}", digest.error().message);
    return ExitFailed;
  }
  const auto release = formats::disc::findKnownRelease(*digest);
  if (!release || release->file != formats::disc::ReleaseFile::Executable) {
    std::println(stderr, "UI chart extraction requires a recognised original executable");
    return ExitUnknown;
  }
  const auto layout = formats::exe::findExecutableLayout(release->version);
  if (!layout || !layout->evolutionChart.located()) {
    std::println(stderr, "UI chart layout is not located for {}", release->version);
    return ExitUnknown;
  }
  std::ifstream file{executable, std::ios::binary | std::ios::ate};
  const auto size = file.tellg();
  if (size <= 0 || size > 4 * 1024 * 1024) {
    std::println(stderr, "invalid executable size");
    return ExitFailed;
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    std::println(stderr, "cannot read {}", executable.string());
    return ExitFailed;
  }
  const auto tables = readGameTables(bytes, *layout);
  if (!tables) {
    std::println(stderr, "{}", tables.error().message);
    return ExitFailed;
  }
  const auto documents = formats::exe::tablesToYaml(tables->tables);
  for (const auto& document : documents.documents) {
    if (document.name != "evolution_chart")
      continue;
    const auto yaml = core::emitYaml(document.document);
    if (!yaml) {
      std::println(stderr, "{}", yaml.error().message);
      return ExitFailed;
    }
    if (auto written = writeFile(output, std::as_bytes(std::span{*yaml})); !written) {
      std::println(stderr, "{}", written.error().message);
      return ExitFailed;
    }
    std::println("wrote {} chart entries to {}", tables->tables.evolutionChart.size(),
                 output.string());
    return ExitOk;
  }
  std::println(stderr, "no chart metadata was extracted");
  return ExitUnknown;
}

} // namespace dwe::dwx
