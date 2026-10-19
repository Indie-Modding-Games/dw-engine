#pragma once

#include <dwe/core/yaml.hpp>

#include <concepts>
#include <string>
#include <utility>

// Scalars and small nodes shared by the YAML converters of formats.
namespace dwe::formats::yaml {

inline core::YamlNode null() {
    return core::YamlNode::scalar("null");
}

template <std::integral T>
core::YamlNode number(T value) {
    return core::YamlNode::scalar(std::to_string(value));
}

inline core::YamlNode boolean(bool value) {
    return core::YamlNode::scalar(value ? "true" : "false");
}

inline core::YamlNode quoted(std::string text) {
    return core::YamlNode::scalar(std::move(text), core::YamlNode::ScalarStyle::Quoted);
}

template <typename Range>
core::YamlNode numbers(const Range& values) {
    core::YamlNode list = core::YamlNode::sequence();
    for (const auto value : values) {
        list.append(number(value));
    }
    return list;
}

// A mapping {x, y, z}.
template <std::integral T>
core::YamlNode vector3(T x, T y, T z) {
    core::YamlNode node = core::YamlNode::mapping();
    node.set("x", number(x));
    node.set("y", number(y));
    node.set("z", number(z));
    return node;
}

} // namespace dwe::formats::yaml
