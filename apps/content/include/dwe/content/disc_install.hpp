#pragma once

#include <dwe/core/error.hpp>
#include <dwe/platform/process.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The player's own content: the game's first start reads the player's disc image with dwx into the
// user's data folder, and every later start mounts that folder (docs/guides/beta-linux.md).
namespace dwe::content {

// The folder of the user's data that holds the content extracted from the player's disc.
inline constexpr std::string_view InstalledContentFolderName = "content";
// Where an extraction writes before its packs move into the installed content folder, so a stopped
// one never leaves half a pack where the game mounts it.
inline constexpr std::string_view PartialContentFolderName = "content.partial";
// The base pack's folder in a content folder, as dwx extract writes it.
inline constexpr std::string_view BasePackFolderName = "base";

// Whether a content folder has the base pack that dwx extract writes (base/pack.yaml).
[[nodiscard]] bool hasBasePack(const std::filesystem::path& content);

// The content folder the game mounts when --content is not given: a ./content with a base pack, as
// dwx extract writes it by default (development), else the installed one in the user's data
// folder; ./content when there is neither.
[[nodiscard]] std::filesystem::path
defaultContentFolder(const std::optional<std::filesystem::path>& userData);

// Where the team's packs (content/packs: the game pack and the translations) are looked for, in
// order: packs/ next to the executable's folder (a package: bin/dwe-game and packs/), then the
// source tree's.
[[nodiscard]] std::vector<std::filesystem::path>
teamPacksCandidates(const std::optional<std::filesystem::path>& executableFolder);

// The first candidate that has a game pack; the source tree's when none has.
[[nodiscard]] std::filesystem::path teamPacksFolder();

// The files a CUE sheet names (FILE "<name>" ...) that are not next to it. Nothing for an image
// that is not a CUE sheet or cannot be read: dwx says what is wrong with those.
[[nodiscard]] std::vector<std::string> missingCueFiles(const std::filesystem::path& image);

// A line of dwx extract --progress: "progress <percent> <step>".
struct ExtractProgressLine {
  int percent = 0;
  std::string step;
};
[[nodiscard]] std::optional<ExtractProgressLine> parseProgressLine(std::string_view line);

// What went wrong while installing a disc, for the player's message.
enum class InstallProblem : std::uint8_t {
  None,
  // The image, or a file its CUE sheet names, is not there.
  MissingFile,
  // dwx cannot read it as a disc image.
  CannotRead,
  // It is a disc image, but of no known release of the game.
  NotTheGame,
  // A known release of another region, whose tables the extractor does not know.
  OtherRegion,
  // The extraction itself failed (or the packs could not be moved into place).
  ExtractFailed,
  // dwx is not next to the game, or there is no user data folder.
  NoTool,
};

// Installs the player's disc into the installed content folder: dwx identify, then dwx extract
// --progress into the partial folder, then its packs move into place. update() moves it on without
// ever waiting, so a screen can show its progress every frame.
class DiscInstall {
public:
  enum class Stage : std::uint8_t { Identifying, Extracting, Finished, Failed };

  // Starts with the image. userData is the user's data folder; dwx is the tool to run.
  static DiscInstall start(const std::filesystem::path& image,
                           const std::filesystem::path& userData, const std::filesystem::path& dwx);
  // An install that failed before it started (no dwx, no user data folder).
  static DiscInstall failed(InstallProblem problem, std::string detail);

  DiscInstall(DiscInstall&&) noexcept = default;
  DiscInstall& operator=(DiscInstall&&) noexcept = default;
  DiscInstall(const DiscInstall&) = delete;
  DiscInstall& operator=(const DiscInstall&) = delete;
  ~DiscInstall();

  // Reads what dwx said and starts the next step when one ends.
  void update();

  // Stops it and removes what it wrote.
  void cancel();

  [[nodiscard]] Stage stage() const { return stage_; }
  [[nodiscard]] InstallProblem problem() const { return problem_; }
  // dwx's own words for a failure, or the missing file's name; empty when there is nothing to add.
  [[nodiscard]] const std::string& detail() const { return detail_; }
  // The release's version that dwx identify gave ("us", "eu"...), once known.
  [[nodiscard]] const std::string& version() const { return version_; }
  [[nodiscard]] int percent() const { return percent_; }
  // The step of dwx extract --progress (read, extract, write, check, upscale, done).
  [[nodiscard]] const std::string& step() const { return step_; }
  // The content folder the packs went to, once finished.
  [[nodiscard]] const std::filesystem::path& contentFolder() const { return content_; }

private:
  DiscInstall() = default;

  void fail(InstallProblem problem, std::string detail);
  void startExtraction();
  void finishIdentify(int exitCode);
  void finishExtraction(int exitCode);
  void removePartial();

  std::filesystem::path image_;
  std::filesystem::path dwx_;
  std::filesystem::path content_;
  std::filesystem::path partial_;
  std::optional<platform::ChildProcess> process_;
  std::vector<std::string> output_;
  Stage stage_ = Stage::Failed;
  InstallProblem problem_ = InstallProblem::None;
  std::string detail_;
  std::string version_;
  int percent_ = 0;
  std::string step_;
};

// What dwx said last about a failure: its last line that starts with "dwx: ", without that, or
// nothing.
[[nodiscard]] std::string dwxError(std::span<const std::string> lines);

} // namespace dwe::content
