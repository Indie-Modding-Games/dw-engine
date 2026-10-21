#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/evolution/messages.hpp>
#include <dwe/game/evolution/state.hpp>
#include <dwe/game/evolution/tables.hpp>

#include <string>
#include <utility>

namespace dwe::game::evolution {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view RaisedKey = "raised";

Species readForm(YamlReader& reader, const YamlNode& node, const EvolutionTables& tables) {
    const std::string id = reader.text(node);
    const auto species = tables.find(id);
    if (reader.ok() && !species) {
        reader.fail(node, messages::formatMessage(messages::UnknownForm, messages::StateText, id));
    }
    return species.value_or(0);
}

} // namespace

void recordEvolution(EvolutionState& state, const EvolutionResult& result) {
    state.raised.insert(result.target);
    state.evolutionItemUsed = state.evolutionItemUsed || result.evolutionItemUsed;
}

core::Result<YamlNode> toYaml(const EvolutionState& state, const EvolutionTables& tables) {
    YamlNode node = YamlNode::mapping();
    YamlNode raised = YamlNode::sequence();
    for (const Species species : state.raised) {
        const std::string_view id = tables.idOf(species);
        if (id.empty()) {
            return core::fail(messages::formatMessage(messages::UnknownForm, messages::StateText, species));
        }
        raised.append(YamlNode::scalar(std::string{id}));
    }
    node.set(std::string{RaisedKey}, std::move(raised));
    return node;
}

core::Result<EvolutionState> fromYaml(const YamlNode& node, const EvolutionTables& tables) {
    YamlReader reader;
    if (!reader.expectKeys(node, messages::StateText, {RaisedKey})) {
        return std::unexpected{reader.error(messages::StateText)};
    }
    EvolutionState state;
    for (const YamlNode& item : reader.sequence(reader.field(node, RaisedKey), RaisedKey)) {
        state.raised.insert(readForm(reader, item, tables));
    }
    return reader.finish(std::move(state), messages::StateText);
}

} // namespace dwe::game::evolution
