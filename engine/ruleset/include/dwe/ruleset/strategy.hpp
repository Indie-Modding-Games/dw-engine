#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ruleset/messages.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace dwe::ruleset {

// The implementations of a rule that is logic rather than a number, by name: a system declares an
// interface and a registry, registers `original` (the original game's behaviour) and any other, and
// its section names the one to use
// (`selection: original`). A name the registry does not have is a load error at that line of the
// section.
//
//     struct EvolutionSelection { virtual ~EvolutionSelection() = default; virtual int pick(...) =
//     0; }; StrategyRegistry<EvolutionSelection> selections{"evolution selection"};
//     selections.add("original", [] { return std::make_unique<OriginalSelection>(); });
//     auto selection = selections.create(*ruleset.section("evolution"), "selection");
template <typename Interface>
class StrategyRegistry {
public:
  using Factory = std::function<std::unique_ptr<Interface>()>;

  // `what` names the rule in the errors: "unknown evolution selection 'x'".
  explicit StrategyRegistry(std::string what) : what_(std::move(what)) {}

  // Registers an implementation; a later one with the same name replaces it.
  void add(std::string name, Factory factory) {
    factories_.insert_or_assign(std::move(name), std::move(factory));
  }

  [[nodiscard]] bool contains(std::string_view name) const { return factories_.contains(name); }

  // The registered names, sorted, separated by commas.
  [[nodiscard]] std::string names() const {
    std::string text;
    for (const auto& [name, factory] : factories_) {
      if (!text.empty()) {
        text += messages::NameSeparator;
      }
      text += name;
    }
    return text;
  }

  // Checks that the value at `path` of a section is a registered name, for the section's check.
  [[nodiscard]] core::Result<void> check(const Section& section, std::string_view path) const {
    auto found = find(section, path);
    if (!found) {
      return std::unexpected{found.error()};
    }
    return {};
  }

  // Builds the implementation that the value at `path` of a section names.
  [[nodiscard]] core::Result<std::unique_ptr<Interface>> create(const Section& section,
                                                                std::string_view path) const {
    auto found = find(section, path);
    if (!found) {
      return std::unexpected{found.error()};
    }
    return (*found)->second();
  }

private:
  using Factories = std::map<std::string, Factory, std::less<>>;

  [[nodiscard]] core::Result<typename Factories::const_iterator> find(const Section& section,
                                                                      std::string_view path) const {
    const core::YamlNode* node = findValue(section.node, path);
    if (node == nullptr) {
      return std::unexpected{
          section.error(section.node, messages::formatMessage(messages::MissingStrategy, path))};
    }
    if (!node->isScalar()) {
      return std::unexpected{
          section.error(*node, messages::formatMessage(messages::StrategyNotName, path, what_))};
    }
    const auto found = factories_.find(node->text());
    if (found == factories_.end()) {
      return std::unexpected{section.error(
          *node, messages::formatMessage(messages::UnknownStrategy, what_, node->text(), names()))};
    }
    return found;
  }

  std::string what_;
  Factories factories_;
};

} // namespace dwe::ruleset
