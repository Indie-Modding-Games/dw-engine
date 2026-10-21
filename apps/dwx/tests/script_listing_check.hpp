#pragma once

#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/script_yaml.hpp>

#include <charconv>
#include <cstdint>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// What the map editor relies on when it turns a script listing (scriptToYaml) into text and back: the instructions
// in file order, each label named after its offset, and the labels exactly the places that something jumps to.
namespace dwe::dwx::testing {

namespace detail {

inline constexpr std::string_view HexPrefix = "0x";
inline constexpr std::string_view LabelKey = "label";
inline constexpr std::string_view LabelPrefix = "l_";

inline std::optional<std::uint32_t> hexOffset(std::string_view text) {
    if (!text.starts_with(HexPrefix)) {
        return std::nullopt;
    }
    text.remove_prefix(HexPrefix.size());
    std::uint32_t offset = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), offset, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return offset;
}

inline bool isLabelName(std::string_view text) {
    if (!text.starts_with(LabelPrefix)) {
        return false;
    }
    const auto offset = hexOffset(std::format("{}{}", HexPrefix, text.substr(LabelPrefix.size())));
    return offset && formats::script::labelName(*offset) == text;
}

// Every label name a node refers to, outside the labels themselves.
inline void collectJumps(const core::YamlNode& node, std::set<std::string>& jumps) {
    if (node.isScalar()) {
        if (isLabelName(node.text())) {
            jumps.insert(node.text());
        }
        return;
    }
    if (node.isSequence()) {
        for (const core::YamlNode& item : node.items()) {
            collectJumps(item, jumps);
        }
        return;
    }
    for (const core::YamlEntry& entry : node.entries()) {
        if (entry.key != LabelKey) {
            collectJumps(entry.value, jumps);
        }
    }
}

} // namespace detail

// The ways a listing breaks what the editor relies on, one line each; empty when there are none.
inline std::vector<std::string> listingProblems(const core::YamlNode& listing) {
    std::vector<std::string> problems;
    const core::YamlNode* instructions = listing.find("instructions");
    const core::YamlNode* codeStart = listing.find("code_start");
    if (instructions == nullptr || !instructions->isSequence() || instructions->items().empty() ||
        codeStart == nullptr) {
        return {"the listing has no code_start or no instructions"};
    }
    std::set<std::string> labels;
    std::optional<std::uint32_t> previous;
    for (const core::YamlNode& instruction : instructions->items()) {
        const core::YamlNode* at = instruction.find("at");
        const auto offset = at != nullptr ? detail::hexOffset(at->text()) : std::nullopt;
        if (!offset) {
            problems.emplace_back("an instruction without a hex offset");
            continue;
        }
        if (previous && *offset <= *previous) {
            problems.push_back(std::format("{} is not after the instruction before it", at->text()));
        }
        previous = offset;
        if (const core::YamlNode* label = instruction.find(detail::LabelKey)) {
            if (label->text() != formats::script::labelName(*offset)) {
                problems.push_back(std::format("{} has the label {}", at->text(), label->text()));
            }
            labels.insert(label->text());
        }
    }
    if (codeStart->text() != instructions->items().front().find("at")->text()) {
        problems.push_back(std::format("code_start {} is not the first instruction", codeStart->text()));
    }
    std::set<std::string> jumps;
    detail::collectJumps(listing, jumps);
    for (const std::string& jump : jumps) {
        if (!labels.contains(jump)) {
            problems.push_back(std::format("a jump to {}, which no instruction has", jump));
        }
    }
    for (const std::string& label : labels) {
        if (!jumps.contains(label)) {
            problems.push_back(std::format("the label {} that nothing jumps to", label));
        }
    }
    return problems;
}

} // namespace dwe::dwx::testing
