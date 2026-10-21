#include <dwe/assets/dev_asset_sources.hpp>
#include <dwe/content/dev_assets.hpp>
#include <dwe/content/game_content.hpp>
#include <dwe/core/log.hpp>
#include <dwe/sim/messages.hpp>
#include <dwe/sim/scenario.hpp>
#include <dwe/sim/sim_options.hpp>
#include <dwe/sim/simulation.hpp>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace dwe;

int runSimulation(std::span<char* const> arguments) {
    const std::vector<std::string_view> words(arguments.begin(), arguments.end());
    auto options = sim::parseSimOptions(words);
    if (!options) {
        core::logError("{}", options.error().message);
        core::logError("{}", sim::messages::formatMessage(sim::messages::UsageLine, sim::Usage));
        return EXIT_FAILURE;
    }
    const sim::ScenarioRegistry& registry = sim::builtInScenarios();
    if (options->list) {
        std::print("{}", sim::describeScenarios(registry));
        return EXIT_SUCCESS;
    }
    assets::DevAssetSources devAssets;
    if (options->devAssets) {
        auto loaded = dwe::content::openDevAssets();
        if (!loaded) {
            core::logError("{}", loaded.error().message);
            return EXIT_FAILURE;
        }
        devAssets = std::move(*loaded);
    }
    auto content = dwe::content::GameContent::mount(options->content, options->mods, devAssets);
    if (!content) {
        core::logError("{}", content.error().message);
        return EXIT_FAILURE;
    }
    auto report = sim::simulate((*content)->files(), registry, *options);
    if (!report) {
        core::logError("{}", report.error().message);
        return EXIT_FAILURE;
    }
    std::print("{}", report->markdown);
    if (options->out) {
        if (auto written = sim::writeReport(*report, *options->out, options->scenario); !written) {
            core::logError("{}", written.error().message);
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char* const> arguments{argv, static_cast<std::size_t>(argc)};
    return runSimulation(arguments.subspan(1));
}
