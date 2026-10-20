#include <dwe/script/script_vm.hpp>

#include <sol/sol.hpp>

#include <string>
#include <utility>

namespace dwe::script {

void ScriptVm::bindCommand(std::string_view name, std::function<void(std::string_view)> run) {
    lua()[std::string{name}] = [run = std::move(run)](const std::string& argument) { run(argument); };
}

} // namespace dwe::script
