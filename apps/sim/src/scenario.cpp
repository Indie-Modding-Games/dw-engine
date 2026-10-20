#include <dwe/core/log.hpp>
#include <dwe/sim/messages.hpp>
#include <dwe/sim/scenario.hpp>

#include <utility>

namespace dwe::sim {

core::Result<void> ScenarioRegistry::add(ScenarioInfo info) {
    if (scenarios_.contains(info.name)) {
        return core::fail(messages::formatMessage(messages::RepeatedScenario, info.name));
    }
    std::string name = info.name;
    scenarios_.emplace(std::move(name), std::move(info));
    return {};
}

const ScenarioInfo* ScenarioRegistry::find(std::string_view name) const {
    const auto found = scenarios_.find(name);
    return found == scenarios_.end() ? nullptr : &found->second;
}

ScenarioRegistry& builtInScenarios() {
    static ScenarioRegistry registry;
    return registry;
}

ScenarioRegistration::ScenarioRegistration(ScenarioInfo info) {
    if (auto added = builtInScenarios().add(std::move(info)); !added) {
        core::logError("{}", added.error().message);
    }
}

} // namespace dwe::sim
