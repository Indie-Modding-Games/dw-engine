#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace dwe::formats::model {

// A bone of a model's skeleton (`SkeletonBone`, include/dw/model.h in the decomp). The skeletons live in the
// executable, one array per kind of model with as many bones as its `boneCount`.
struct SkeletonBone {
    std::int8_t objIndex = -1;    // the TMD object drawn with this bone, or -1 for none
    std::int8_t parentIndex = -1; // the parent bone, or -1 for a root

    bool operator==(const SkeletonBone&) const = default;
};

// Checks that a skeleton can be used with a TMD of objectCount objects: at least one bone, each parent before its
// children and each object index in range.
core::Result<void> validateSkeleton(std::span<const SkeletonBone> skeleton, std::size_t objectCount);

} // namespace dwe::formats::model
