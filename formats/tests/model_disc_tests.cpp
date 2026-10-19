#include <dwe/formats/model/animation.hpp>
#include <dwe/formats/model/mmd.hpp>
#include <dwe/formats/model/skeleton.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using namespace dwe::formats::model;

namespace {

std::vector<std::byte> readFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> chars{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes(chars.size());
    std::ranges::transform(chars, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

// Parses "object:parent,object:parent,...", the bones of the model's skeleton in order.
std::vector<SkeletonBone> parseSkeleton(std::string_view text) {
    std::vector<SkeletonBone> bones;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view bone = text.substr(0, comma);
        const std::size_t colon = bone.find(':');
        int object = 0;
        int parent = 0;
        const std::string_view objectText = bone.substr(0, colon);
        const std::string_view parentText = bone.substr(colon + 1);
        std::from_chars(objectText.data(), objectText.data() + objectText.size(), object);
        std::from_chars(parentText.data(), parentText.data() + parentText.size(), parent);
        bones.push_back(
            {.objIndex = static_cast<std::int8_t>(object), .parentIndex = static_cast<std::int8_t>(parent)});
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    }
    return bones;
}

} // namespace

// Runs only on a developer's machine, against a file of their own disc: DWE_MMD_SAMPLE is the path of an MMD and
// DWE_MMD_SKELETON, if set, its skeleton as "object:parent,...". Nothing from the disc is part of the repository.
TEST_CASE("a model of the user's disc is read and every animation plays") {
    const char* path = std::getenv("DWE_MMD_SAMPLE");
    if (path == nullptr) {
        return;
    }
    const auto bytes = readFile(path);
    const auto mmd = readMmd(bytes);
    REQUIRE_MESSAGE(mmd.has_value(), mmd.error().message);
    std::size_t primitives = 0;
    for (const TmdObject& object : mmd->tmd.objects) {
        primitives += object.primitives.size();
    }
    MESSAGE(std::string(path), ": ", mmd->tmd.objects.size(), " objects, ", primitives, " primitives, ",
            mmd->tmd.issues.size(), " issues, ", mmd->animations.size(), " animation entries");
    for (const TmdIssue& issue : mmd->tmd.issues) {
        MESSAGE("object ", issue.object, " primitive ", issue.primitive, ": ", issue.message);
    }

    const char* skeletonText = std::getenv("DWE_MMD_SKELETON");
    if (skeletonText == nullptr) {
        return;
    }
    const auto skeleton = parseSkeleton(skeletonText);
    const auto valid = validateSkeleton(skeleton, mmd->tmd.objects.size());
    REQUIRE_MESSAGE(valid.has_value(), valid.error().message);
    std::size_t played = 0;
    std::size_t ticks = 0;
    std::size_t forever = 0;
    for (std::size_t id = 0; id < mmd->animations.size(); ++id) {
        const auto offset = mmd->animations[id];
        if (!offset) {
            continue;
        }
        const auto animation =
            simulateAnimation(std::span(bytes).subspan(*offset), static_cast<std::uint8_t>(id), skeleton.size());
        REQUIRE_MESSAGE(animation.has_value(), animation.error().message);
        ++played;
        ticks += animation->tickCount();
        forever += animation->endsInLoop ? 1 : 0;
    }
    MESSAGE(played, " animations, ", ticks, " ticks, ", forever, " end in a loop");
}
