#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/lua_api.hpp>
#include <dwe/content/user_ui_addons.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

#include "messages.hpp"

namespace dwe::content {
namespace {

constexpr std::string_view ManifestFile = "pack.yaml";
constexpr std::string_view AddonsFolder = "ui/addons";
constexpr std::string_view AddonManifest = "addon.yaml";
// What macOS adds to the archives it makes: resource forks, never part of an addon.
constexpr std::string_view MacFolder = "__MACOSX";

std::span<const std::byte> bytesOf(std::string_view text) {
  return std::as_bytes(std::span{text.data(), text.size()});
}

// A .zip file name, of any case.
bool isZipName(std::string_view name) {
  constexpr std::string_view Extension = ".zip";
  if (name.size() <= Extension.size()) {
    return false;
  }
  const std::string_view end = name.substr(name.size() - Extension.size());
  return std::ranges::equal(end, Extension, [](char a, char b) {
    return (a >= 'A' && a <= 'Z' ? static_cast<char>(a - 'A' + 'a') : a) == b;
  });
}

// An archive entry no addon has: a hidden file or folder (as the folders on disk leave out) or
// what macOS adds to the archives it makes.
bool isIgnored(std::string_view name) {
  if (name.starts_with(std::string{MacFolder} + "/")) {
    return true;
  }
  return name.starts_with('.') || name.find("/.") != std::string_view::npos;
}

// The folder of an archive that holds addon.yaml: "" at its root, "<folder>/" when every file is in
// that one folder and it holds it, nothing for any other archive.
std::optional<std::string> addonRoot(const ZipArchive& zip) {
  std::optional<std::string> top;
  bool oneFolder = true;
  for (const ZipEntry& entry : zip.entries()) {
    if (isIgnored(entry.name)) {
      continue;
    }
    if (entry.name == AddonManifest) {
      return std::string{};
    }
    const std::size_t slash = entry.name.find('/');
    const std::string folder =
        slash == std::string::npos ? std::string{} : entry.name.substr(0, slash + 1);
    if (folder.empty() || (top && *top != folder)) {
      oneFolder = false;
    }
    top = top.value_or(folder);
  }
  if (oneFolder && top && zip.find(*top + std::string{AddonManifest}) != nullptr) {
    return top;
  }
  return std::nullopt;
}

// The id an addon.yaml declares, if it is one segment of an asset id (the folder it shows as).
// Whether it suits the rest of the manifest is ui::findUiAddons's to say, as for folders.
std::optional<std::string> declaredId(std::span<const std::byte> manifest) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): YAML is read as text.
  const std::string_view text{reinterpret_cast<const char*>(manifest.data()), manifest.size()};
  auto tree = core::parseYaml(text);
  if (!tree || !tree->isMapping()) {
    return std::nullopt;
  }
  const core::YamlNode* id = tree->find("id");
  if (id == nullptr || !id->isScalar() || id->text().find('/') != std::string::npos ||
      !assets::AssetId::parse(id->text())) {
    return std::nullopt;
  }
  return id->text();
}

} // namespace

UserUiAddonsFileSystem::UserUiAddonsFileSystem(std::filesystem::path root, const ZipLimits& limits)
    : root_{std::move(root)},
      manifest_{std::format("id: {}\nversion: 0.1.0\nlayer: user\napi: \"{}.{}\"\npriority: 0\n"
                            "name: \"Interfaces del jugador\"\n"
                            "description: \"Los addons de interfaz copiados en la carpeta de datos "
                            "del jugador.\"\n"
                            "dependencies:\n  - id: game\n    min_version: 0.1.0\n",
                            UserUiAddonsPack, assets::SupportedLuaApi.major,
                            assets::SupportedLuaApi.minor)} {
  openArchives(limits);
}

