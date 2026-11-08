#pragma once

#include <dwe/core/error.hpp>
#include <dwe/net/datagram.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace dwe::game::versus {

// The version of the messages below: two machines talk only with the same one.
inline constexpr std::uint32_t ProtocolVersion = 1;
// The UDP port a host listens on, and a guest's address without one goes to.
inline constexpr std::uint16_t DefaultPort = 47120;
// The battle frames each side's commands are scheduled ahead: a 150 ms delay at 20 frames a
// second, which hides most of a home connection's round trip.
inline constexpr std::uint32_t DefaultInputDelay = 3;

// A partner a player brings to a versus series: its registered record (main_menu.c:
// registerBattleData), which is all the other machine needs to build the same fighter from its own
// copy of the game's content (vs_combat.c: VS_loadFighterEntities). Ids, not data: the species
// and the techniques by their ids in the tables.
struct VersusFighter {
  static constexpr std::size_t Techniques = 3;
  std::string name;
  std::string species;
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
  std::int32_t brains = 0;
  std::int32_t discipline = 0;
  std::array<std::optional<std::string>, Techniques> techniques{};

  friend bool operator==(const VersusFighter&, const VersusFighter&) = default;
};

// Who a machine is: both must run the same protocol, the same game version and the same content
// (the hash of what the battle reads), or the battles would not play alike.
struct Identity {
  std::uint32_t protocol = ProtocolVersion;
  std::string gameVersion;
  std::string contentHash;

  friend bool operator==(const Identity&, const Identity&) = default;
};

// What the host decides for the whole series.
struct SeriesSetup {
  // The battles of the series (one of the rules' series).
  std::int32_t battles = 1;
  // The arena, by its place in the arenas both machines read from the same content.
  std::int32_t arena = 0;
  // The seed of the first battle; each next one adds its number.
  std::uint64_t seed = 0;
  // The frames each side's commands are scheduled ahead.
  std::uint32_t inputDelay = 0;

  friend bool operator==(const SeriesSetup&, const SeriesSetup&) = default;
};

// Why a machine refuses the other or leaves.
enum class LeaveReason : std::uint8_t {
  Quit = 0,
  ProtocolMismatch = 1,
  VersionMismatch = 2,
  ContentMismatch = 3,
  BadLineup = 4,
  Desync = 5,
};

// The guest's first message.
struct Hello {
  Identity identity;
};
// The host's answer: its identity and the series.
struct Welcome {
  Identity identity;
  SeriesSetup setup;
};
// The partners a side fights with, in the order of the battles.
struct Lineup {
  std::vector<VersusFighter> fighters;
};
// A battle of the series ended this way on the sender's machine.
struct RoundOver {
  std::int32_t round = 0;
  // 1 the host won, -1 the guest won, 2 both lost (vs_combat.c: VS__combatMain's results).
  std::int32_t result = 0;
  std::uint64_t checksum = 0;
};
struct Leave {
  LeaveReason reason = LeaveReason::Quit;
};

using Message = std::variant<Hello, Welcome, Lineup, RoundOver, Leave>;

// The first byte of each message; the lockstep's own messages use 0x40 and 0x41.
inline constexpr std::uint8_t HelloKind = 0x10;
inline constexpr std::uint8_t WelcomeKind = 0x11;
inline constexpr std::uint8_t LineupKind = 0x12;
inline constexpr std::uint8_t RoundOverKind = 0x13;
inline constexpr std::uint8_t LeaveKind = 0x14;

[[nodiscard]] net::Bytes encode(const Message& message);
// Reads a message of this protocol: none when the first byte is not one of its kinds, an error
// when it is and the rest is damaged.
[[nodiscard]] core::Result<std::optional<Message>> decode(std::span<const std::uint8_t> bytes);

} // namespace dwe::game::versus
