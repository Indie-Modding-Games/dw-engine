#include <dwe/formats/model/skeleton.hpp>

#include <format>
#include <utility>

namespace dwe::formats::model {

core::Result<void> validateSkeleton(std::span<const SkeletonBone> skeleton, std::size_t objectCount) {
    if (skeleton.empty()) {
        return core::fail("the skeleton has no bones");
    }
    for (std::size_t i = 0; i < skeleton.size(); ++i) {
        const SkeletonBone& bone = skeleton[i];
        if (bone.parentIndex < -1 || (bone.parentIndex >= 0 && std::cmp_greater_equal(bone.parentIndex, i))) {
            return core::fail(
                std::format("bone {} has parent {}; a parent must come before its children", i, bone.parentIndex));
        }
        if (bone.objIndex < -1 || (bone.objIndex >= 0 && std::cmp_greater_equal(bone.objIndex, objectCount))) {
            return core::fail(
                std::format("bone {} draws object {} but the model has {}", i, bone.objIndex, objectCount));
        }
    }
    return {};
}

} // namespace dwe::formats::model
