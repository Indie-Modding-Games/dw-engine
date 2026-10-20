#include <dwe/localization/catalog.hpp>
#include <dwe/script/script_vm.hpp>

#include <sol/sol.hpp>

#include <string>
#include <vector>

namespace dwe::script {
namespace {

// The same names the UI's documents have (docs/architecture/localizacion.md).
constexpr std::string_view TextFunction = "t";
constexpr std::string_view TextsTable = "texts";

// The placeholders' values a script passes: a table of names to texts or numbers. Other entries are left out.
std::vector<localization::TextArgument> argumentsOf(const sol::optional<sol::table>& table) {
    std::vector<localization::TextArgument> arguments;
    if (!table) {
        return arguments;
    }
    for (const auto& [name, value] : *table) {
        if (name.get_type() != sol::type::string) {
            continue;
        }
        if (value.get_type() == sol::type::string) {
            arguments.push_back({.name = name.as<std::string_view>(), .value = value.as<std::string>()});
        } else if (value.get_type() == sol::type::number) {
            arguments.push_back(
                {.name = name.as<std::string_view>(), .value = localization::numberText(value.as<double>())});
        }
    }
    return arguments;
}

} // namespace

void ScriptVm::bindTexts(const localization::Catalog& catalog) {
    const localization::Catalog* texts = &catalog;
    lua().set_function(TextFunction, [texts](const std::string& key, const sol::optional<sol::table>& arguments) {
        return texts->text(key, argumentsOf(arguments));
    });
    sol::table api = lua().create_named_table(TextsTable);
    api["language"] = [texts]() { return texts->language(); };
}

} // namespace dwe::script
