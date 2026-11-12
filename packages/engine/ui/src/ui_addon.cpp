#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ui/ui_addon.hpp>

#include <algorithm>
#include <charconv>
#include <optional>
#include <utility>

#include "messages.hpp"

namespace dwe::ui {
namespace {

constexpr std::size_t MaxIdLength = 64;
constexpr std::size_t MaxNameLength = 64;
constexpr std::size_t MaxVersionLength = 32;
constexpr std::size_t MaxDocuments = 64;

// An addon id, a screen name: lowercase letters, digits and underscores, as the asset ids they
// become part of.
bool isPlainName(std::string_view name) {
  return !name.empty() && name.size() <= MaxIdLength && std::ranges::all_of(name, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  });
}

std::string plainName(assets::YamlReader& reader, const core::YamlNode& node) {
  std::string name = reader.text(node);
  if (reader.ok() && !isPlainName(name)) {
    reader.fail(node, messages::BadUiAddonId);
  }
  return name;
}

// A text shown to the player: one line, not empty, at most `most` bytes.
std::string shownText(assets::YamlReader& reader, const core::YamlNode& node, std::size_t most) {
  std::string text = reader.text(node);
  const bool oneLine = std::ranges::none_of(
      text, [](char c) { return static_cast<unsigned char>(c) < ' ' || c == '\x7f'; });
  if (reader.ok() && (text.empty() || text.size() > most || !oneLine)) {
    reader.fail(node, core::formatMessage(messages::BadUiAddonText, most));
  }
  return text;
}

// A document of the addon, relative to its folder.
std::optional<assets::AssetId> document(assets::YamlReader& reader, const core::YamlNode& node,
                                        std::string_view folder) {
  const std::string relative = reader.text(node);
  if (!reader.ok()) {
    return std::nullopt;
  }
  if (relative.starts_with('/') || relative.find("..") != std::string::npos) {
    reader.fail(node, messages::UiAddonDocumentOutside);
    return std::nullopt;
  }
  auto id = assets::AssetId::parse(std::string{folder} + "/" + relative);
  if (!id) {
    reader.fail(node, id.error().message);
    return std::nullopt;
  }
  return std::move(*id);
}

std::optional<std::uint32_t> number(std::string_view text) {
  std::uint32_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || text.empty()) {
    return std::nullopt;
  }
  return value;
}

// "0.8" or "0.8.1": a missing patch is 0.
std::optional<UiApiVersion> version(std::string_view text) {
  const std::size_t first = text.find('.');
  if (first == std::string_view::npos) {
    return std::nullopt;
  }
  const std::size_t second = text.find('.', first + 1);
  const auto major = number(text.substr(0, first));
  const auto minor = number(text.substr(
      first + 1, second == std::string_view::npos ? std::string_view::npos : second - first - 1));
  const auto patch = second == std::string_view::npos ? std::optional<std::uint32_t>{0}
                                                      : number(text.substr(second + 1));
  if (!major || !minor || !patch) {
    return std::nullopt;
  }
  return UiApiVersion{.major = *major, .minor = *minor, .patch = *patch};
}

} // namespace

core::Result<UiApiRange> parseUiApiRange(std::string_view text) {
  const auto bad = [&] { return core::fail(core::formatMessage(messages::BadUiApiRange, text)); };
  if (auto single = version(text)) {
    return UiApiRange{.minimum = *single,
                      .below = UiApiVersion{.major = single->major + 1, .minor = 0, .patch = 0}};
  }
  UiApiRange range{.minimum = {}, .below = {.major = UINT32_MAX, .minor = 0, .patch = 0}};
  bool lower = false;
  bool upper = false;
  std::size_t start = 0;
  while (start < text.size()) {
    std::size_t end = text.find(' ', start);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    const std::string_view bound = text.substr(start, end - start);
    start = end + 1;
    if (bound.empty()) {
      continue;
    }
    if (bound.starts_with(">=") && !lower) {
      auto read = version(bound.substr(2));
      if (!read) {
        return bad();
      }
      range.minimum = *read;
      lower = true;
    } else if (bound.starts_with('<') && !bound.starts_with("<=") && !upper) {
      auto read = version(bound.substr(1));
      if (!read) {
        return bad();
      }
      range.below = *read;
      upper = true;
    } else {
      return bad();
    }
  }
  if (!lower && !upper) {
    return bad();
  }
  if (!(range.minimum < range.below)) {
    return core::fail(core::formatMessage(messages::EmptyUiApiRange, text));
  }
  return range;
}

