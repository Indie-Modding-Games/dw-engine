#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/script/script_vm.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dwe::script::testing {

// A call deadline no test reaches unless it means to, even in a sanitized build on a loaded machine: the tests of
// other limits must not stop on time instead.
inline constexpr std::chrono::milliseconds TestDeadline{60'000};

// The engine's limits, with the test deadline for calls and hooks.
inline constexpr ScriptLimits TestLimits{
    .callDeadline = TestDeadline, .hookDeadline = TestDeadline, .hookTimePerFrame = TestDeadline};

// A VM with small limits, so the tests that exhaust them finish quickly.
inline constexpr ScriptLimits SmallLimits{.memoryBytes = std::size_t{16} * 1024 * 1024,
                                          .instructionsPerCall = 1'000'000,
                                          .callDeadline = TestDeadline,
                                          .hookDeadline = TestDeadline,
                                          .hookTimePerFrame = TestDeadline};

// A VM whose calls stop on time only: the instruction budget is out of reach.
inline constexpr ScriptLimits ShortDeadline{.instructionsPerCall = 100'000'000'000,
                                            .callDeadline = std::chrono::milliseconds{200}};

// How late a call of ShortDeadline may stop: twice its deadline, plus room for a machine loaded by parallel builds
// and tests. A C function that the budget did not charge would run for seconds between two checks of the clock; the
// tests that must tell a charge from none use the instruction budget instead (SmallLimits and QuickStop).
inline constexpr std::chrono::milliseconds DeadlineStop =
    (2 * ShortDeadline.callDeadline) + std::chrono::milliseconds{1000};

// Far more than stopping a runaway script takes, even in a sanitized debug build, and far less than it would run.
inline constexpr std::chrono::milliseconds QuickStop{5000};

inline std::unique_ptr<ScriptVm> makeVm(const ScriptLimits& limits = TestLimits) {
    auto vm = ScriptVm::create(limits);
    REQUIRE(vm.has_value());
    return std::move(vm.value());
}

// The lines written to the log since the last clear().
class LogCapture {
public:
    LogCapture() {
        static const bool added = [] {
            core::addLogSink([](core::LogLevel /*level*/, std::string_view message) { lines().emplace_back(message); });
            return true;
        }();
        static_cast<void>(added);
        lines().clear();
    }

    [[nodiscard]] static bool contains(std::string_view text) {
        return std::ranges::any_of(lines(), [text](const std::string& line) { return line.contains(text); });
    }

    [[nodiscard]] static std::size_t count(std::string_view text) {
        return static_cast<std::size_t>(
            std::ranges::count_if(lines(), [text](const std::string& line) { return line.contains(text); }));
    }

private:
    static std::vector<std::string>& lines() {
        static std::vector<std::string> captured;
        return captured;
    }
};

// Packs in memory, mounted as a VFS. Add the files, then mount(). A pack is a mod unless it is given the engine's
// origin, whatever layer it declares.
class MemoryPacks {
public:
    void addPack(std::string_view id, std::string_view manifestExtra = {}, std::string_view layer = "mods",
                 assets::PackOrigin origin = assets::PackOrigin::Mod) {
        files_.addFile(std::filesystem::path{std::string{id}} / "pack.yaml",
                       std::format("id: {}\nversion: 1.0.0\nlayer: {}\n{}", id, layer, manifestExtra));
        locations_.push_back({.fileSystem = &files_, .root = std::string{id}, .fillsGaps = false, .origin = origin});
    }

    // One of the engine's own packs, in the base layer.
    void addEnginePack(std::string_view id) { addPack(id, {}, "base", assets::PackOrigin::Engine); }

    void addFile(std::string_view pack, std::string_view path, std::string_view text) {
        files_.addFile(std::filesystem::path{std::string{pack}} / std::string{path}, text);
    }

    const assets::VirtualFileSystem& mount() {
        auto mounted = assets::VirtualFileSystem::mount(locations_);
        REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
        vfs_ = std::make_unique<assets::VirtualFileSystem>(std::move(*mounted));
        return *vfs_;
    }

    [[nodiscard]] const assets::VirtualFileSystem& files() const { return *vfs_; }

    [[nodiscard]] const assets::PackManifest& manifest(std::string_view id) const {
        const auto pack = std::ranges::find(vfs_->packs(), id,
                                            [](const assets::MountedPack& mounted) { return mounted.manifest.id; });
        REQUIRE(pack != vfs_->packs().end());
        return pack->manifest;
    }

private:
    assets::MemoryFileSystem files_;
    std::vector<assets::PackLocation> locations_;
    std::unique_ptr<assets::VirtualFileSystem> vfs_;
};

inline assets::AssetId assetId(std::string_view text) {
    auto id = assets::AssetId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

// The error of a result that must have failed.
template <typename Result>
std::string errorOf(const Result& result) {
    REQUIRE_FALSE(result.has_value());
    return result.error().message;
}

inline assets::PackManifest packNamed(std::string_view id) {
    assets::PackManifest pack;
    pack.id = id;
    return pack;
}

inline ModEnvironment environmentOf(ScriptVm& vm, std::string_view pack) {
    auto environment = vm.modEnvironment(packNamed(pack));
    REQUIRE_MESSAGE(environment.has_value(), environment.error().message);
    return *environment;
}

// Publishes test.report(value), which collects what scripts report as text.
inline std::vector<std::string>& publishReport(ScriptVm& vm) {
    static std::vector<std::string> reported;
    reported.clear();
    if (!vm.api().isPublished("test", "report")) {
        REQUIRE(vm.api().publish("test", "report", [](const ApiCall& call) -> core::Result<ApiValue> {
            const auto& value = call.arguments().empty() ? ApiValue{} : call.arguments().front();
            if (const auto* text = std::get_if<std::string>(&value)) {
                reported.push_back(*text);
            } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
                reported.push_back(std::to_string(*integer));
            } else if (const auto* flag = std::get_if<bool>(&value)) {
                reported.emplace_back(*flag ? "true" : "false");
            } else {
                reported.emplace_back("nil");
            }
            return ApiValue{};
        }));
    }
    return reported;
}

// Runs a script that the budget must stop, and how long that took.
inline std::chrono::milliseconds stopTime(ScriptVm& vm, std::string_view source) {
    const auto start = std::chrono::steady_clock::now();
    const std::string error = errorOf(vm.runString(source, "test"));
    CHECK_MESSAGE(error.contains("ran too long"), error);
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
}

} // namespace dwe::script::testing
