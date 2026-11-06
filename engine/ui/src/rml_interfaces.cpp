#include "rml_interfaces.hpp"

#include <dwe/core/log.hpp>
#include <dwe/ui/glyph_art.hpp>
#include <dwe/ui/image_reference.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>
#include <functional>
#include <utility>

#include "code_trust.hpp"

namespace dwe::ui {
namespace {

std::size_t fingerprintOf(const std::vector<std::byte>& bytes) {
  return std::hash<std::string_view>{}(
      std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

} // namespace

core::Result<assets::AssetId> assetIdOfPath(const assets::VirtualFileSystem& files,
                                            std::string_view path) {
  while (path.starts_with('/')) {
    path.remove_prefix(1);
  }
  const std::size_t slash = path.rfind('/');
  const std::size_t dot = path.rfind('.');
  std::string_view extension;
  if (dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash)) {
    extension = path.substr(dot);
    path = path.substr(0, dot);
  }
  auto id = assets::AssetId::parse(path);
  if (!id) {
    return id;
  }
  const assets::AssetSource* source = files.resolve(*id);
  if (source == nullptr) {
    return core::fail(std::format("no {} in the packs", path));
  }
  if (source->file.extension().string() != extension) {
    return core::fail(std::format("{}{} is asked for, but the packs have {}", path, extension,
                                  source->file.filename().string()));
  }
  return id;
}

std::string joinUiPath(std::string_view documentPath, std::string_view path) {
  if (path.starts_with(GeneratedPrefix)) {
    return std::string{path};
  }
  std::vector<std::string_view> parts;
  if (path.starts_with('/')) {
    path.remove_prefix(1);
  } else {
    const std::size_t slash = documentPath.rfind('/');
    std::string_view folder =
        slash == std::string_view::npos ? std::string_view{} : documentPath.substr(0, slash);
    while (!folder.empty()) {
      const std::size_t next = folder.find('/');
      parts.push_back(folder.substr(0, next));
      folder = next == std::string_view::npos ? std::string_view{} : folder.substr(next + 1);
    }
  }
  while (!path.empty()) {
    const std::size_t next = path.find('/');
    const std::string_view part = path.substr(0, next);
    if (part == "..") {
      if (!parts.empty()) {
        parts.pop_back();
      }
    } else if (!part.empty() && part != ".") {
      parts.push_back(part);
    }
    path = next == std::string_view::npos ? std::string_view{} : path.substr(next + 1);
  }
  std::string joined;
  for (const std::string_view part : parts) {
    if (!joined.empty()) {
      joined += '/';
    }
    joined += part;
  }
  return joined;
}

double SystemInterface::GetElapsedTime() {
  if (clock_) {
    return clock_();
  }
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void SystemInterface::JoinPath(Rml::String& translatedPath, const Rml::String& documentPath,
                               const Rml::String& path) {
  translatedPath = joinUiPath(documentPath, path);
}

bool SystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
  switch (type) {
  case Rml::Log::LT_ERROR:
  case Rml::Log::LT_ASSERT:
    core::logError("ui: {}", message);
    break;
  case Rml::Log::LT_WARNING:
    core::logWarning("ui: {}", message);
    break;
  case Rml::Log::LT_ALWAYS:
  case Rml::Log::LT_INFO:
    core::logInfo("ui: {}", message);
    break;
  case Rml::Log::LT_DEBUG:
  case Rml::Log::LT_MAX:
    break;
  }
  return true;
}

std::optional<std::vector<std::byte>> FileInterface::read(std::string_view path) {
  auto id = assetIdOfPath(*files_, path);
  if (!id) {
    core::logWarning("ui: {}", id.error().message);
    return std::nullopt;
  }
  auto bytes = files_->read(*id);
  if (!bytes) {
    return std::nullopt;
  }
  trust_->noteRead(path);
  recorded_[std::string{path}] = fingerprintOf(*bytes);
  return std::move(*bytes);
}

std::optional<std::size_t> FileInterface::fingerprint(std::string_view path) const {
  auto id = assetIdOfPath(*files_, path);
  if (!id) {
    return std::nullopt;
  }
  auto bytes = files_->read(*id);
  if (!bytes) {
    return std::nullopt;
  }
  return fingerprintOf(*bytes);
}

Rml::FileHandle FileInterface::Open(const Rml::String& path) {
  auto bytes = read(path);
  if (!bytes) {
    return 0;
  }
  const Rml::FileHandle handle = nextHandle_++;
  open_[handle] = OpenFile{.bytes = std::move(*bytes), .position = 0};
  return handle;
}

void FileInterface::Close(Rml::FileHandle file) {
  open_.erase(file);
}

size_t FileInterface::Read(void* buffer, size_t size, Rml::FileHandle file) {
  auto found = open_.find(file);
  if (found == open_.end()) {
    return 0;
  }
  OpenFile& open = found->second;
  const std::size_t count = std::min(size, open.bytes.size() - open.position);
  std::memcpy(buffer, open.bytes.data() + open.position, count);
  open.position += count;
  return count;
}

bool FileInterface::Seek(Rml::FileHandle file, long offset, int origin) {
  auto found = open_.find(file);
  if (found == open_.end()) {
    return false;
  }
  OpenFile& open = found->second;
  long base = 0;
  if (origin == SEEK_CUR) {
    base = static_cast<long>(open.position);
  } else if (origin == SEEK_END) {
    base = static_cast<long>(open.bytes.size());
  }
  const long target = base + offset;
  if (target < 0 || std::cmp_greater(target, open.bytes.size())) {
    return false;
  }
  open.position = static_cast<std::size_t>(target);
  return true;
}

size_t FileInterface::Tell(Rml::FileHandle file) {
  auto found = open_.find(file);
  return found == open_.end() ? 0 : found->second.position;
}

size_t FileInterface::Length(Rml::FileHandle file) {
  auto found = open_.find(file);
  return found == open_.end() ? 0 : found->second.bytes.size();
}

bool FileInterface::LoadFile(const Rml::String& path, Rml::String& data) {
  auto bytes = read(path);
  if (!bytes) {
    return false;
  }
  data.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
  return true;
}

bool TextureSources::isPixelArt(std::string_view source) {
  if (source.starts_with(GeneratedPrefix)) {
    return false;
  }
  // The classic glyphs are pixel art drawn at three dp a pixel; the modern ones are smooth.
  if (source.starts_with(GlyphPrefix)) {
    return source.starts_with(ClassicGlyphPrefix);
  }
  const auto reference = parseImageReference(source);
  return reference && ui::isPixelArt(*reference);
}

core::Result<DrawnImage> TextureSources::load(std::string_view source) const {
  const auto drawnAsItIs = [](assets::Image image) {
    const ImageSize size{.width = image.width, .height = image.height};
    return DrawnImage{.pixels = std::move(image), .size = size};
  };
  if (source.starts_with(GeneratedPrefix)) {
    auto found = generated_.find(source.substr(GeneratedPrefix.size()));
    if (found == generated_.end()) {
      return core::fail(std::format("no generated image '{}'", source));
    }
    return drawnAsItIs(found->second);
  }
  if (source.starts_with(GlyphPrefix)) {
    auto glyph = drawGlyph(source.substr(GlyphPrefix.size()));
    if (!glyph) {
      return std::unexpected{glyph.error()};
    }
    return drawnAsItIs(std::move(*glyph));
  }
  auto reference = parseImageReference(source);
  if (!reference) {
    return std::unexpected{reference.error()};
  }
  return loadDrawnImage(*files_, *reference);
}

} // namespace dwe::ui
