#include <dwe/game/coop/protocol.hpp>
#include <dwe/net/wire.hpp>

#include <format>
#include <type_traits>
#include <unordered_set>

namespace dwe::game::coop {

namespace {

constexpr std::uint8_t LastKind = ReadyKind;

void put(net::WireWriter& out, const Identity& identity) {
  out.put(identity.protocol).put(std::string_view{identity.gameVersion});
  out.put(std::string_view{identity.contentHash});
}

Identity getIdentity(net::WireReader& in) {
  return {.protocol = in.get<std::uint32_t>(), .gameVersion = in.text(), .contentHash = in.text()};
}

void put(net::WireWriter& out, const FieldCommand& field) {
  out.put(field.stickX).put(field.stickY).put(field.run).put(field.cancelTarget);
  out.put(field.confirm).put(field.moveTarget.has_value());
  if (field.moveTarget) {
    out.put(field.moveTarget->first).put(field.moveTarget->second);
  }
}

void put(net::WireWriter& out, const EntityState& entity) {
  out.put(entity.networkId).put(entity.owner).put(entity.role);
  out.put(entity.x).put(entity.y).put(entity.z).put(entity.yaw).put(entity.motion);
}

EntityState readEntity(net::WireReader& in) {
  return {.networkId = in.get<std::uint16_t>(),
          .owner = in.get<PlayerId>(),
          .role = static_cast<EntityRole>(in.get<std::uint8_t>()),
          .x = in.get<std::int32_t>(),
          .y = in.get<std::int32_t>(),
          .z = in.get<std::int32_t>(),
          .yaw = in.get<std::uint16_t>(),
          .motion = static_cast<Motion>(in.get<std::uint8_t>())};
}

bool validIdentity(const Identity& identity) {
  return !identity.gameVersion.empty() && !identity.contentHash.empty();
}

bool validCoordinate(std::int32_t value, std::int32_t limit) {
  return value >= -limit && value <= limit;
}

bool validField(const FieldCommand& field) {
  constexpr std::int16_t MaxStick = 32767;
  return field.stickX >= -MaxStick && field.stickY >= -MaxStick &&
         (!field.moveTarget || (validCoordinate(field.moveTarget->first, MaxWorldCoordinate) &&
                                validCoordinate(field.moveTarget->second, MaxWorldCoordinate)));
}

bool validEntity(const EntityState& entity) {
  return entity.owner <= GuestPlayerId &&
         static_cast<std::uint8_t>(entity.role) <= static_cast<std::uint8_t>(EntityRole::Npc) &&
         static_cast<std::uint8_t>(entity.motion) <= static_cast<std::uint8_t>(Motion::Run) &&
         validCoordinate(entity.x, MaxWorldCoordinate) &&
         validCoordinate(entity.y, MaxWorldCoordinate) &&
         validCoordinate(entity.z, MaxWorldCoordinate);
}

bool getFlag(net::WireReader& in, bool& value) {
  const std::uint8_t byte = in.get<std::uint8_t>();
  if (byte > 1 || !in.ok()) {
    return false;
  }
  value = byte != 0;
  return true;
}

bool getField(net::WireReader& in, FieldCommand& field) {
  field.stickX = in.get<std::int16_t>();
  field.stickY = in.get<std::int16_t>();
  if (!getFlag(in, field.run) || !getFlag(in, field.cancelTarget) || !getFlag(in, field.confirm)) {
    return false;
  }
  bool hasTarget = false;
  if (!getFlag(in, hasTarget)) {
    return false;
  }
  if (hasTarget) {
    field.moveTarget = {in.get<std::int32_t>(), in.get<std::int32_t>()};
  }
  return in.ok() && validField(field);
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
          out.put(std::string_view{body.playerName});
        } else if constexpr (std::is_same_v<T, Welcome>) {
          out.put(WelcomeKind);
          put(out, body.identity);
          out.put(body.accepted).put(body.assignedPlayer).put(body.reason).put(body.sessionId);
          if (body.accepted && !body.hostName.empty()) {
            out.put(std::string_view{body.hostName});
          }
        } else if constexpr (std::is_same_v<T, Ready>) {
          out.put(ReadyKind).put(body.ready);
        } else if constexpr (std::is_same_v<T, JoinState>) {
          out.put(JoinStateKind).put(body.formatVersion).put(std::string_view{body.mapId});
          out.put(body.worldTick).put(body.saveTransferId).put(body.saveChunkCount);
          out.put(static_cast<std::uint16_t>(body.entities.size()));
          for (const EntityState& entity : body.entities) {
            put(out, entity);
          }
        } else if constexpr (std::is_same_v<T, Commands>) {
          out.put(CommandsKind).put(body.sequence);
          put(out, body.field);
        } else if constexpr (std::is_same_v<T, Snapshot>) {
          out.put(SnapshotKind).put(std::string_view{body.mapId});
          out.put(body.worldTick).put(body.acknowledgedCommand);
          out.put(static_cast<std::uint16_t>(body.entities.size()));
          for (const EntityState& entity : body.entities) {
            put(out, entity);
          }
        } else if constexpr (std::is_same_v<T, ViewState>) {
          out.put(ViewStateKind).put(body.player).put(body.sequence);
          out.put(std::string_view{body.mapId});
          out.put(body.left).put(body.top).put(body.right).put(body.bottom);
        } else if constexpr (std::is_same_v<T, Leave>) {
          out.put(LeaveKind).put(body.reason);
        } else {
          out.put(ProfileChunkKind)
              .put(body.transferId)
              .put(body.index)
              .put(body.count)
              .put(body.kind);
          out.put(std::span<const std::uint8_t>{body.bytes});
        }
      },
      message);
  return out.take();
}

