#include <dwe/core/message.hpp>
#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/exe/executable_layout.hpp>

#include <cstddef>
#include <cstdio>
#include <print>
#include <string>

#include "base_pack.hpp"
#include "disc_description.hpp"
#include "dwx.hpp"
#include "messages.hpp"
#include "upscale.hpp"

namespace dwe::dwx {

namespace {

double percent(std::uint64_t part, std::uint64_t whole) {
  return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

// Where each step of an extraction starts, in percent: reading the files takes most of it.
constexpr int ExtractStart = 2;
constexpr int WriteStart = 80;
constexpr int CheckStart = 90;
constexpr int UpscaleStart = 95;
constexpr int Finished = 100;

void tell(const ExtractProgress& progress, int at, std::string_view step) {
  if (progress) {
    progress(at, step);
  }
}

std::string_view kindName(DiscKind kind) {
  switch (kind) {
  case DiscKind::Supported:
    return "supported";
  case DiscKind::OtherVersion:
    return "other-version";
  case DiscKind::Unknown:
    break;
  }
  return "unknown";
}

} // namespace

core::Result<DiscIdentity> identifyDisc(const std::filesystem::path& path) {
  auto image = formats::disc::DiscImage::open(path);
  if (!image) {
    return std::unexpected{image.error()};
  }
  const auto volume = formats::disc::readIsoVolume(*image);
  if (!volume) {
    return std::unexpected{volume.error()};
  }
  const DiscDescription description = describeDisc(*image, *volume);
  DiscIdentity identity{.kind = DiscKind::Unknown, .version = description.info.version};
  if (!identity.version.empty()) {
    identity.kind = formats::exe::findExecutableLayout(identity.version) ? DiscKind::Supported
                                                                         : DiscKind::OtherVersion;
  }
  return identity;
}

int identify(const std::filesystem::path& path) {
  const auto identity = identifyDisc(path);
  if (!identity) {
    std::println(stderr, "dwx: {}", identity.error().message);
    return ExitFailed;
  }
  std::println("{} {}", kindName(identity->kind),
               identity->version.empty() ? "-" : identity->version);
  switch (identity->kind) {
  case DiscKind::Supported:
    return ExitOk;
  case DiscKind::OtherVersion:
    return ExitUnsupported;
  case DiscKind::Unknown:
    break;
  }
  return ExitUnknown;
}

core::Result<void> extractBasePack(const std::filesystem::path& path,
                                   const std::filesystem::path& out, bool force, std::FILE* report,
                                   const ExtractProgress& progress) {
  tell(progress, 0, ExtractStep::Read);
  auto image = formats::disc::DiscImage::open(path);
  if (!image) {
    return std::unexpected{image.error()};
  }
  const auto volume = formats::disc::readIsoVolume(*image);
  if (!volume) {
    return std::unexpected{volume.error()};
  }
  const DiscDescription description = describeDisc(*image, *volume);
  for (const auto& warning : description.warnings) {
    std::println(stderr, "dwx: warning: {}", warning);
  }
  const DiscInfo& info = description.info;
  std::println(report, "Disc: {}, {} files, executable {} ({})", info.volumeId,
               volume->files.size(), info.executablePath,
               info.version.empty() ? "unknown version" : info.version);

  if (description.tables) {
    std::println(report, "Tables: {} Digimon, {} maps; assets take their names",
                 description.tables->ids.digimon.size(), description.tables->ids.maps.size());
  }

  const Disc disc{*image, *volume, info, description.tables ? &*description.tables : nullptr};
  tell(progress, ExtractStart, ExtractStep::Extract);
  const auto run =
      runExtractors(disc, extractors(), [&progress](std::size_t done, std::size_t total) {
        tell(progress, ExtractStart + static_cast<int>((WriteStart - ExtractStart) * done / total),
             ExtractStep::Extract);
      });
  if (!run) {
    return std::unexpected{run.error()};
  }
  tell(progress, WriteStart, ExtractStep::Write);
  if (auto prepared = prepareOutputFolder(out, force); !prepared) {
    return prepared;
  }
  if (auto written = writeBasePack(out, *run, info); !written) {
    return written;
  }

  const CoverageSummary summary = summarize(*run);
  std::println(report, "Base pack: {}", out.string());
  std::println(report, "Files: {} total, {} extracted, {} partial, {} pending", summary.files,
               summary.extracted, summary.partial, summary.pending);
  std::println(report, "Bytes covered: {} of {} ({:.1f} %)", summary.bytesCovered, summary.bytes,
               percent(summary.bytesCovered, summary.bytes));
  std::println(report, "Outputs: {} ({} repeat another output byte for byte)", summary.outputs,
               summary.duplicates);

  tell(progress, CheckStart, ExtractStep::Check);
  const auto assets = checkBasePack(out, *run);
  if (!assets) {
    return core::fail(core::formatMessage(messages::EngineCheckFailed, assets.error().message));
  }
  std::println(
      report,
      "Engine check: the pack mounts with {} assets, every output resolves and every asset opens",
      *assets);
  tell(progress, UpscaleStart, ExtractStep::Upscale);
  if (auto upscaled = runBundledRecipes(out, report); !upscaled) {
    return upscaled;
  }
  tell(progress, Finished, ExtractStep::Done);
  return {};
}

int extractDisc(const std::filesystem::path& path, const std::filesystem::path& out, bool force,
                bool progress) {
  // One line per percent or step, flushed at once: the game reads them while the extraction runs.
  int lastPercent = -1;
  std::string lastStep;
  const ExtractProgress print = [&](int at, std::string_view step) {
    if (at != lastPercent || step != lastStep) {
      lastPercent = at;
      lastStep = step;
      std::println(stderr, "progress {} {}", at, step);
      std::fflush(stderr);
    }
  };
  if (auto extracted =
          extractBasePack(path, out, force, stdout, progress ? print : ExtractProgress{});
      !extracted) {
    std::println(stderr, "dwx: {}", extracted.error().message);
    return ExitFailed;
  }
  return ExitOk;
}

} // namespace dwe::dwx
