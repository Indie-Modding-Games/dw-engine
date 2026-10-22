#pragma once

#include <dwe/core/yaml.hpp>
#include <dwe/game/save/migration.hpp>
#include <dwe/game/save/sections.hpp>

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace dwe::game::save::testing {

// Reads a file of tests/fixtures.
inline std::string readFixture(const std::string& name) {
  std::ifstream stream{std::filesystem::path{DWE_SAVE_FIXTURES} / name, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

// A system as a partner would be: version 1 saved `name` and `hp`; version 2 renamed `name` to
// `nickname` and added `max_hp`, which old saves take from `hp`.
struct Partner {
  std::string nickname;
  int hp = 0;
  int maxHp = 0;
};

class PartnerSection final : public SectionSerializer {
public:
  explicit PartnerSection(Partner& partner) : partner_{&partner} {}

  [[nodiscard]] core::Result<core::YamlNode> write() const override {
    core::YamlNode state = core::YamlNode::mapping();
    state.set("nickname",
              core::YamlNode::scalar(partner_->nickname, core::YamlNode::ScalarStyle::Quoted));
    state.set("hp", core::YamlNode::scalar(std::to_string(partner_->hp)));
    state.set("max_hp", core::YamlNode::scalar(std::to_string(partner_->maxHp)));
    return state;
  }

  core::Result<void> read(const core::YamlNode& state) override {
    const core::YamlNode* nickname = state.find("nickname");
    const core::YamlNode* hp = state.find("hp");
    const core::YamlNode* maxHp = state.find("max_hp");
    if (nickname == nullptr || hp == nullptr || maxHp == nullptr) {
      return core::fail("nickname, hp and max_hp are needed");
    }
    partner_->nickname = nickname->text();
    if (!readInt(*hp, partner_->hp) || !readInt(*maxHp, partner_->maxHp)) {
      return core::fail("hp and max_hp are numbers");
    }
    return {};
  }

  // The migrations of the section: 1 -> 2.
  static MigrationChain migrations() {
    MigrationChain chain;
    chain.add([](core::YamlNode state) -> core::Result<core::YamlNode> {
      const core::YamlNode* hp = state.find("hp");
      if (hp == nullptr) {
        return core::fail("hp is missing");
      }
      const core::YamlNode maxHp = *hp;
      state = renamedKey(state, "name", "nickname");
      state.set("max_hp", maxHp);
      return state;
    });
    return chain;
  }

private:
  static bool readInt(const core::YamlNode& node, int& value) {
    const std::string& text = node.text();
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
  }

  Partner* partner_;
};

} // namespace dwe::game::save::testing
