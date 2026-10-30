#include <dwe/game/inventory/merit_shop.hpp>

#include <algorithm>

namespace dwe::game::inventory {

std::vector<MeritOffer> meritOffers(const Inventory& inventory, const assets::ItemTable& items,
                                    std::int32_t merit) {
  std::vector<const assets::ItemEntry*> priced;
  for (const assets::ItemEntry& item : items.entries()) {
    if (item.meritValue > 0) {
      priced.push_back(&item);
    }
  }
  std::ranges::sort(priced, {}, &assets::ItemEntry::index);
  std::vector<MeritOffer> offers;
  offers.reserve(priced.size());
  for (const assets::ItemEntry* item : priced) {
    const assets::ItemHandle handle = items.handleOf(*item);
    // The merit first, then a free slot or a stack of the item that is not full.
    const bool buyable = item->meritValue <= merit && inventory.hasRoomFor(handle);
    offers.push_back({.item = handle, .price = item->meritValue, .buyable = buyable});
  }
  return offers;
}

std::optional<std::int32_t> cardMerit(const CardCollection& cards, std::int32_t card) {
  const assets::CardEntry* entry = cards.table().find(card);
  if (entry == nullptr || entry->meritValue <= 0) {
    return std::nullopt;
  }
  return entry->meritValue;
}

std::int32_t addMerit(std::int32_t merit, std::int32_t gained, std::int32_t most) {
  return std::min(merit + gained, most);
}

} // namespace dwe::game::inventory
