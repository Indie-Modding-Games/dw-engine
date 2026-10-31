#include "bug_reporter.hpp"

#include <dwe/core/log.hpp>
#include <dwe/platform/platform.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "bug_report.hpp"
#include "common.hpp"
#include "play_session.hpp"
#include "screenshot.hpp"

namespace dwe::app {
namespace {

constexpr std::string_view ReportsFolderName = "reports";
// How long the toast stays on screen, in seconds.
constexpr double ToastSeconds = 2.5;
// Reports made in the same second on the same map get a number: -2, -3...
constexpr int MaxSameSecondReports = 100;

// The source the game was built from: the commit a package was built from (DWE_BUILD_COMMIT), or
// as git describes it: the commit, and -dirty with changes not committed. The source folder is the
// build's; "unknown" without git or without the folder.
std::string sourceCommit() {
#ifdef DWE_BUILD_COMMIT
  return DWE_BUILD_COMMIT;
#else
#ifdef _WIN32
  constexpr std::string_view NoErrors = "2>NUL";
  const auto open = [](const char* command) { return _popen(command, "r"); };
  const auto close = [](FILE* pipe) { return _pclose(pipe); };
#else
  constexpr std::string_view NoErrors = "2>/dev/null";
  const auto open = [](const char* command) { return popen(command, "r"); };
  const auto close = [](FILE* pipe) { return pclose(pipe); };
#endif
  const std::string command =
      std::format(R"(git -C "{}" describe --always --dirty {})", DWE_SOURCE_DIR, NoErrors);
  FILE* pipe = open(command.c_str());
  if (pipe == nullptr) {
    return "unknown";
  }
  std::array<char, 128> buffer{};
  std::string output;
  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }
  const int status = close(pipe);
  while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
    output.pop_back();
  }
  return status == 0 && !output.empty() ? output : "unknown";
#endif
}

std::tm localNow() {
  const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  return local;
}

bool writeText(const std::filesystem::path& path, std::string_view text) {
  std::ofstream file{path, std::ios::binary};
  if (!file.write(text.data(), static_cast<std::streamsize>(text.size()))) {
    core::logWarning("bug report: cannot write {}", path.string());
    return false;
  }
  return true;
}

// A folder no report has yet: the name, or the name with -2, -3...
std::filesystem::path freshFolder(const std::filesystem::path& root, const std::string& name) {
  std::filesystem::path folder = root / name;
  std::error_code error;
  for (int number = 2; std::filesystem::exists(folder, error) && number < MaxSameSecondReports;
       ++number) {
    folder = root / std::format("{}-{}", name, number);
  }
  return folder;
}

} // namespace

std::optional<std::filesystem::path> bugReportsFolder(const Options& options) {
  if (options.reports) {
    return options.reports;
  }
  auto data = platform::userDataFolder(UserDataOrganization, UserDataApplication);
  if (!data) {
    core::logWarning("bug report: no folder for the reports: {}", data.error().message);
    return std::nullopt;
  }
  return *data / ReportsFolderName;
}

BugReporter::BugReporter(std::optional<std::filesystem::path> folder,
                         std::shared_ptr<core::LogRing> log)
    : root_{std::move(folder)}, log_{std::move(log)} {}

void BugReporter::report(const PlaySession& play, render::Renderer& renderer) {
  if (!root_) {
    core::logWarning("bug report: not written, there is no folder for the reports");
    return;
  }
  const std::tm local = localNow();
  BugReportState state = play.reportState();
  state.commit = sourceCommit();
  state.gameVersion = DWE_GAME_VERSION;
  state.created =
      std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", local.tm_year + 1900, local.tm_mon + 1,
                  local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
  const std::filesystem::path folder = freshFolder(*root_, bugReportFolderName(local, state.map));
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  if (error) {
    core::logWarning("bug report: cannot make {}: {}", folder.string(), error.message());
    return;
  }

  const std::optional<std::string> save = play.reportSave(state.save);
  if (save) {
    writeText(folder / ReportSaveFile, *save);
  }
  if (auto text = bugReportStateYaml(state)) {
    writeText(folder / ReportStateFile, *text);
  } else {
    core::logWarning("bug report: {}: {}", ReportStateFile, text.error().message);
  }
  if (auto text = bugReportRouteYaml(state.map, save.has_value())) {
    writeText(folder / ReportRouteFile, *text);
  } else {
    core::logWarning("bug report: {}: {}", ReportRouteFile, text.error().message);
  }
  std::string log;
  if (log_) {
    for (const std::string& line : log_->lines()) {
      log += line;
      log += '\n';
    }
  }
  writeText(folder / ReportLogFile, log);

  shot_ = std::make_shared<Shot>(Shot::Waiting);
  renderer.captureNextFrame([shot = shot_, path = folder / ReportScreenshotFile](
                                core::Result<rhi::CapturedImage> captured) {
    auto written = captured ? writeScreenshot(std::move(*captured), path)
                            : core::Result<void>{std::unexpected{captured.error()}};
    if (!written) {
      core::logWarning("bug report: no screenshot: {}", written.error().message);
    }
    *shot = written ? Shot::Written : Shot::Failed;
  });
  core::logInfo("bug report: saved in {} (save: {})", folder.string(),
                save ? (state.save.kind == ReportSaveKind::Now ? "now" : "field snapshot")
                     : "none");
}

void BugReporter::update(ui::Ui& ui, double frameSeconds) {
  // The toast shows once the screenshot is taken, so it is not in it.
  if (shot_ && *shot_ != Shot::Waiting) {
    shot_.reset();
    toastSeconds_ = ToastSeconds;
    ui.show(BugReportDocument);
    return;
  }
  if (toastSeconds_ > 0.0) {
    toastSeconds_ -= frameSeconds;
    if (toastSeconds_ <= 0.0) {
      ui.hide(BugReportDocument);
    }
  }
}

} // namespace dwe::app
