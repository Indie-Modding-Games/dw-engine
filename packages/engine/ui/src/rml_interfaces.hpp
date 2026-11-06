#pragma once

#include <dwe/assets/image.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/ui/image_reference.hpp>

#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::ui {

class CodeTrust;

// The prefix of the images the UI makes itself, such as the minimap: `generated:minimap/3`.
inline constexpr std::string_view GeneratedPrefix = "generated:";

// The asset a path that RmlUi asks for names: the path without its extension, so
// `ui/menu/style.rcss` is the asset `ui/menu/style`. Fails for a path that is not inside the packs,
// and when the file that provides the asset has another extension (`style.rcss` asked for,
// `style.png` found).
core::Result<assets::AssetId> assetIdOfPath(const assets::VirtualFileSystem& files,
                                            std::string_view path);

// Joins a path found in a document to the document's own path, as RmlUi asks for it: relative paths
// start at the document's folder, a leading '/' starts at the packs' root, and `generated:` names
// stay as they are.
std::string joinUiPath(std::string_view documentPath, std::string_view path);

// RmlUi's clock, log and paths.
class SystemInterface final : public Rml::SystemInterface {
public:
  explicit SystemInterface(std::function<double()> clock) : clock_{std::move(clock)} {}

  double GetElapsedTime() override;
  void JoinPath(Rml::String& translatedPath, const Rml::String& documentPath,
                const Rml::String& path) override;
  bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

private:
  std::function<double()> clock_;
};

// RmlUi's files, read by asset id from the virtual file system. It remembers what each document
// read, so a document can be reloaded when one of its files changes.
class FileInterface final : public Rml::FileInterface {
public:
  // Tells trust of every file it reads, so a document that reads a template from a mod loses its
  // own code.
  FileInterface(const assets::VirtualFileSystem& files, CodeTrust& trust)
      : files_{&files}, trust_{&trust} {}

  Rml::FileHandle Open(const Rml::String& path) override;
  void Close(Rml::FileHandle file) override;
  size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
  bool Seek(Rml::FileHandle file, long offset, int origin) override;
  size_t Tell(Rml::FileHandle file) override;
  size_t Length(Rml::FileHandle file) override;
  bool LoadFile(const Rml::String& path, Rml::String& data) override;

  // Reads an asset by its path, as Open does.
  [[nodiscard]] std::optional<std::vector<std::byte>> read(std::string_view path);

  // Starts and ends recording the assets that are read, with what they held.
  void startRecording() { recorded_.clear(); }
  [[nodiscard]] std::map<std::string, std::size_t> stopRecording() { return std::move(recorded_); }

  // A fingerprint of what an asset holds now, or nullopt if it cannot be read.
  [[nodiscard]] std::optional<std::size_t> fingerprint(std::string_view path) const;

private:
  struct OpenFile {
    std::vector<std::byte> bytes;
    std::size_t position = 0;
  };

  const assets::VirtualFileSystem* files_;
  CodeTrust* trust_;
  std::map<Rml::FileHandle, OpenFile> open_;
  Rml::FileHandle nextHandle_ = 1;
  std::map<std::string, std::size_t> recorded_;
};

// Where a texture's pixels come from: an image the UI generated, or an image asset (ImageReference:
// any extension, and a palette of an indexed image).
class TextureSources {
public:
  explicit TextureSources(const assets::VirtualFileSystem& files) : files_{&files} {}

  void setGenerated(const std::string& name, assets::Image image) {
    generated_[name] = std::move(image);
  }
  void dropGenerated(const std::string& name) { generated_.erase(name); }

  // The pixels of a source as RmlUi names it and the size it is drawn at (loadDrawnImage), or an
  // error.
  [[nodiscard]] core::Result<DrawnImage> load(std::string_view source) const;

  // Whether a source is the disc's pixel art (isPixelArt of its reference); a generated image never
  // is.
  [[nodiscard]] static bool isPixelArt(std::string_view source);

private:
  const assets::VirtualFileSystem* files_;
  std::map<std::string, assets::Image, std::less<>> generated_;
};

} // namespace dwe::ui
