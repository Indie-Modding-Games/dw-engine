#pragma once

#include <cstdint>

namespace dwe::ruleset {

// The random numbers of the game rules: seedable, so a test or a simulation with the same seed plays the same game on
// every platform (the standard distributions differ between standard libraries, so none is used). The rulesets keep
// the original's probabilities and distributions, not its generator: the design owner decided on 2026-10-20 that the
// rolls need not repeat the original's bit for bit. PCG32 (pcg-random.org, XSH RR).
class Random {
public:
    explicit Random(std::uint64_t seed) { reseed(seed); }

    // Starts the sequence of a seed again.
    void reseed(std::uint64_t seed) {
        state_ = 0;
        next();
        state_ += seed;
        next();
    }

    // Where the sequence is, to save it with a game; restoreState goes on from there.
    [[nodiscard]] std::uint64_t state() const { return state_; }
    void restoreState(std::uint64_t state) { state_ = state; }

    // The next 32 random bits.
    std::uint32_t next() {
        const std::uint64_t old = state_;
        state_ = (old * Multiplier) + Increment;
        const auto xorShifted = static_cast<std::uint32_t>(((old >> XorShift) ^ old) >> OutputShift);
        const auto rotation = static_cast<std::uint32_t>(old >> RotationShift);
        return (xorShifted >> rotation) | (xorShifted << ((~rotation + 1U) & RotationMask));
    }

    // A whole number from 0 to limit - 1, as the original's roll of a limit (math.c: randomLimit); 0 when the limit is
    // 0 or less. Like the original, a multiply and a shift rather than a remainder.
    std::int32_t below(std::int32_t limit) {
        if (limit <= 0) {
            return 0;
        }
        return static_cast<std::int32_t>((static_cast<std::uint64_t>(next()) * static_cast<std::uint64_t>(limit)) >>
                                         ResultBits);
    }

    // Whether a roll of 100 falls under `percent`: true `percent` times in 100.
    bool chance(std::int32_t percent) { return below(PercentLimit) < percent; }

private:
    static constexpr std::uint64_t Multiplier = 6364136223846793005ULL;
    static constexpr std::uint64_t Increment = 1442695040888963407ULL;
    static constexpr std::uint32_t XorShift = 18;
    static constexpr std::uint32_t OutputShift = 27;
    static constexpr std::uint32_t RotationShift = 59;
    static constexpr std::uint32_t RotationMask = 31;
    static constexpr std::uint32_t ResultBits = 32;
    static constexpr std::int32_t PercentLimit = 100;

    std::uint64_t state_ = 0;
};

} // namespace dwe::ruleset
