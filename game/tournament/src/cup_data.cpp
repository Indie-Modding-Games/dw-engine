#include <dwe/game/tournament/tournament.hpp>

#include <charconv>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::game::tournament {

namespace {

using core::YamlNode;

// Each jump table entry is a JUMP_TO of four bytes (getCupDataJumpTableEntry: id * 4 + 2).
constexpr std::uint32_t EntrySize = 4;
// The cup's entries: 0 its text, then the requirements, the overlevel, the eligible and the rivals.
constexpr std::uint32_t RequirementsEntry = 1;
constexpr std::uint32_t OverlevelEntry = 2;
constexpr std::uint32_t EligibleEntry = 3;
constexpr std::uint32_t RivalsEntry = 4;
constexpr std::string_view HexPrefix = "0x";
constexpr std::string_view LabelPrefix = "l_";
constexpr int Hex = 16;

struct Listed {
  std::string op;
  std::optional<std::uint32_t> target;
  std::string data;
};

std::optional<std::uint32_t> hexAfter(const YamlNode* node, std::string_view prefix) {
  if (node == nullptr || !node->isScalar() || !node->text().starts_with(prefix)) {
    return std::nullopt;
  }
  const std::string_view digits = std::string_view{node->text()}.substr(prefix.size());
  std::uint32_t value = 0;
  const auto [end, error] =
      std::from_chars(digits.data(), digits.data() + digits.size(), value, Hex);
  if (error != std::errc{} || end != digits.data() + digits.size()) {
    return std::nullopt;
  }
  return value;
}

std::vector<std::uint8_t> bytesOf(std::string_view text) {
  std::vector<std::uint8_t> bytes;
  while (!text.empty()) {
    const std::size_t space = text.find(' ');
    const std::string_view word = text.substr(0, space);
    std::uint32_t value = 0;
    if (std::from_chars(word.data(), word.data() + word.size(), value, Hex).ec == std::errc{}) {
      bytes.push_back(static_cast<std::uint8_t>(value));
    }
    text = space == std::string_view::npos ? std::string_view{} : text.substr(space + 1);
  }
  return bytes;
}

} // namespace

core::Result<CupData> cupDataOf(const YamlNode& listing, std::int32_t cup) {
  const YamlNode* sections = listing.find("sections");
  const YamlNode* instructions = listing.find("instructions");
  if (sections == nullptr || instructions == nullptr || !sections->isSequence() ||
      !instructions->isSequence() || cup < 0) {
    return core::fail("the script is no listing");
  }
  std::optional<std::uint32_t> start;
  for (const YamlNode& section : sections->items()) {
    if (hexAfter(section.find("id"), HexPrefix) == std::uint32_t{CupDataSection}) {
      start = hexAfter(section.find("at"), LabelPrefix);
      break;
    }
  }
  if (!start) {
    return core::fail(std::format("the script has no section {}", CupDataSection));
  }
  std::map<std::uint32_t, Listed> listed;
  for (const YamlNode& instruction : instructions->items()) {
    const auto at = hexAfter(instruction.find("at"), HexPrefix);
    const YamlNode* op = instruction.find("op");
    if (!at || op == nullptr || !op->isScalar()) {
      continue;
    }
    const YamlNode* data = instruction.find("data");
    listed.emplace(*at, Listed{.op = op->text(),
                               .target = hexAfter(instruction.find("target"), LabelPrefix),
                               .data = data != nullptr && data->isScalar() ? data->text() : ""});
  }
  const auto jump = [&](std::uint32_t at) -> std::optional<std::uint32_t> {
    const auto found = listed.find(at);
    if (found == listed.end() || found->second.op != "jump_to") {
      return std::nullopt;
    }
    return found->second.target;
  };
  const std::optional<std::uint32_t> table =
      jump(*start + (static_cast<std::uint32_t>(cup) * EntrySize));
  if (!table) {
    return core::fail(std::format("the script has no cup {}", cup));
  }
  const auto list = [&](std::uint32_t entry) -> core::Result<std::vector<std::int32_t>> {
    const std::optional<std::uint32_t> block = jump(*table + (entry * EntrySize));
    const auto found = block ? listed.find(*block) : listed.end();
    if (found == listed.end() || found->second.op != "data") {
      return core::fail(std::format("cup {} has no list {}", cup, entry));
    }
    return cupList(bytesOf(found->second.data));
  };
  CupData data;
  for (const auto& [entry, out] :
       {std::pair{RequirementsEntry, &data.requirements},
        std::pair{OverlevelEntry, &data.overlevel}, std::pair{EligibleEntry, &data.eligible},
        std::pair{RivalsEntry, &data.rivals}}) {
    auto values = list(entry);
    if (!values) {
      return std::unexpected{values.error()};
    }
    *out = std::move(*values);
  }
  return data;
}

} // namespace dwe::game::tournament