core::Result<UiAddon> parseUiAddon(std::string_view yaml, std::string_view folder) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  assets::YamlReader reader;
  UiAddon addon;
  reader.expectKeys(*tree, messages::UiAddonWhat, {"id", "name", "version", "ui_api", "type"},
                    {"screens", "documents"});
  addon.id = plainName(reader, reader.field(*tree, "id"));
  addon.name = shownText(reader, reader.field(*tree, "name"), MaxNameLength);
  addon.version = shownText(reader, reader.field(*tree, "version"), MaxVersionLength);
  const core::YamlNode& range = reader.field(*tree, "ui_api");
  if (const std::string text = reader.text(range); reader.ok()) {
    if (auto read = parseUiApiRange(text)) {
      addon.uiApi = *read;
    } else {
      reader.fail(range, read.error().message);
    }
  }
  const core::YamlNode& type = reader.field(*tree, "type");
  if (const std::string text = reader.text(type); text == "ui") {
    addon.type = UiAddonType::Ui;
  } else if (text == "plugin") {
    addon.type = UiAddonType::Plugin;
  } else if (reader.ok()) {
    reader.fail(type, messages::BadUiAddonType);
  }
  const core::YamlNode* screens = tree->find("screens");
  const core::YamlNode* documents = tree->find("documents");
  if (reader.ok() && addon.type == UiAddonType::Ui) {
    if (documents != nullptr) {
      reader.fail(*documents, core::formatMessage(messages::UiAddonWrongPart, "ui", "documents"));
    } else if (screens == nullptr) {
      reader.fail(*tree, messages::UiAddonNeedsScreens);
    } else if (!screens->isMapping() || screens->entries().empty() ||
               screens->entries().size() > MaxDocuments) {
      reader.fail(*screens, messages::UiAddonScreensNotMapping);
    } else {
      for (const core::YamlEntry& entry : screens->entries()) {
        if (!isPlainName(entry.key)) {
          reader.fail(entry.value, messages::BadUiAddonId);
        }
        if (auto id = document(reader, entry.value, folder)) {
          addon.screens.insert_or_assign(entry.key, std::move(*id));
        }
      }
    }
  }
  if (reader.ok() && addon.type == UiAddonType::Plugin) {
    if (screens != nullptr) {
      reader.fail(*screens, core::formatMessage(messages::UiAddonWrongPart, "plugin", "screens"));
    } else if (documents == nullptr) {
      reader.fail(*tree, messages::UiAddonNeedsDocuments);
    } else {
      for (const core::YamlNode& node :
           reader.sequence(*documents, messages::UiAddonDocumentsWhat)) {
        if (auto id = document(reader, node, folder)) {
          addon.documents.push_back(std::move(*id));
        }
      }
      if (reader.ok() && (addon.documents.empty() || addon.documents.size() > MaxDocuments)) {
        reader.fail(*documents, messages::UiAddonNeedsDocuments);
      }
    }
  }
  return reader.finish(std::move(addon), messages::UiAddonContext);
}

core::Result<UiAddon> readUiAddon(const assets::VirtualFileSystem& files, std::string_view folder) {
  auto id = assets::AssetId::parse(std::string{folder} + "/" + std::string{UiAddonManifest});
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto bytes = files.read(*id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  return parseUiAddon(std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()},
                      folder);
}

} // namespace dwe::ui
