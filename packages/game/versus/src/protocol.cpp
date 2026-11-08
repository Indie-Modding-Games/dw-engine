#include <dwe/game/versus/protocol.hpp>
#include <dwe/net/wire.hpp>

#include <format>
#include <type_traits>

namespace dwe::game::versus {

namespace {

// More partners than any series takes: a lineup longer than this is damaged.
constexpr std::uint8_t MaxLineup = 16;
constexpr std::uint8_t LastReason = static_cast<std::uint8_t>(LeaveReason::Desync);

void put(net::WireWriter& out, const Identity& identity) {
  out.put(identity.protocol).put(std::string_view{identity.gameVersion});
  out.put(std::string_view{identity.contentHash});
}

Identity getIdentity(net::WireReader& in) {
  Identity identity;
  identity.protocol = in.get<std::uint32_t>();
  identity.gameVersion = in.text();
  identity.contentHash = in.text();
  return identity;
}

void put(net::WireWriter& out, const VersusFighter& fighter) {
  out.put(std::string_view{fighter.name}).put(std::string_view{fighter.species});
  out.put(fighter.hp).put(fighter.mp).put(fighter.offense).put(fighter.defense);
  out.put(fighter.speed).put(fighter.brains).put(fighter.discipline);
  for (const std::optional<std::string>& technique : fighter.techniques) {
    out.put(technique.has_value());
    if (technique) {
      out.put(std::string_view{*technique});
    }
  }
}

VersusFighter getFighter(net::WireReader& in) {
  VersusFighter fighter;
  fighter.name = in.text();
  fighter.species = in.text();
  fighter.hp = in.get<std::int32_t>();
  fighter.mp = in.get<std::int32_t>();
  fighter.offense = in.get<std::int32_t>();
  fighter.defense = in.get<std::int32_t>();
  fighter.speed = in.get<std::int32_t>();
  fighter.brains = in.get<std::int32_t>();
  fighter.discipline = in.get<std::int32_t>();
  for (std::optional<std::string>& technique : fighter.techniques) {
    if (in.get<bool>()) {
      technique = in.text();
    }
  }
  return fighter;
}

} // namespace

net::Bytes encode(const Message& message) {
  net::WireWriter out;
  std::visit(
      [&](const auto& body) {
        using T = std::decay_t<decltype(body)>;
        if constexpr (std::is_same_v<T, Hello>) {
          out.put(HelloKind);
          put(out, body.identity);
        } else if constexpr (std::is_same_v<T, Welcome>) {
          out.put(WelcomeKind);
          put(out, body.identity);
          out.put(body.setup.battles).put(body.setup.arena).put(body.setup.seed);
          out.put(body.setup.inputDelay);
        } else if constexpr (std::is_same_v<T, Lineup>) {
          out.put(LineupKind).put(static_cast<std::uint8_t>(body.fighters.size()));
          for (const VersusFighter& fighter : body.fighters) {
            put(out, fighter);
          }
        } else if constexpr (std::is_same_v<T, RoundOver>) {
          out.put(RoundOverKind).put(body.round).put(body.result).put(body.checksum);
        } else {
          out.put(LeaveKind).put(body.reason);
        }
      },
      message);
  return out.take();
}

core::Result<std::optional<Message>> decode(std::span<const std::uint8_t> bytes) {
  if (bytes.empty() || bytes.front() < HelloKind || bytes.front() > LeaveKind) {
    return std::optional<Message>{};
  }
  net::WireReader in{bytes};
  const auto kind = in.get<std::uint8_t>();
  std::optional<Message> message;
  switch (kind) {
  case HelloKind:
    message = Hello{.identity = getIdentity(in)};
    break;
  case WelcomeKind: {
    Welcome welcome{.identity = getIdentity(in), .setup = {}};
    welcome.setup.battles = in.get<std::int32_t>();
    welcome.setup.arena = in.get<std::int32_t>();
    welcome.setup.seed = in.get<std::uint64_t>();
    welcome.setup.inputDelay = in.get<std::uint32_t>();
    message = welcome;
    break;
  }
  case LineupKind: {
    Lineup lineup;
    const auto count = in.get<std::uint8_t>();
    if (count > MaxLineup) {
      return core::fail(std::format("a versus lineup of {} partners", count));
    }
    for (std::uint8_t fighter = 0; fighter < count && in.ok(); ++fighter) {
      lineup.fighters.push_back(getFighter(in));
    }
    message = std::move(lineup);
    break;
  }
  case RoundOverKind: {
    RoundOver over;
    over.round = in.get<std::int32_t>();
    over.result = in.get<std::int32_t>();
    over.checksum = in.get<std::uint64_t>();
    message = over;
    break;
  }
  default: {
    const auto reason = in.get<std::uint8_t>();
    if (reason > LastReason) {
      return core::fail(std::format("an unknown versus leave reason {}", reason));
    }
    message = Leave{.reason = static_cast<LeaveReason>(reason)};
    break;
  }
  }
  if (!in.done()) {
    return core::fail(std::format("a damaged versus message of kind {:#x}", kind));
  }
  return message;
}

} // namespace dwe::game::versus
