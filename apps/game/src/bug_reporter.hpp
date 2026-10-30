#pragma once

#include <dwe/core/log_ring.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/ui/ui.hpp>

#include <filesystem>
#include <memory>
#include <optional>

#include "options.hpp"

namespace dwe::app {

class PlaySession;

// The UI document of the toast that says a report was saved.
inline constexpr std::string_view BugReportDocument = "bug_report";

// The folder of the bug reports: --reports, or `reports` in the user's data folder; nothing when
// there is no user data folder.
[[nodiscard]] std::optional<std::filesystem::path> bugReportsFolder(const Options& options);

// Writes bug reports (the report_bug action, F12) into <reports>/<YYYYMMDD-HHMMSS>-<map>/: the
// save, state.yaml, the log's tail, route.yaml and the screenshot of the next frame; then shows a
// short toast and logs the folder (docs/guides/prueba-vertical.md, "Reportar un bug").
class BugReporter {
public:
  // `log` keeps the log's last lines (core::keepLogTail).
  BugReporter(std::optional<std::filesystem::path> folder, std::shared_ptr<core::LogRing> log);

  // Writes a report of the game being played. Its screenshot is the next frame drawn.
  void report(const PlaySession& play, render::Renderer& renderer);

  // Once per frame: shows the toast once the screenshot is written, and hides it after a while.
  void update(ui::Ui& ui, double frameSeconds);

private:
  // The screenshot's state, written by the renderer's capture.
  enum class Shot : std::uint8_t { Waiting, Written, Failed };

  std::optional<std::filesystem::path> root_;
  std::shared_ptr<core::LogRing> log_;
  std::shared_ptr<Shot> shot_;
  double toastSeconds_ = 0.0;
};

} // namespace dwe::app
