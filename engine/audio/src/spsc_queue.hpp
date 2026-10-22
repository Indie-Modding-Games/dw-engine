#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <new>
#include <optional>
#include <type_traits>

namespace dwe::audio {

// A lock-free queue between one producer thread and one consumer thread, of fixed capacity, that
// never allocates. The producer stages items and publishes them together, so the consumer sees all
// of a batch or none of it.
template <typename Item, std::size_t Capacity>
class SpscQueue {
  // Items are copied in and out of the slots while the other thread may read the indices.
  static_assert(std::is_trivially_copyable_v<Item>);

public:
  // Producer: adds an item the consumer cannot see until publish. False when the queue is full.
  bool stage(const Item& item) {
    if (!hasRoom()) {
      return false;
    }
    items_[staged_ % Capacity] = item;
    ++staged_;
    return true;
  }

  [[nodiscard]] bool hasRoom() const {
    return staged_ - read_.load(std::memory_order_acquire) < Capacity;
  }

  // Producer: lets the consumer see every staged item.
  void publish() { published_.store(staged_, std::memory_order_release); }

  bool push(const Item& item) {
    if (!stage(item)) {
      return false;
    }
    publish();
    return true;
  }

  std::optional<Item> pop() {
    const std::size_t read = read_.load(std::memory_order_relaxed);
    if (read == published_.load(std::memory_order_acquire)) {
      return std::nullopt;
    }
    const Item item = items_[read % Capacity];
    read_.store(read + 1, std::memory_order_release);
    return item;
  }

private:
  // Apart, so the two threads do not fight over one cache line.
  static constexpr std::size_t CacheLine = 64;

  std::array<Item, Capacity> items_{};
  alignas(CacheLine) std::atomic<std::size_t> published_{0};
  alignas(CacheLine) std::atomic<std::size_t> read_{0};
  // Only the producer touches it.
  alignas(CacheLine) std::size_t staged_ = 0;
};

} // namespace dwe::audio
