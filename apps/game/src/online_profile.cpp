#include "online_profile.hpp"

#include <algorithm>

#include "user_settings.hpp"

namespace dwe::app {
namespace {

// The bytes of the UTF-8 letter that starts with `lead`.
std::size_t letterSize(unsigned char lead) {
  if (lead < 0x80) {
    return 1;
  }
  if ((lead & 0xE0) == 0xC0) {
    return 2;
  }
  if ((lead & 0xF0) == 0xE0) {
    return 3;
  }
  return (lead & 0xF8) == 0xF0 ? 4 : 1;
}

} // namespace

std::string OnlineProfile::cleanName(std::string_view typed) {
  std::string name;
  std::size_t letters = 0;
  bool space = false;
  for (std::size_t at = 0; at < typed.size() && letters < NameLength;) {
    const auto lead = static_cast<unsigned char>(typed[at]);
    const std::size_t size = std::min(letterSize(lead), typed.size() - at);
    if (size == 1 && (lead < ' ' || lead == 0x7f || lead >= 0x80)) {
      // A control character, or a byte that cannot start a letter.
      ++at;
      continue;
    }
    if (lead == ' ') {
      space = !name.empty();
      ++at;
      continue;
    }
    if (space) {
      if (letters + 1 >= NameLength) {
        break;
      }
      name.push_back(' ');
      ++letters;
      space = false;
    }
    name.append(typed.substr(at, size));
    ++letters;
    at += size;
  }
  return name;
}

bool OnlineProfile::knownIcon(std::string_view icon) {
  return std::ranges::find(Icons, icon) != Icons.end();
}

OnlineProfile OnlineProfile::fromSettings(const UserSettings& settings,
                                          std::string_view defaultName) {
  OnlineProfile profile;
  const std::string name = cleanName(settings.onlineName.value_or(std::string{}));
  profile.named_ = !name.empty();
  profile.name_ = profile.named_ ? name : std::string{defaultName};
  if (settings.onlineIcon && knownIcon(*settings.onlineIcon)) {
    profile.icon_ = *settings.onlineIcon;
  }
  profile.wins_ = std::max(0, settings.onlineWins.value_or(0));
  profile.losses_ = std::max(0, settings.onlineLosses.value_or(0));
  profile.relayOnly_ = settings.onlineRelayOnly.value_or(false);
  return profile;
}

void OnlineProfile::store(UserSettings& settings) const {
  if (named_) {
    settings.onlineName = name_;
  }
  settings.onlineIcon = icon_;
  settings.onlineWins = wins_;
  settings.onlineLosses = losses_;
  // Written only once chosen, so a settings file that never chose it keeps not naming it.
  if (relayOnly_ || settings.onlineRelayOnly) {
    settings.onlineRelayOnly = relayOnly_;
  }
}

bool OnlineProfile::setName(std::string_view typed) {
  std::string name = cleanName(typed);
  if (name.empty()) {
    return false;
  }
  name_ = std::move(name);
  named_ = true;
  return true;
}

bool OnlineProfile::setIcon(std::string_view icon) {
  if (!knownIcon(icon)) {
    return false;
  }
  icon_ = std::string{icon};
  return true;
}

void OnlineProfile::recordVersus(bool won) {
  (won ? wins_ : losses_) += 1;
}

} // namespace dwe::app
