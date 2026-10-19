#pragma once

#include <dwe/core/log.hpp>

#include <cstdlib>

// Stops the program when a condition that must always hold does not. Active in every build: a broken invariant is a
// bug, and continuing would only hide where it happened.
#define DWE_ASSERT(condition)                                                                                          \
    do {                                                                                                               \
        if (const bool dweAssertHolds = (condition); !dweAssertHolds) {                                                \
            ::dwe::core::logError("assertion failed: {} ({}:{})", #condition, __FILE__, __LINE__);                     \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
