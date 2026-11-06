#include <dwe/game/save/text_keys.hpp>

namespace dwe::game::save::text_key {

std::string autosaveReason(AutosaveReason reason) {
  std::string key{AutosaveReasonPrefix};
  key += autosaveReasonName(reason);
  return key;
}

} // namespace dwe::game::save::text_key