void UserUiAddonsFileSystem::openArchives(const ZipLimits& limits) {
  std::error_code error;
  std::vector<std::string> names;
  std::set<std::string, std::less<>> folders;
  for (std::filesystem::directory_iterator entries{root_ / AddonsFolder, error}, end;
       !error && entries != end; entries.increment(error)) {
    const std::string name = entries->path().filename().generic_string();
    if (name.starts_with('.')) {
      continue;
    }
    std::error_code kind;
    if (entries->is_directory(kind)) {
      folders.insert(name);
    } else if (isZipName(name)) {
      names.push_back(name);
    }
  }
  std::ranges::sort(names);
  // Each id an archive gives, and that archive.
  std::map<std::string, std::string, std::less<>> used;
  for (const std::string& name : names) {
    const std::filesystem::path path = root_ / AddonsFolder / name;
    const std::string archive = std::format("{}/{}", AddonsFolder, name);
    LeftOutUiAddonArchive leftOut{.folder = archive, .reason = {}, .shadowed = false};
    const auto leaveOut = [&](std::string reason) {
      leftOut.reason = std::move(reason);
      leftOut_.push_back(std::move(leftOut));
    };
    auto zip = ZipArchive::open(path, limits);
    if (!zip) {
      leaveOut(zip.error().message);
      continue;
    }
    const auto inArchive = [&](std::string_view message) {
      return core::formatMessage(messages::ZipInFile, assets::pathText(path), message);
    };
    const std::optional<std::string> prefix = addonRoot(*zip);
    if (!prefix) {
      leaveOut(inArchive(messages::ZipNoManifest));
      continue;
    }
    auto manifest = zip->read(*zip->find(*prefix + std::string{AddonManifest}));
    if (!manifest) {
      leaveOut(manifest.error().message);
      continue;
    }
    const std::optional<std::string> id = declaredId(*manifest);
    if (!id) {
      leaveOut(inArchive(messages::ZipNoAddonId));
      continue;
    }
    leftOut.folder = std::format("{}/{}", AddonsFolder, *id);
    if (folders.contains(*id)) {
      leftOut.shadowed = true;
      leaveOut(inArchive(core::formatMessage(messages::ZipSameAsFolder, *id)));
      continue;
    }
    if (const auto first = used.find(*id); first != used.end()) {
      leftOut.shadowed = true;
      leaveOut(inArchive(core::formatMessage(messages::ZipSameAsZip, first->second, *id)));
      continue;
    }
    // Every file must give its own asset id, as the folders' must, or the whole pack of the
    // player's addons would not mount.
    std::map<std::string, ArchiveFile, std::less<>> files;
    std::set<assets::AssetId> ids;
    std::string bad;
    for (std::size_t index = 0; index < zip->entries().size() && bad.empty(); ++index) {
      const ZipEntry& entry = zip->entries()[index];
      if (isIgnored(entry.name) || !entry.name.starts_with(*prefix)) {
        continue;
      }
      const std::string inner = entry.name.substr(prefix->size());
      if (isIgnored(inner)) {
        continue;
      }
      std::string file = std::format("{}/{}/{}", AddonsFolder, *id, inner);
      auto asset = assets::assetIdOfFile(file);
      if (!asset) {
        bad = core::formatMessage(messages::ZipBadFileName, entry.name, asset.error().message);
      } else if (!ids.insert(*asset).second) {
        bad = core::formatMessage(messages::ZipBadFileName, entry.name,
                                  std::format("another file gives {}", asset->str()));
      }
      files.emplace(std::move(file), ArchiveFile{.archive = archives_.size(), .entry = index});
    }
    if (!bad.empty()) {
      leaveOut(inArchive(bad));
      continue;
    }
    used.emplace(*id, assets::pathText(path));
    archives_.push_back(std::move(*zip));
    archiveFiles_.merge(files);
  }
}

bool UserUiAddonsFileSystem::hasAddons() const {
  if (!archiveFiles_.empty()) {
    return true;
  }
  auto files = disk_.listFiles(root_ / AddonsFolder);
  return files && std::ranges::any_of(*files, [](const std::string& file) {
           return file.find('/') != std::string::npos || !isZipName(file);
         });
}

std::string UserUiAddonsFileSystem::addonFile(const std::filesystem::path& file) const {
  const std::filesystem::path relative = file.lexically_normal().lexically_relative(root_);
  const std::string text = relative.generic_string();
  const std::string folder = std::string{AddonsFolder} + "/";
  if (!text.starts_with(folder) || text.find("..") != std::string::npos) {
    return {};
  }
  if (const std::string_view name = std::string_view{text}.substr(folder.size());
      name.find('/') == std::string_view::npos && isZipName(name)) {
    return {};
  }
  return text;
}

const UserUiAddonsFileSystem::ArchiveFile*
UserUiAddonsFileSystem::archiveFile(std::string_view addonFile) const {
  const auto found = archiveFiles_.find(addonFile);
  return found == archiveFiles_.end() ? nullptr : &found->second;
}

core::Result<std::vector<std::string>>
UserUiAddonsFileSystem::listFiles(const std::filesystem::path& folder) const {
  if (folder.lexically_normal() != root_.lexically_normal()) {
    return core::fail(std::format("{} lists only its root", UserUiAddonsPack));
  }
  std::vector<std::string> files{std::string{ManifestFile}};
  for (const auto& [file, where] : archiveFiles_) {
    files.push_back(file);
  }
  std::error_code error;
  if (!std::filesystem::is_directory(root_ / AddonsFolder, error)) {
    return files;
  }
  auto addons = disk_.listFiles(root_ / AddonsFolder);
  if (!addons) {
    return std::unexpected{addons.error()};
  }
  for (std::string& file : *addons) {
    // The archives show as the folders they hold.
    if (file.find('/') == std::string::npos && isZipName(file)) {
      continue;
    }
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
  const std::string relative = addonFile(file);
  if (relative.empty()) {
    return core::fail(std::format("{} has no file {}", UserUiAddonsPack, file.generic_string()));
  }
  if (const ArchiveFile* inArchive = archiveFile(relative); inArchive != nullptr) {
    const ZipArchive& zip = archives_[inArchive->archive];
    return zip.read(zip.entries()[inArchive->entry]);
  }
  return disk_.readFile(file);
}

core::Result<std::uint64_t>
UserUiAddonsFileSystem::fileSize(const std::filesystem::path& file) const {
  if (file.lexically_normal() == (root_ / ManifestFile).lexically_normal()) {
    return manifest_.size();
  }
  const std::string relative = addonFile(file);
  if (relative.empty()) {
    return core::fail(std::format("{} has no file {}", UserUiAddonsPack, file.generic_string()));
  }
  if (const ArchiveFile* inArchive = archiveFile(relative); inArchive != nullptr) {
    return archives_[inArchive->archive].entries()[inArchive->entry].size;
  }
  return disk_.fileSize(file);
}

core::Result<std::unique_ptr<assets::FileReader>>
UserUiAddonsFileSystem::openFile(const std::filesystem::path& file) const {
  // An archive's file is read whole (FileSystem::openFile): its size is bounded (ZipLimits).
  if (const std::string relative = addonFile(file);
      !relative.empty() && archiveFile(relative) == nullptr) {
    return disk_.openFile(file);
  }
  return FileSystem::openFile(file);
}

} // namespace dwe::content
