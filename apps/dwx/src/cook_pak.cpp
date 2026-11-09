#include "cook_pak.hpp"

#include <dwe/assets/content_check.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <format>
#include <print>
#include <string>
#include <system_error>
#include <vector>

#include "dwx.hpp"

namespace dwe::dwx {

namespace {

constexpr std::string_view CookFolderSuffix = ".cook";
constexpr std::string_view PartialSuffix = ".partial";
// The share of the whole job each step ends at, in percent: cooking is most of it (UASTC textures,
// AV1 movies), packing and the engine's check far less.
constexpr int CookEnd = 80;
constexpr int PackEnd = 95;
constexpr int CheckStart = 96;
constexpr int Whole = 100;

// Tells progress the percent of the whole job from done of total of a step that spans [from, to).
class StepProgress {
public:
  explicit StepProgress(const CookPakProgress& progress) : progress_(&progress) {}

  void tell(int percent, std::string_view step) {
    if (!*progress_ || (percent == last_ && step == step_)) {
      return;
    }
    last_ = percent;
    step_ = step;
    (*progress_)(percent, step);
  }

  void tell(std::size_t done, std::size_t total, int from, int to, std::string_view step) {
    const std::size_t share = total == 0 ? 1 : total;
    tell(from + static_cast<int>((std::min(done, share) * static_cast<std::size_t>(to - from)) /
                                 share),
         step);
  }

private:
  const CookPakProgress* progress_;
  int last_ = -1;
  std::string_view step_;
};

// The archive mounted on its own, as the game mounts the base pack: it must give the source pack's
// asset ids and the engine must open every asset.
core::Result<void> checkMounted(const std::filesystem::path& pack,
                                const std::filesystem::path& archive) {
  auto pak = assets::PakFileSystem::open(archive);
  if (!pak) {
    return std::unexpected{pak.error()};
  }
  const std::array locations{assets::PackLocation{
      .fileSystem = pak->get(), .root = archive, .origin = assets::PackOrigin::Engine}};
  auto files = assets::VirtualFileSystem::mount(locations);
  if (!files) {
    return core::fail(std::format("the archive does not mount: {}", files.error().message));
  }
  const auto source = assets::listPackFiles(assets::DiskFileSystem{}, pack);
  if (!source) {
    return std::unexpected{source.error()};
  }
  if (!std::ranges::equal(files->ids(), *source, {}, {}, &assets::PackFile::id)) {
    return core::fail("the archive does not have the same asset ids as the pack");
  }
  const std::vector<assets::ContentProblem> problems = assets::checkContent(*files);
  if (!problems.empty()) {
    std::string list;
    for (const assets::ContentProblem& problem : problems) {
      list += std::format("\n  {}: {}", problem.file, problem.message);
    }
    return core::fail(
        std::format("the engine cannot open {} of its assets:{}", problems.size(), list));
  }
  return {};
}

core::Result<CookPakSummary> cookAndPack(std::span<const Cooker> registry,
                                         const std::filesystem::path& pack,
                                         const std::filesystem::path& archive,
                                         StepProgress& progress) {
  const std::filesystem::path cooked = cookFolderOf(archive);
  const std::filesystem::path partial = partialArchiveOf(archive);
  progress.tell(0, cook_pak_step::Cook);
  auto cook = cookPack(registry, pack, cooked, [&](std::size_t done, std::size_t total) {
    progress.tell(done, total, 0, CookEnd, cook_pak_step::Cook);
  });
  if (!cook) {
    return std::unexpected{cook.error()};
  }
  progress.tell(CookEnd, cook_pak_step::Pack);
  const assets::DiskFileSystem disk;
  auto entries =
      assets::writePak(disk, cooked, partial, [&](std::size_t written, std::size_t total) {
        progress.tell(written, total, CookEnd, PackEnd, cook_pak_step::Pack);
      });
  if (!entries) {
    return std::unexpected{entries.error()};
  }
  progress.tell(CheckStart, cook_pak_step::Check);
  if (auto read = checkArchive(cooked, partial, *entries); !read) {
    return core::fail(std::format("the archive does not read back: {}", read.error().message));
  }
  if (auto opened = checkMounted(pack, partial); !opened) {
    return std::unexpected{opened.error()};
  }
  std::error_code error;
  std::filesystem::rename(partial, archive, error);
  if (error) {
    return core::fail(
        std::format("cannot name the archive {}: {}", assets::pathText(archive), error.message()));
  }
  // The archive is in place; a cooked folder left behind costs only space.
  std::filesystem::remove_all(cooked, error);
  CookPakSummary summary{.cook = *cook, .entries = entries->size(), .archiveBytes = 0};
  summary.archiveBytes = std::filesystem::file_size(archive, error);
  progress.tell(Whole, cook_pak_step::Done);
  return summary;
}

} // namespace

std::filesystem::path cookFolderOf(const std::filesystem::path& archive) {
  std::filesystem::path folder = archive;
  folder += CookFolderSuffix;
  return folder;
}

std::filesystem::path partialArchiveOf(const std::filesystem::path& archive) {
  std::filesystem::path partial = archive;
  partial += PartialSuffix;
  return partial;
}

core::Result<CookPakSummary> cookPakArchive(std::span<const Cooker> registry,
                                            const std::filesystem::path& pack,
                                            const std::filesystem::path& archive,
                                            const CookPakProgress& progress) {
  if (assets::pathText(archive.extension()) != assets::PakExtension) {
    return core::fail(std::format("the archive {} must end in {}", assets::pathText(archive),
                                  assets::PakExtension));
  }
  std::error_code error;
  const std::filesystem::path packPath = std::filesystem::weakly_canonical(pack, error);
  const std::filesystem::path archivePath = std::filesystem::weakly_canonical(archive, error);
  if (error || isWithin(archivePath, packPath)) {
    return core::fail(std::format("the archive {} must be outside the pack {}",
                                  assets::pathText(archive), assets::pathText(pack)));
  }
  if (!archive.parent_path().empty()) {
    std::filesystem::create_directories(archive.parent_path(), error);
  }
  std::filesystem::remove(partialArchiveOf(archive), error);
  StepProgress steps{progress};
  auto summary = cookAndPack(registry, pack, archive, steps);
  if (!summary) {
    std::filesystem::remove(partialArchiveOf(archive), error);
  }
  return summary;
}

int cookPak(const std::filesystem::path& pack, const std::filesystem::path& archive,
            bool progress) {
  const CookPakProgress print = [](int percent, std::string_view step) {
    std::println(stderr, "progress {} {}", percent, step);
    std::fflush(stderr);
  };
  const auto summary =
      cookPakArchive(cookers(), pack, archive, progress ? print : CookPakProgress{});
  if (!summary) {
    std::println(stderr, "dwx: {}", summary.error().message);
    return ExitFailed;
  }
  std::println("Archive: {}", assets::pathText(archive));
  std::println("Assets: {} ({} cooked, {} copied, {} passed through as lookup data, {} shared)",
               summary->cook.assets, summary->cook.cooked, summary->cook.copied,
               summary->cook.passed, summary->cook.shared);
  std::println("Bytes: {} of source, {} in the archive ({} entries)", summary->cook.sourceBytes,
               summary->archiveBytes, summary->entries);
  std::println("Check: every entry reads back and the engine opens every asset");
  return ExitOk;
}

} // namespace dwe::dwx
