#include <dwe/test_support/user_assets.hpp>

#include <cstdlib>
#include <string>

namespace dwe::test_support {
namespace {

std::optional<std::filesystem::path> pathFromVariable(std::string_view variable) {
    const char* value = std::getenv(std::string{variable}.c_str());
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::filesystem::path{value};
}

} // namespace

std::optional<std::filesystem::path> userDisc() {
    return pathFromVariable(DiscVariable);
}

std::optional<std::filesystem::path> userContent() {
    return pathFromVariable(ContentVariable);
}

std::optional<std::filesystem::path> userDecomp() {
    return pathFromVariable(DecompVariable);
}

} // namespace dwe::test_support
