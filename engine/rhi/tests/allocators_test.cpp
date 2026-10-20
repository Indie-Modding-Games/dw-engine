#include <dwe/rhi/linear_allocator.hpp>
#include <dwe/rhi/slot_allocator.hpp>

#include <doctest/doctest.h>

#include <set>

using dwe::rhi::LinearAllocator;
using dwe::rhi::SlotAllocator;

TEST_CASE("the linear allocator aligns each range and fails when full") {
    LinearAllocator allocator{100};
    CHECK(allocator.allocate(10, 16) == 0);
    CHECK(allocator.allocate(4, 16) == 16);
    CHECK(allocator.allocate(1, 1) == 20);
    CHECK(allocator.allocate(8, 8) == 24);
    CHECK(allocator.used() == 32);
    CHECK(allocator.allocate(68, 4) == 32);
    CHECK(allocator.used() == 100);
    CHECK_FALSE(allocator.allocate(1, 1));
}

TEST_CASE("the linear allocator refuses what does not fit, even after padding") {
    LinearAllocator allocator{64};
    CHECK(allocator.allocate(33, 1) == 0);
    // 33 rounds up to 48, and 48 + 32 is past the end.
    CHECK_FALSE(allocator.allocate(32, 16));
    CHECK(allocator.allocate(16, 16) == 48);
    CHECK_FALSE(allocator.allocate(0, 128));
    CHECK_FALSE(allocator.allocate(1, 3));
}

TEST_CASE("the linear allocator starts over after a reset") {
    LinearAllocator allocator{32};
    CHECK(allocator.allocate(32, 4) == 0);
    allocator.reset();
    CHECK(allocator.used() == 0);
    CHECK(allocator.allocate(32, 4) == 0);
}

TEST_CASE("the slot allocator hands out each index once until released") {
    SlotAllocator slots{3};
    std::set<std::uint32_t> taken;
    for (int i = 0; i < 3; ++i) {
        const auto slot = slots.acquire();
        REQUIRE(slot.has_value());
        CHECK(slot.value_or(3) < 3);
        CHECK(taken.insert(slot.value_or(3)).second);
    }
    CHECK_FALSE(slots.acquire());
    CHECK(slots.used() == 3);

    slots.release(1);
    CHECK(slots.used() == 2);
    CHECK(slots.acquire() == 1);
    CHECK_FALSE(slots.acquire());
}

TEST_CASE("the slot allocator reuses the most recently released index first") {
    SlotAllocator slots{8};
    for (int i = 0; i < 4; ++i) {
        CHECK(slots.acquire());
    }
    slots.release(0);
    slots.release(2);
    CHECK(slots.acquire() == 2);
    CHECK(slots.acquire() == 0);
    CHECK(slots.acquire() == 4);
}

TEST_CASE("the slot allocator ignores releasing a slot that is not in use") {
    SlotAllocator slots{4};
    CHECK(slots.acquire() == 0);
    CHECK(slots.acquire() == 1);

    CHECK(slots.release(0));
    CHECK_FALSE(slots.release(0));
    CHECK_FALSE(slots.release(2));
    CHECK_FALSE(slots.release(9));
    CHECK(slots.used() == 1);

    // A double release must not hand the same slot out twice.
    CHECK(slots.acquire() == 0);
    CHECK(slots.acquire() == 2);
}
