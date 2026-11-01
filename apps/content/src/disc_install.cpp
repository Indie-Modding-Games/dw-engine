#include <dwe/assets/extractor_version.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/content/disc_install.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/platform/process.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <system_error>
#include <utility>

namespace dwe::content {
namespace {

// What dwx identify and extract are run with.
constexpr std::string_view IdentifyCommand = "identify";
constexpr std::string_view ExtractCommand = "extract";
constexpr std::string_view OutOption = "--out";
constexpr std::string_view ProgressOption = "--progress";
constexpr std::string_view ProgressPrefix = "progress ";
constexpr std::string_view DwxErrorPrefix = "dwx: ";
// dwx identify's exit codes (dwx.hpp): known and supported, unknown, unreadable, another region's.
constexpr int IdentifySupported = 0;
constexpr int IdentifyUnknown = 1;
constexpr int IdentifyOtherVersion = 3;
// The packs a content folder may hold after an extraction: the base pack and the remastered UI the
// bundled recipe grows next to it.
constexpr std::string_view TeamPacksFolderName = "packs";
constexpr std::string_view GamePackFolderName = "game";
// The keys of the install record.
constexpr std::string_view RecordVersionKey = "extractor_version";
constexpr std::string_view RecordImageKey = "disc_image";
constexpr std::string_view RecordComment =
    "# Written by the game when it read the player's disc; another extractor version reads it "
    "again.\n";

std::string trimmed(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return std::string{text};
}

std::string_view problemName(InstallProblem problem) {
  switch (problem) {
  case InstallProblem::MissingFile:
    return "a file is missing";
  case InstallProblem::CannotRead:
    return "not a readable disc image";
  case InstallProblem::NotTheGame:
    return "not a known release of the game";
  case InstallProblem::OtherRegion:
    return "another region's release";
  case InstallProblem::ExtractFailed:
    return "the extraction failed";
  case InstallProblem::NoTool:
    return "dwx cannot run";
  case InstallProblem::None:
    break;
  }
  return "stopped";
}

bool isFolder(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_directory(path, error);
}

} // namespace

bool hasBasePack(const std::filesystem::path& content) {
  std::error_code error;
  return std::filesystem::is_regular_file(content / BasePackFolderName / "pack.yaml", error);
}

std::filesystem::path defaultContentFolder(const std::optional<std::filesystem::path>& userData) {
  const std::filesystem::path local{InstalledContentFolderName};
  if (hasBasePack(local) || !userData) {
    return local;
  }
  return *userData / InstalledContentFolderName;
}

std::optional<InstallRecord> readInstallRecord(const std::filesystem::path& content) {
  std::ifstream stream{content / InstallRecordFileName, std::ios::binary};
  if (!stream) {
    return std::nullopt;
  }
  const std::string text{std::istreambuf_iterator<char>{stream}, {}};
  const auto tree = core::parseYaml(text);
  if (!tree || !tree->isMapping()) {
    return std::nullopt;
  }
  const core::YamlNode* version = tree->find(RecordVersionKey);
  if (version == nullptr || !version->isScalar()) {
    return std::nullopt;
  }
  InstallRecord record;
  const std::string& digits = version->text();
  const auto [end, error] =
      std::from_chars(digits.data(), digits.data() + digits.size(), record.extractorVersion);
  if (error != std::errc{} || end != digits.data() + digits.size()) {
    return std::nullopt;
  }
  if (const core::YamlNode* image = tree->find(RecordImageKey);
      image != nullptr && image->isScalar()) {
    const std::string& path = image->text();
    record.image = std::filesystem::path{std::u8string{path.begin(), path.end()}};
  }
  return record;
}

core::Result<void> writeInstallRecord(const std::filesystem::path& content,
                                      const InstallRecord& record) {
  core::YamlNode root = core::YamlNode::mapping();
  root.set(std::string{RecordVersionKey},
           core::YamlNode::scalar(std::to_string(record.extractorVersion)));
  root.set(
      std::string{RecordImageKey},
      core::YamlNode::scalar(assets::pathText(record.image), core::YamlNode::ScalarStyle::Quoted));
  auto yaml = core::emitYaml(root);
  if (!yaml) {
    return std::unexpected{yaml.error()};
  }
  const std::filesystem::path file = content / InstallRecordFileName;
  std::ofstream stream{file, std::ios::binary | std::ios::trunc};
  stream << RecordComment << *yaml;
  if (!stream.flush()) {
    return core::fail(std::format("cannot write {}", assets::pathText(file)));
  }
  return {};
}

bool installIsOutdated(const std::filesystem::path& content) {
  if (!hasBasePack(content)) {
    return false;
  }
  const auto record = readInstallRecord(content);
  return !record || record->extractorVersion != assets::ExtractorVersion;
}

std::vector<std::filesystem::path>
teamPacksCandidates(const std::optional<std::filesystem::path>& executableFolder) {
  std::vector<std::filesystem::path> candidates;
  if (executableFolder) {
    candidates.push_back(executableFolder->parent_path() / TeamPacksFolderName);
  }
#ifdef DWE_TEAM_PACKS
  candidates.emplace_back(DWE_TEAM_PACKS);
#endif
  return candidates;
}

std::filesystem::path teamPacksFolder() {
  const auto folder = platform::executableFolder();
  const auto candidates = teamPacksCandidates(folder ? std::optional{*folder} : std::nullopt);
  for (const std::filesystem::path& candidate : candidates) {
    if (isFolder(candidate / GamePackFolderName)) {
      return candidate;
    }
  }
  return candidates.empty() ? std::filesystem::path{TeamPacksFolderName} : candidates.back();
}

std::vector<std::string> missingCueFiles(const std::filesystem::path& image) {
  std::string extension = image.extension().string();
  std::ranges::transform(extension, extension.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (extension != ".cue") {
    return {};
  }
  std::ifstream sheet{image};
  std::vector<std::string> missing;
  for (std::string line; std::getline(sheet, line);) {
    const std::string text = trimmed(line);
    if (!text.starts_with("FILE ")) {
      continue;
    }
    // FILE "<name>" BINARY, or FILE <name> BINARY without quotes.
    std::string name;
    if (const auto open = text.find('"'); open != std::string::npos) {
      const auto close = text.find('"', open + 1);
      name =
          text.substr(open + 1, close == std::string::npos ? std::string::npos : close - open - 1);
    } else {
      const std::string rest = text.substr(5);
      name = rest.substr(0, rest.find(' '));
    }
    std::error_code error;
    if (!name.empty() && !std::filesystem::exists(image.parent_path() / name, error)) {
      missing.push_back(name);
    }
  }
  return missing;
}

std::optional<ExtractProgressLine> parseProgressLine(std::string_view line) {
  if (!line.starts_with(ProgressPrefix)) {
    return std::nullopt;
  }
  line.remove_prefix(ProgressPrefix.size());
  ExtractProgressLine progress;
  const auto [end, error] =
      std::from_chars(line.data(), line.data() + line.size(), progress.percent);
  if (error != std::errc{} || end == line.data() + line.size() || *end != ' ' ||
      progress.percent < 0 || progress.percent > 100) {
    return std::nullopt;
  }
  progress.step = trimmed(std::string_view{end + 1, line.data() + line.size()});
  if (progress.step.empty()) {
    return std::nullopt;
  }
  return progress;
}

std::string dwxError(std::span<const std::string> lines) {
  for (const std::string& line : std::views::reverse(lines)) {
    if (line.starts_with(DwxErrorPrefix)) {
      return line.substr(DwxErrorPrefix.size());
    }
  }
  return {};
}

DiscInstall DiscInstall::start(const std::filesystem::path& image,
                               const std::filesystem::path& userData,
                               const std::filesystem::path& dwx) {
  DiscInstall install;
  install.image_ = image;
  install.dwx_ = dwx;
  install.content_ = userData / InstalledContentFolderName;
  install.partial_ = userData / PartialContentFolderName;
  install.stage_ = Stage::Identifying;
  std::error_code error;
  if (!std::filesystem::is_regular_file(image, error)) {
    install.fail(InstallProblem::MissingFile, assets::pathText(image));
    return install;
  }
  if (const auto missing = missingCueFiles(image); !missing.empty()) {
    install.fail(InstallProblem::MissingFile, missing.front());
    return install;
  }
  core::logInfo("first run: identifying {}", assets::pathText(image));
  const std::array<std::string, 3> command{assets::pathText(dwx), std::string{IdentifyCommand},
                                           assets::pathText(image)};
  auto process = platform::ChildProcess::start(command);
  if (!process) {
    install.fail(InstallProblem::NoTool, process.error().message);
    return install;
  }
  install.process_ = std::move(*process);
  return install;
}

DiscInstall DiscInstall::failed(InstallProblem problem, std::string detail) {
  DiscInstall install;
  install.fail(problem, std::move(detail));
  return install;
}

DiscInstall::~DiscInstall() {
  if (stage_ == Stage::Identifying || stage_ == Stage::Extracting) {
    cancel();
  }
}

void DiscInstall::fail(InstallProblem problem, std::string detail) {
  stage_ = Stage::Failed;
  problem_ = problem;
  detail_ = std::move(detail);
  core::logWarning("first run: the disc was not installed: {}{}", problemName(problem),
                   detail_.empty() ? std::string{} : std::format(" ({})", detail_));
}

void DiscInstall::update() {
  if (!process_) {
    return;
  }
  for (std::string& line : process_->readLines()) {
    core::logInfo("dwx: {}", line);
    if (const auto progress = parseProgressLine(line)) {
      percent_ = progress->percent;
      step_ = progress->step;
    }
    output_.push_back(std::move(line));
  }
  const std::optional<int> code = process_->exitCode();
  if (!code) {
    return;
  }
  process_.reset();
  if (stage_ == Stage::Identifying) {
    finishIdentify(*code);
  } else if (stage_ == Stage::Extracting) {
    finishExtraction(*code);
  }
}

void DiscInstall::finishIdentify(int exitCode) {
  // "supported us", "other-version eu", "unknown -".
  std::string said;
  for (const std::string& line : output_) {
    if (line.starts_with("supported ") || line.starts_with("other-version ") ||
        line.starts_with("unknown ")) {
      said = line;
    }
  }
  if (const auto space = said.find(' '); space != std::string::npos) {
    version_ = said.substr(space + 1);
  }
  switch (exitCode) {
  case IdentifySupported:
    startExtraction();
    return;
  case IdentifyUnknown:
    fail(InstallProblem::NotTheGame, {});
    return;
  case IdentifyOtherVersion:
    fail(InstallProblem::OtherRegion, version_);
    return;
  default:
    fail(InstallProblem::CannotRead, dwxError(output_));
    return;
  }
}

void DiscInstall::startExtraction() {
  output_.clear();
  removePartial();
  stage_ = Stage::Extracting;
  percent_ = 0;
  step_ = "read";
  core::logInfo("first run: extracting {} into {}", assets::pathText(image_),
                assets::pathText(partial_));
  const std::array<std::string, 6> command{
      assets::pathText(dwx_),      std::string{ExtractCommand},
      std::string{OutOption},      assets::pathText(partial_ / BasePackFolderName),
      std::string{ProgressOption}, assets::pathText(image_)};
  auto process = platform::ChildProcess::start(command);
  if (!process) {
    fail(InstallProblem::NoTool, process.error().message);
    return;
  }
  process_ = std::move(*process);
}

void DiscInstall::finishExtraction(int exitCode) {
  if (exitCode != 0) {
    removePartial();
    fail(InstallProblem::ExtractFailed, dwxError(output_));
    return;
  }
  // Each pack the extraction wrote replaces the installed one of the same name; anything else in
  // the installed folder stays.
  std::error_code error;
  std::filesystem::create_directories(content_, error);
  if (error) {
    removePartial();
    fail(InstallProblem::ExtractFailed, error.message());
    return;
  }
  for (const auto& entry : std::filesystem::directory_iterator{partial_, error}) {
    const std::filesystem::path target = content_ / entry.path().filename();
    std::error_code removed;
    std::filesystem::remove_all(target, removed);
    std::error_code moved;
    std::filesystem::rename(entry.path(), target, moved);
    if (moved) {
      removePartial();
      fail(InstallProblem::ExtractFailed,
           std::format("{}: {}", assets::pathText(target), moved.message()));
      return;
    }
  }
  removePartial();
  if (!hasBasePack(content_)) {
    fail(InstallProblem::ExtractFailed, dwxError(output_));
    return;
  }
  // Without its record the next start would read the disc again: a failure here is only logged.
  std::error_code absolute;
  std::filesystem::path image = std::filesystem::absolute(image_, absolute);
  if (absolute) {
    image = image_;
  }
  if (auto recorded = writeInstallRecord(
          content_, {.extractorVersion = assets::ExtractorVersion, .image = image});
      !recorded) {
    core::logWarning("first run: {}", recorded.error().message);
  }
  percent_ = 100;
  step_ = "done";
  stage_ = Stage::Finished;
  core::logInfo("first run: the disc is installed in {}", assets::pathText(content_));
}

void DiscInstall::cancel() {
  if (process_) {
    process_->kill();
    process_.reset();
  }
  if (stage_ == Stage::Extracting) {
    removePartial();
  }
  if (stage_ == Stage::Identifying || stage_ == Stage::Extracting) {
    stage_ = Stage::Failed;
    problem_ = InstallProblem::None;
  }
}

void DiscInstall::removePartial() {
  if (partial_.empty()) {
    return;
  }
  std::error_code error;
  std::filesystem::remove_all(partial_, error);
}

} // namespace dwe::content
