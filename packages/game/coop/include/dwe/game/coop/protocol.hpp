#pragma once

#include <dwe/core/error.hpp>
#include <dwe/net/datagram.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace dwe::game::coop {

using PlayerId = std::uint8_t;
inline constexpr PlayerId HostPlayerId = 0;
inline constexpr PlayerId GuestPlayerId = 1;
inline constexpr std::uint32_t ProtocolVersion = 2;

// A worst-case 255-byte map id plus 48 entities fits in one 1200-byte unreliable datagram,
// including ChannelPeerLink's one-byte lane tag. JoinState travels reliably and can carry more.
inline constexpr std::size_t MaxSnapshotEntities = 48;
inline constexpr std::size_t MaxJoinEntities = 128;
inline constexpr std::int32_t MaxWorldCoordinate = 1'000'000;
inline constexpr std::int32_t MaxViewCoordinate = 1'000'000;
inline constexpr std::size_t MaxProfileChunkBytes = 1000;
inline constexpr std::uint16_t MaxProfileChunks = 128;
inline constexpr std::size_t MaxProfileBytes = MaxProfileChunkBytes * MaxProfileChunks;

struct Identity {
  std::uint32_t protocol = ProtocolVersion;
  std::string gameVersion;
  std::string contentHash;

  friend bool operator==(const Identity&, const Identity&) = default;
};

struct Hello {
  Identity identity;
  std::string playerName;

  friend bool operator==(const Hello&, const Hello&) = default;
};

enum class RejectReason : std::uint8_t {
  None,
  ProtocolMismatch,
  VersionMismatch,
  ContentMismatch,
  RoomFull,
  Busy,
};

struct Welcome {
  Identity identity;
  bool accepted = false;
  PlayerId assignedPlayer = GuestPlayerId;
  RejectReason reason = RejectReason::None;
  std::uint64_t sessionId = 0;

  friend bool operator==(const Welcome&, const Welcome&) = default;
};

// The guest's lobby readiness; it never grants authority to select or start the shared world.
struct Ready {
  bool ready = false;

  friend bool operator==(const Ready&, const Ready&) = default;
};

// Input values use signed 15-bit stick coordinates. A click target is in integral world units.
struct FieldCommand {
  std::int16_t stickX = 0;
  std::int16_t stickY = 0;
  bool run = false;
  bool cancelTarget = false;
  bool confirm = false;
  std::optional<std::pair<std::int32_t, std::int32_t>> moveTarget;

  friend bool operator==(const FieldCommand&, const FieldCommand&) = default;
};

struct Commands {
  std::uint32_t sequence = 0;
  FieldCommand field;

  friend bool operator==(const Commands&, const Commands&) = default;
};

enum class Motion : std::uint8_t { Idle, Walk, Run };
enum class EntityRole : std::uint8_t { Player, Partner, Npc };

// Positions are signed integral world units and yaw covers one turn in 16 bits. Quantization is
// part of the protocol so the host and guests do not exchange native floats.
struct EntityState {
  std::uint16_t networkId = 0;
  PlayerId owner = HostPlayerId;
  EntityRole role = EntityRole::Npc;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::uint16_t yaw = 0;
  Motion motion = Motion::Idle;

  friend bool operator==(const EntityState&, const EntityState&) = default;
};

struct Snapshot {
  std::string mapId;
  std::uint32_t worldTick = 0;
  std::uint32_t acknowledgedCommand = 0;
  std::vector<EntityState> entities;

  friend bool operator==(const Snapshot&, const Snapshot&) = default;
};

struct JoinState {
  std::uint32_t formatVersion = 1;
  std::string mapId;
  std::uint32_t worldTick = 0;
  std::uint32_t saveTransferId = 0;
  std::uint16_t saveChunkCount = 0;
  std::vector<EntityState> entities;

  friend bool operator==(const JoinState&, const JoinState&) = default;
};

struct ViewState {
  PlayerId player = GuestPlayerId;
  std::uint32_t sequence = 0;
  std::string mapId;
  std::int32_t left = 0;
  std::int32_t top = 0;
  std::int32_t right = 0;
  std::int32_t bottom = 0;

  friend bool operator==(const ViewState&, const ViewState&) = default;
};

// A reliable, bounded piece of the guest's full save profile. The app assembles the complete save
// and extracts only the visitor data approved by the session rules; this protocol layer does not
// depend on save, partner, session, or field modules.
enum class ProfilePayloadKind : std::uint8_t { GuestSave, HostSave };

struct ProfileChunk {
  std::uint32_t transferId = 0;
  std::uint16_t index = 0;
  std::uint16_t count = 0;
  ProfilePayloadKind kind = ProfilePayloadKind::GuestSave;
  net::Bytes bytes;

  friend bool operator==(const ProfileChunk&, const ProfileChunk&) = default;
};

enum class LeaveReason : std::uint8_t { Quit, Disconnected, SessionEnded };
struct Leave {
  LeaveReason reason = LeaveReason::Quit;

  friend bool operator==(const Leave&, const Leave&) = default;
};

using Message = std::variant<Hello, Welcome, Ready, JoinState, Commands, Snapshot, ViewState, Leave,
                             ProfileChunk>;

inline constexpr std::uint8_t HelloKind = 0x60;
inline constexpr std::uint8_t WelcomeKind = 0x61;
inline constexpr std::uint8_t JoinStateKind = 0x62;
inline constexpr std::uint8_t CommandsKind = 0x63;
inline constexpr std::uint8_t SnapshotKind = 0x66;
inline constexpr std::uint8_t LeaveKind = 0x6A;
inline constexpr std::uint8_t ViewStateKind = 0x6C;
inline constexpr std::uint8_t ProfileChunkKind = 0x6D;
inline constexpr std::uint8_t ReadyKind = 0x6E;

[[nodiscard]] net::Bytes encode(const Message& message);
// Returns empty for a message owned by another protocol. Messages in this range are validated and
// rejected on truncation, trailing bytes, invalid enums, counts, or coordinate bounds.
[[nodiscard]] core::Result<std::optional<Message>> decode(std::span<const std::uint8_t> bytes);

} // namespace dwe::game::coop
