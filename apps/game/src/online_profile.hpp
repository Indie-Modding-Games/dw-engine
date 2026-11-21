#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace dwe::app {

struct UserSettings;

// The player's online profile (#294): the name the other players see in rooms and over their head,
// an icon from a fixed list and the online versus record. It lives in the game's settings
// (settings.yaml, UserSettings), not in a save, so it stays the same whatever game is loaded.
class OnlineProfile {
public:
  // The icons a profile can show, in the profile screen's order.
  static constexpr std::array<std::string_view, 6> Icons{"egg",    "device", "crown",
                                                         "swords", "tree",   "dumbbell"};
  static constexpr std::string_view DefaultNameKey = "online.profile.default_name";
  // A name's longest length, in letters (UTF-8 code points).
  static constexpr std::size_t NameLength = 12;

  // The profile the settings keep; the defaults where they keep nothing, `defaultName` (the
  // language's online.profile.default_name) for the name.
  static OnlineProfile fromSettings(const UserSettings& settings, std::string_view defaultName);
  // Writes the profile into the settings.
  void store(UserSettings& settings) const;

  // A typed name as the profile keeps it: control characters left out, spaces at the ends and
  // doubled spaces dropped, cut to NameLength letters. Empty when nothing is left.
  [[nodiscard]] static std::string cleanName(std::string_view typed);
  [[nodiscard]] static bool knownIcon(std::string_view icon);

  [[nodiscard]] const std::string& name() const { return name_; }
  [[nodiscard]] const std::string& icon() const { return icon_; }
  [[nodiscard]] int wins() const { return wins_; }
  [[nodiscard]] int losses() const { return losses_; }
  // Whether the player chose a name (else the default one shows).
  [[nodiscard]] bool named() const { return named_; }
  // Online rooms connect through the server's relay only (UserSettings::onlineRelayOnly).
  [[nodiscard]] bool relayOnly() const { return relayOnly_; }
  void setRelayOnly(bool on) { relayOnly_ = on; }

  // False (and nothing changes) for a name with nothing left once cleaned.
  bool setName(std::string_view typed);
  // False (and nothing changes) for an icon not in Icons.
  bool setIcon(std::string_view icon);
  void recordVersus(bool won);

private:
  std::string name_;
  std::string icon_{Icons.front()};
  int wins_ = 0;
  int losses_ = 0;
  bool named_ = false;
  bool relayOnly_ = false;
};

} // namespace dwe::app