core::Result<std::optional<Message>> decode(std::span<const std::uint8_t> bytes) {
  if (bytes.empty() || bytes.front() < HelloKind || bytes.front() > LastKind) {
    return std::optional<Message>{};
  }
  net::WireReader in{bytes};
  const std::uint8_t kind = in.get<std::uint8_t>();
  std::optional<Message> message;
  switch (kind) {
  case HelloKind: {
    Hello hello{.identity = getIdentity(in), .playerName = in.text()};
    if (!validIdentity(hello.identity) || hello.playerName.empty()) {
      return core::fail("invalid cooperative hello identity or player name");
    }
    message = std::move(hello);
    break;
  }
  case WelcomeKind: {
    Welcome welcome;
    welcome.identity = getIdentity(in);
    if (!getFlag(in, welcome.accepted)) {
      return core::fail("invalid cooperative welcome flag");
    }
    welcome.assignedPlayer = in.get<PlayerId>();
    const std::uint8_t reason = in.get<std::uint8_t>();
    welcome.sessionId = in.get<std::uint64_t>();
    if (!validIdentity(welcome.identity) ||
        reason > static_cast<std::uint8_t>(RejectReason::Busy) ||
        welcome.assignedPlayer != GuestPlayerId ||
        (welcome.accepted && reason != static_cast<std::uint8_t>(RejectReason::None)) ||
        (!welcome.accepted && reason == static_cast<std::uint8_t>(RejectReason::None))) {
      return core::fail("invalid cooperative welcome");
    }
    welcome.reason = static_cast<RejectReason>(reason);
    if (welcome.accepted && in.ok() && !in.done()) {
      welcome.hostName = in.text();
    }
    message = welcome;
    break;
  }
  case ReadyKind: {
    bool ready = false;
    if (!getFlag(in, ready)) {
      return core::fail("invalid cooperative ready state");
    }
    message = Ready{.ready = ready};
    break;
  }
  case JoinStateKind: {
    JoinState state;
    state.formatVersion = in.get<std::uint32_t>();
    state.mapId = in.text();
    state.worldTick = in.get<std::uint32_t>();
    state.saveTransferId = in.get<std::uint32_t>();
    state.saveChunkCount = in.get<std::uint16_t>();
    const std::uint16_t count = in.get<std::uint16_t>();
    if (count > MaxJoinEntities) {
      return core::fail(std::format("cooperative join state has {} entities", count));
    }
    std::unordered_set<std::uint16_t> ids;
    for (std::uint16_t i = 0; i < count && in.ok(); ++i) {
      EntityState entity = readEntity(in);
      if (!validEntity(entity) || !ids.insert(entity.networkId).second) {
        return core::fail("invalid or duplicate entity in cooperative join state");
      }
      state.entities.push_back(entity);
    }
    if (state.formatVersion != 1 || state.mapId.empty() || state.saveTransferId == 0 ||
        state.saveChunkCount == 0 || state.saveChunkCount > MaxProfileChunks) {
      return core::fail("invalid cooperative join state metadata");
    }
    message = std::move(state);
    break;
  }
  case CommandsKind: {
    Commands commands;
    commands.sequence = in.get<std::uint32_t>();
    if (!getField(in, commands.field)) {
      return core::fail("invalid cooperative command");
    }
    message = commands;
    break;
  }
  case SnapshotKind: {
    Snapshot snapshot;
    snapshot.mapId = in.text();
    snapshot.worldTick = in.get<std::uint32_t>();
    snapshot.acknowledgedCommand = in.get<std::uint32_t>();
    const std::uint16_t count = in.get<std::uint16_t>();
    if (count > MaxSnapshotEntities) {
      return core::fail(std::format("cooperative snapshot has {} entities", count));
    }
    std::unordered_set<std::uint16_t> ids;
    for (std::uint16_t i = 0; i < count && in.ok(); ++i) {
      EntityState entity = readEntity(in);
      if (!validEntity(entity) || !ids.insert(entity.networkId).second) {
        return core::fail("invalid or duplicate entity in cooperative snapshot");
      }
      snapshot.entities.push_back(entity);
    }
    if (snapshot.mapId.empty()) {
      return core::fail("cooperative snapshot has no map id");
    }
    if (bytes.size() + 1 > net::MaxDatagram) {
      return core::fail("cooperative snapshot exceeds the unreliable datagram limit");
    }
    message = std::move(snapshot);
    break;
  }
  case ViewStateKind: {
    ViewState view;
    view.player = in.get<PlayerId>();
    view.sequence = in.get<std::uint32_t>();
    view.mapId = in.text();
    view.left = in.get<std::int32_t>();
    view.top = in.get<std::int32_t>();
    view.right = in.get<std::int32_t>();
    view.bottom = in.get<std::int32_t>();
    if (view.player > GuestPlayerId || view.mapId.empty() || view.left >= view.right ||
        view.top >= view.bottom || !validCoordinate(view.left, MaxViewCoordinate) ||
        !validCoordinate(view.top, MaxViewCoordinate) ||
        !validCoordinate(view.right, MaxViewCoordinate) ||
        !validCoordinate(view.bottom, MaxViewCoordinate) ||
        view.right - view.left > MaxViewCoordinate || view.bottom - view.top > MaxViewCoordinate) {
      return core::fail("invalid cooperative view state");
    }
    message = std::move(view);
    break;
  }
  case ProfileChunkKind: {
    ProfileChunk chunk;
    chunk.transferId = in.get<std::uint32_t>();
    chunk.index = in.get<std::uint16_t>();
    chunk.count = in.get<std::uint16_t>();
    chunk.kind = static_cast<ProfilePayloadKind>(in.get<std::uint8_t>());
    const std::span<const std::uint8_t> payload = in.rest();
    chunk.bytes.assign(payload.begin(), payload.end());
    if (chunk.transferId == 0 || chunk.count == 0 || chunk.count > MaxProfileChunks ||
        chunk.index >= chunk.count ||
        static_cast<std::uint8_t>(chunk.kind) >
            static_cast<std::uint8_t>(ProfilePayloadKind::HostSave) ||
        chunk.bytes.empty() || chunk.bytes.size() > MaxProfileChunkBytes) {
      return core::fail("invalid cooperative profile chunk");
    }
    message = std::move(chunk);
    break;
  }
  case LeaveKind: {
    const std::uint8_t reason = in.get<std::uint8_t>();
    if (reason > static_cast<std::uint8_t>(LeaveReason::SessionEnded)) {
      return core::fail("unknown cooperative leave reason");
    }
    message = Leave{.reason = static_cast<LeaveReason>(reason)};
    break;
  }
  default:
    return std::optional<Message>{};
  }
  if (!in.done()) {
    return core::fail(std::format("damaged cooperative message of kind {:#x}", kind));
  }
  return message;
}

} // namespace dwe::game::coop
