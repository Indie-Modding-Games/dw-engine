#include <dwe/game/story/story_orders.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <ostream>
#include <vector>

using namespace dwe::game::story;

namespace {

// The base set of the original: 800 triggers, 256 variables, values 0 to 255.
constexpr std::int64_t BaseTriggers = 800;
constexpr std::int64_t BaseVariables = 256;
constexpr StoryRules OriginalRules{.variableMax = 255};
constexpr std::int64_t Huge = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t HugeNegative = std::numeric_limits<std::int64_t>::min();

// A state of any size; the orders check the ranges before they reach it.
class FakeState final : public StoryStatePort {
public:
    explicit FakeState(std::int64_t triggers = BaseTriggers, std::int64_t variables = BaseVariables)
        : triggers_(static_cast<std::size_t>(triggers)), variables_(static_cast<std::size_t>(variables)) {}

    [[nodiscard]] std::int64_t triggerCount() const override { return static_cast<std::int64_t>(triggers_.size()); }
    [[nodiscard]] std::int64_t variableCount() const override { return static_cast<std::int64_t>(variables_.size()); }
    [[nodiscard]] dwe::core::Result<bool> trigger(std::int64_t trigger) const override {
        return triggers_.at(static_cast<std::size_t>(trigger));
    }
    dwe::core::Result<void> setTrigger(std::int64_t trigger, bool set) override {
        triggers_.at(static_cast<std::size_t>(trigger)) = set;
        return {};
    }
    [[nodiscard]] dwe::core::Result<std::int64_t> variable(std::int64_t variable) const override {
        return variables_.at(static_cast<std::size_t>(variable));
    }
    dwe::core::Result<void> setVariable(std::int64_t variable, std::int64_t value) override {
        variables_.at(static_cast<std::size_t>(variable)) = value;
        return {};
    }

private:
    std::vector<bool> triggers_;
    std::vector<std::int64_t> variables_;
};

class FixedRandom final : public StoryRandomPort {
public:
    explicit FixedRandom(std::int64_t value) : value_{value} {}
    std::int64_t next() override { return value_; }

private:
    std::int64_t value_;
};

std::int64_t variableOf(const FakeState& state, std::int64_t variable) {
    return state.variable(variable).value_or(-1);
}

} // namespace

TEST_CASE("set_trigger and unset_trigger change one trigger, and one the state does not hold is an error") {
    FakeState state;
    FixedRandom random{0};
    StoryOrders orders{state, random, OriginalRules};
    REQUIRE(orders.setTrigger(799));
    CHECK(state.trigger(799).value_or(false));
    REQUIRE(orders.unsetTrigger(799));
    CHECK_FALSE(state.trigger(799).value_or(true));
    CHECK_FALSE(orders.setTrigger(800));
    CHECK_FALSE(orders.setTrigger(-1));
    CHECK_FALSE(orders.unsetTrigger(Huge));
}

TEST_CASE("a state that holds more than the base set takes triggers and variables past it") {
    FakeState state{1200, 512};
    FixedRandom random{0};
    StoryOrders orders{state, random, OriginalRules};
    REQUIRE(orders.setTrigger(1000));
    CHECK(state.trigger(1000).value_or(false));
    REQUIRE(orders.setVariable(400, 9));
    CHECK(variableOf(state, 400) == 9);
    CHECK_FALSE(orders.setTrigger(1200));
    CHECK_FALSE(orders.setVariable(512, 1));
}

TEST_CASE("add_to_pstat stops at the top of the variable range and reduce_pstat at 0, as in the original") {
    FakeState state;
    FixedRandom random{0};
    StoryOrders orders{state, random, OriginalRules};
    REQUIRE(orders.setVariable(101, 250));
    REQUIRE(orders.addToVariable(101, 3));
    CHECK(variableOf(state, 101) == 253);
    REQUIRE(orders.addToVariable(101, 200));
    CHECK(variableOf(state, 101) == 255);
    REQUIRE(orders.reduceVariable(101, 100));
    CHECK(variableOf(state, 101) == 155);
    REQUIRE(orders.reduceVariable(101, 255));
    CHECK(variableOf(state, 101) == 0);
}

TEST_CASE("any integer is an operand: values stop at the ends of the variable range, and nothing overflows") {
    FakeState state;
    FixedRandom random{0};
    StoryOrders orders{state, random, OriginalRules};
    REQUIRE(orders.setVariable(1, 300));
    CHECK(variableOf(state, 1) == 255);
    REQUIRE(orders.setVariable(1, -5));
    CHECK(variableOf(state, 1) == 0);
    REQUIRE(orders.addToVariable(1, Huge));
    CHECK(variableOf(state, 1) == 255);
    REQUIRE(orders.addToVariable(1, HugeNegative));
    CHECK(variableOf(state, 1) == 0);
    REQUIRE(orders.reduceVariable(1, HugeNegative));
    CHECK(variableOf(state, 1) == 255);
    REQUIRE(orders.reduceVariable(1, Huge));
    CHECK(variableOf(state, 1) == 0);
    REQUIRE(orders.addToVariable(1, -1));
    CHECK(variableOf(state, 1) == 0);
}

TEST_CASE("the variable range is the rules', not a storage width") {
    FakeState state;
    FixedRandom random{0};
    StoryOrders orders{state, random, StoryRules{.variableMax = 1000}};
    REQUIRE(orders.setVariable(1, 900));
    REQUIRE(orders.addToVariable(1, 200));
    CHECK(variableOf(state, 1) == 1000);
}

TEST_CASE("copy_pstat copies the source into the target, and a variable the state does not hold is an error") {
    FakeState state;
    FixedRandom random{0};
    StoryOrders orders{state, random, OriginalRules};
    REQUIRE(orders.setVariable(3, 42));
    REQUIRE(orders.copyVariable(3, 250));
    CHECK(variableOf(state, 250) == 42);
    CHECK(variableOf(state, 3) == 42);
    CHECK_FALSE(orders.copyVariable(3, 256));
    CHECK_FALSE(orders.copyVariable(-1, 3));
    CHECK_FALSE(orders.addToVariable(Huge, 1));
    CHECK(variableOf(state, 3) == 42);
}

TEST_CASE("store_random scales the draw by highest + 1 and keeps the top 15 bits, as math.c: randomLimit") {
    FakeState state;
    FixedRandom top{StoryRandomPort::RandomMax};
    StoryOrders highest{state, top, OriginalRules};
    REQUIRE(highest.storeRandom(7, 255));
    CHECK(variableOf(state, 7) == 255);
    REQUIRE(highest.storeRandom(7, 0));
    CHECK(variableOf(state, 7) == 0);
    REQUIRE(highest.storeRandom(7, Huge));
    CHECK(variableOf(state, 7) == 255);

    FixedRandom half{0x4000};
    StoryOrders middle{state, half, OriginalRules};
    REQUIRE(middle.storeRandom(7, 9));
    CHECK(variableOf(state, 7) == 5);
    REQUIRE(middle.storeRandom(7, -3));
    CHECK(variableOf(state, 7) == 0);
}

TEST_CASE("delay waits its frames, at least one") {
    CHECK(StoryOrders::delay(0).frames == 1);
    CHECK(StoryOrders::delay(-4).frames == 1);
    CHECK(StoryOrders::delay(20).frames == 20);
    CHECK(StoryOrders::delay(0x10000).frames == 0x10000);
}
