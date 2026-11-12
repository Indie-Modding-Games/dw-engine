#include <dwe/assets/lua_api.hpp>
#include <dwe/content/user_ui_addons.hpp>
#include <dwe/core/error.hpp>

#include <format>
#include <system_error>
#include <utility>

namespace dwe::content {
namespace {

constexpr std::string_view ManifestFile = "pack.yaml";
constexpr std::string_view AddonsFolder = "ui/addons";

std::span<const std::byte> bytesOf(std::string_view text) {
  return std::as_bytes(std::span{text.data(), text.size()});
}

} // namespace

UserUiAddonsFileSystem::UserUiAddonsFileSystem(std::filesystem::path root)
    : root_{std::move(root)},
      manifest_{std::format("id: {}\nversion: 0.1.0\nlayer: user\napi: \"{}.{}\"\npriority: 0\n"
                            "name: \"Interfaces del jugador\"\n"
                            "description: \"Los addons de interfaz copiados en la carpeta de datos "
                            "del jugador.\"\n"
                            "dependencies:\n  - id: game\n    min_version: 0.1.0\n",
                            UserUiAddonsPack, assets::SupportedLuaApi.major,
                            assets::SupportedLuaApi.minor)} {}

bool UserUiAddonsFileSystem::hasAddons() const {
  auto files = disk_.listFiles(root_ / AddonsFolder);
  return files && !files->empty();
}

std::string UserUiAddonsFileSystem::addonFile(const std::filesystem::path& file) const {
  const std::filesystem::path relative = file.lexically_normal().lexically_relative(root_);
  const std::string text = relative.generic_string();
  if (text.starts_with(std::string{AddonsFolder} + "/") && text.find("..") == std::string::npos) {
    return text;
  }
  return {};
}

core::Result<std::vector<std::string>>
UserUiAddonsFileSystem::listFiles(const std::filesystem::path& folder) const {
  if (folder.lexically_normal() != root_.lexically_normal()) {
    return core::fail(std::format("{} lists only its root", UserUiAddonsPack));
  }
  std::vector<std::string> files{std::string{ManifestFile}};
  std::error_code error;
  if (!std::filesystem::is_directory(root_ / AddonsFolder, error)) {
    return files;
  }
  auto addons = disk_.listFiles(root_ / AddonsFolder);
  if (!addons) {
    return std::unexpected{addons.error()};
  }
  for (std::string& file : *addons) {
    files.push_back(std::format("{}/{}", AddonsFolder, file));
  }
  return files;
}

core::Result<std::vector<std::byte>>
UserUiAddonsFileSystem::readFile(const std::filesystem::path& file) const {
  if (file.lexically_normal() == (root_ / ManifestFile).lexically_normal()) {
    const auto bytes = bytesOf(manifest_);
    return std::vector<std::byte>{bytes.begin(), bytes.end()};
  }
  if (addonFile(file).empty()) {
    return core::fail(std::format("{} has no file {}", UserUiAddonsPack, file.generic_string()));
  }
  return disk_.readFile(file);
}

core::Result<std::uint64_t>
UserUiAddonsFileSystem::fileSize(const std::filesystem::path& file) const {
  if (file.lexically_normal() == (root_ / ManifestFile).lexically_normal()) {
    return manifest_.size();
  }
  if (addonFile(file).empty()) {
    return core::fail(std::format("{} has no file {}", UserUiAddonsPack, file.generic_string()));
  }
  return disk_.fileSize(file);
}

core::Result<std::unique_ptr<assets::FileReader>>
UserUiAddonsFileSystem::openFile(const std::filesystem::path& file) const {
  if (!addonFile(file).empty()) {
    return disk_.openFile(file);
  }
  return FileSystem::openFile(file);
}

} // namespace dwe::content
