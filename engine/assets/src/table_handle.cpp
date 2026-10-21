#include <dwe/assets/table_handle.hpp>

#include <atomic>

namespace dwe::assets {

CompositionGeneration CompositionGeneration::next() {
    static std::atomic<std::uint32_t> last{0};
    return CompositionGeneration{last.fetch_add(1, std::memory_order_relaxed) + 1};
}

} // namespace dwe::assets
