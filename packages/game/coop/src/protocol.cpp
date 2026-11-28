#include <dwe/game/coop/protocol.hpp>
#include <dwe/net/wire.hpp>

#include <algorithm>
#include <format>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace dwe::game::coop {

namespace {

// The highest kind: the ranges below it that hold no kind decode as another protocol's.
constexpr std::uint8_t LastKind = EventStatusKind;
// A motion byte with this bit set is followed by the character's scripted clip (ActionClip).
constexpr std::uint8_t ActionBit = 0x80;

// A kind this protocol may decode: any byte outside is another protocol's (versus, lockstep).
constexpr bool coopKind(std::uint8_t kind) {
  return kind >= HelloKind && kind <= LastKind;
}
// A roster name or partner species longer than this is damaged.
constexpr std::size_t MaxRosterText = 64;

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

void putMotion(net::WireWriter& out, Motion motion, const std::optional<ActionClip>& action) {
  const auto byte = static_cast<std::uint8_t>(static_cast<std::uint8_t>(motion) |
                                              (action ? ActionBit : std::uint8_t{0}));
  out.put(byte);
  if (action) {
    out.put(action->clip).put(action->starts);
  }
}

// The motion byte and the clip it may carry; false for a motion this protocol does not know.
bool getMotion(net::WireReader& in, Motion& motion, std::optional<ActionClip>& action) {
  const std::uint8_t byte = in.get<std::uint8_t>();
  const auto value = static_cast<std::uint8_t>(byte & ~ActionBit);
  if ((byte & ActionBit) != 0) {
    action = ActionClip{.clip = in.get<std::uint16_t>(), .starts = in.get<std::uint8_t>()};
  }
  motion = static_cast<Motion>(value);
  return value <= static_cast<std::uint8_t>(Motion::Run);
}

void put(net::WireWriter& out, const EntityState& entity) {
  out.put(entity.networkId).put(entity.owner).put(entity.role);
  out.put(entity.x).put(entity.y).put(entity.z).put(entity.yaw);
  putMotion(out, entity.motion, entity.action);
}

std::optional<EntityState> readEntity(net::WireReader& in) {
  EntityState entity{.networkId = in.get<std::uint16_t>(),
                     .owner = in.get<PlayerId>(),
                     .role = static_cast<EntityRole>(in.get<std::uint8_t>()),
                     .x = in.get<std::int32_t>(),
                     .y = in.get<std::int32_t>(),
                     .z = in.get<std::int32_t>(),
                     .yaw = in.get<std::uint16_t>()};
  if (!getMotion(in, entity.motion, entity.action)) {
    return std::nullopt;
  }
  return entity;
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
  return validPlayer(entity.owner) &&
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

// Battles (protocol.hpp: BattleAsk and the others).
constexpr std::size_t MaxBattleText = 64;
constexpr std::int8_t MaxEquippedSlot = 15;
constexpr std::uint8_t LastBattleCommand = 11;
constexpr std::uint8_t LastMenuEntry = 8;
constexpr std::uint8_t LastDeparture = 2;

void putTexts(net::WireWriter& out, const std::vector<std::string>& texts) {
  out.put(static_cast<std::uint8_t>(texts.size()));
  for (const std::string& text : texts) {
    out.put(std::string_view{text});
  }
}

bool getTexts(net::WireReader& in, std::vector<std::string>& texts, std::size_t limit) {
  const std::uint8_t count = in.get<std::uint8_t>();
  if (!in.ok() || count > limit) {
    return false;
  }
  for (std::uint8_t i = 0; i < count && in.ok(); ++i) {
    std::string text = in.text();
    if (text.empty() || text.size() > MaxBattleText) {
      return false;
    }
    texts.push_back(std::move(text));
  }
  return in.ok();
}

// Item trades (protocol.hpp: TradeAsk and the others).
constexpr std::size_t MaxTradeItemText = 64;

void putTradeItems(net::WireWriter& out, const std::vector<TradeItem>& items) {
  out.put(static_cast<std::uint8_t>(items.size()));
  for (const TradeItem& item : items) {
    out.put(std::string_view{item.item}).put(item.count);
  }
}

// A list of items with no id twice, each with 1 to MaxTradeCount.
bool getTradeItems(net::WireReader& in, std::vector<TradeItem>& items) {
  const std::uint8_t count = in.get<std::uint8_t>();
  if (!in.ok() || count > MaxTradeItems) {
    return false;
  }
  std::unordered_set<std::string> ids;
  for (std::uint8_t i = 0; i < count && in.ok(); ++i) {
    TradeItem item{.item = in.text(), .count = in.get<std::uint16_t>()};
    if (item.item.empty() || item.item.size() > MaxTradeItemText || item.count == 0 ||
        item.count > MaxTradeCount || !ids.insert(item.item).second) {
      return false;
    }
    items.push_back(std::move(item));
  }
  return in.ok();
}

void putTradePlayers(net::WireWriter& out, const std::array<PlayerId, 2>& players) {
  out.put(players[0]).put(players[1]);
}

void putSlots(net::WireWriter& out, const std::vector<std::int16_t>& slots) {
  out.put(static_cast<std::uint8_t>(slots.size()));
  for (const std::int16_t slot : slots) {
    out.put(slot);
  }
}

bool getSlots(net::WireReader& in, std::vector<std::int16_t>& slots, std::size_t limit) {
  const std::uint8_t count = in.get<std::uint8_t>();
  if (!in.ok() || count > limit) {
    return false;
  }
  for (std::uint8_t i = 0; i < count && in.ok(); ++i) {
    const auto slot = in.get<std::int16_t>();
    if (slot < 0) {
      return false;
    }
    slots.push_back(slot);
  }
  return in.ok();
}

template <std::size_t N>
void putInts(net::WireWriter& out, const std::array<std::int32_t, N>& values) {
  for (const std::int32_t value : values) {
    out.put(value);
  }
}

template <std::size_t N>
void getInts(net::WireReader& in, std::array<std::int32_t, N>& values) {
  for (std::int32_t& value : values) {
    value = in.get<std::int32_t>();
  }
}

void put(net::WireWriter& out, const BattleFighterProfile& fighter) {
  out.put(std::string_view{fighter.species});
  putInts(out, fighter.stats);
  out.put(fighter.hp).put(fighter.mp).put(fighter.chargeMode);
  for (const std::int8_t slot : fighter.equipped) {
    out.put(slot);
  }
  out.put(fighter.discipline).put(fighter.tiredness).put(fighter.sick).put(fighter.injured);
  putTexts(out, fighter.knownMoves);
}

bool getFighter(net::WireReader& in, BattleFighterProfile& fighter) {
  fighter.species = in.text();
  getInts(in, fighter.stats);
  fighter.hp = in.get<std::int32_t>();
  fighter.mp = in.get<std::int32_t>();
  fighter.chargeMode = in.get<std::uint8_t>();
  for (std::int8_t& slot : fighter.equipped) {
    slot = in.get<std::int8_t>();
  }
  fighter.discipline = in.get<std::int32_t>();
  fighter.tiredness = in.get<std::int32_t>();
  if (!getFlag(in, fighter.sick) || !getFlag(in, fighter.injured) ||
      !getTexts(in, fighter.knownMoves, MaxBattleMoves)) {
    return false;
  }
  return !fighter.species.empty() && fighter.species.size() <= MaxBattleText &&
         fighter.chargeMode <= 2 && fighter.hp >= 0 && fighter.mp >= 0 &&
         std::ranges::all_of(fighter.stats, [](std::int32_t stat) { return stat >= 0; }) &&
         std::ranges::all_of(fighter.equipped, [](std::int8_t slot) {
           return slot >= -1 && slot <= MaxEquippedSlot;
         });
}

void put(net::WireWriter& out, const BattleFigure& figure) {
  out.put(figure.slot).put(figure.x).put(figure.z).put(figure.yaw).put(figure.clip);
  out.put(figure.starts).put(figure.frozen).put(figure.pace);
}

bool getFigure(net::WireReader& in, BattleFigure& figure) {
  figure.slot = in.get<std::int16_t>();
  figure.x = in.get<std::int32_t>();
  figure.z = in.get<std::int32_t>();
  figure.yaw = in.get<std::uint16_t>();
  figure.clip = in.get<std::uint16_t>();
  figure.starts = in.get<std::uint32_t>();
  if (!getFlag(in, figure.frozen)) {
    return false;
  }
  figure.pace = in.get<std::uint8_t>();
  return in.ok() && figure.slot >= LowestFigureSlot &&
         validCoordinate(figure.x, MaxWorldCoordinate) &&
         validCoordinate(figure.z, MaxWorldCoordinate);
}

void put(net::WireWriter& out, const BattleThrow& thrown) {
  out.put(thrown.serial).put(thrown.thrower).put(std::string_view{thrown.item}).put(thrown.stage);
  out.put(thrown.x).put(thrown.z).put(thrown.height).put(thrown.target);
}

bool getThrow(net::WireReader& in, BattleThrow& thrown) {
  thrown.serial = in.get<std::uint16_t>();
  thrown.thrower = in.get<PlayerId>();
  thrown.item = in.text();
  const auto stage = in.get<std::uint8_t>();
  thrown.x = in.get<std::int32_t>();
  thrown.z = in.get<std::int32_t>();
  thrown.height = in.get<std::int32_t>();
  thrown.target = in.get<std::uint8_t>();
  if (!in.ok() || stage > static_cast<std::uint8_t>(BattleThrowStage::Landed)) {
    return false;
  }
  thrown.stage = static_cast<BattleThrowStage>(stage);
  return validPlayer(thrown.thrower) && !thrown.item.empty() &&
         thrown.item.size() <= MaxBattleText && thrown.target < MaxBattleFighters &&
         validCoordinate(thrown.x, MaxWorldCoordinate) &&
         validCoordinate(thrown.z, MaxWorldCoordinate) &&
         validCoordinate(thrown.height, MaxWorldCoordinate);
}

void put(net::WireWriter& out, const BattleSpoils& spoils) {
  out.put(spoils.hp).put(spoils.mp);
  putInts(out, spoils.statGains);
  putTexts(out, spoils.learnedMoves);
  out.put(spoils.injured);
  putInts(out, spoils.care);
  out.put(spoils.food).put(spoils.livesLost).put(spoils.elapsedMinutes).put(spoils.hourCrossed);
  out.put(spoils.lifetimeHoursLost).put(spoils.departure);
  out.put(static_cast<std::uint8_t>(spoils.commandsLearned.size()));
  for (const std::uint8_t entry : spoils.commandsLearned) {
    out.put(entry);
  }
  out.put(spoils.mpDiscountLearned);
  putInts(out, spoils.counters);
  out.put(spoils.anyoneHit).put(spoils.partnerHit).put(spoils.frames);
}

bool getSpoils(net::WireReader& in, BattleSpoils& spoils) {
  spoils.hp = in.get<std::int32_t>();
  spoils.mp = in.get<std::int32_t>();
  getInts(in, spoils.statGains);
  if (!getTexts(in, spoils.learnedMoves, MaxBattleMoves) || !getFlag(in, spoils.injured)) {
    return false;
  }
  getInts(in, spoils.care);
  spoils.food = in.get<std::int32_t>();
  spoils.livesLost = in.get<std::int32_t>();
  spoils.elapsedMinutes = in.get<std::int32_t>();
  if (!getFlag(in, spoils.hourCrossed)) {
    return false;
  }
  spoils.lifetimeHoursLost = in.get<std::int32_t>();
  spoils.departure = in.get<std::uint8_t>();
  const std::uint8_t entries = in.get<std::uint8_t>();
  if (!in.ok() || entries > LastMenuEntry + 1 || spoils.departure > LastDeparture) {
    return false;
  }
  for (std::uint8_t i = 0; i < entries && in.ok(); ++i) {
    const auto entry = in.get<std::uint8_t>();
    if (entry > LastMenuEntry) {
      return false;
    }
    spoils.commandsLearned.push_back(entry);
  }
  spoils.mpDiscountLearned = in.get<std::int32_t>();
  getInts(in, spoils.counters);
  if (!getFlag(in, spoils.anyoneHit) || !getFlag(in, spoils.partnerHit)) {
    return false;
  }
  spoils.frames = in.get<std::int64_t>();
  return in.ok() && spoils.hp >= 0 && spoils.mp >= 0 && spoils.frames >= 0;
}

void putEdits(net::WireWriter& out, const std::vector<WorldEdit>& edits) {
  out.put(static_cast<std::uint8_t>(edits.size()));
  for (const WorldEdit& edit : edits) {
    out.put(edit.kind).put(edit.key).put(edit.value);
    if (edit.kind == WorldEditKind::MapBlock) {
      out.put(edit.width).put(edit.height);
    }
  }
}

bool getEdits(net::WireReader& in, std::vector<WorldEdit>& edits) {
  const std::uint8_t count = in.get<std::uint8_t>();
  if (count > MaxWorldEdits) {
    return false;
  }
  for (std::uint8_t i = 0; i < count && in.ok(); ++i) {
    const std::uint8_t kind = in.get<std::uint8_t>();
    if (kind > static_cast<std::uint8_t>(WorldEditKind::MapBlock)) {
      return false;
    }
    WorldEdit edit{.kind = static_cast<WorldEditKind>(kind),
                   .key = in.get<std::int32_t>(),
                   .value = in.get<std::int32_t>()};
    if (edit.kind == WorldEditKind::MapBlock) {
      edit.width = in.get<std::int16_t>();
      edit.height = in.get<std::int16_t>();
    }
    const bool valid = edit.kind == WorldEditKind::Trigger
                           ? edit.key >= 0 && (edit.value == 0 || edit.value == 1)
                       : edit.kind == WorldEditKind::MapBlock ? edit.width > 0 && edit.height > 0
                                                              : edit.key >= 0;
    if (!valid) {
      return false;
    }
    edits.push_back(edit);
  }
  return in.ok();
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
          if (body.rejoinKey != 0) {
            out.put(body.rejoinKey);
          }
        } else if constexpr (std::is_same_v<T, Welcome>) {
          out.put(WelcomeKind);
          put(out, body.identity);
          out.put(body.accepted).put(body.assignedPlayer).put(body.reason).put(body.sessionId);
          if (body.accepted && (!body.hostName.empty() || body.seatKey != 0)) {
            out.put(std::string_view{body.hostName});
          }
          if (body.accepted && body.seatKey != 0) {
            out.put(body.seatKey);
          }
        } else if constexpr (std::is_same_v<T, Ready>) {
          out.put(ReadyKind).put(body.ready);
        } else if constexpr (std::is_same_v<T, Lobby>) {
          out.put(LobbyKind).put(body.capacity);
          out.put(static_cast<std::uint8_t>(body.seats.size()));
          for (const LobbySeat& seat : body.seats) {
            out.put(seat.id).put(std::string_view{seat.name}).put(seat.state);
          }
          out.put(body.playing).put(body.open).put(body.difficulty);
        } else if constexpr (std::is_same_v<T, SyncWatch>) {
          out.put(SyncWatchKind).put(body.on);
        } else if constexpr (std::is_same_v<T, SyncReport>) {
          out.put(SyncReportKind).put(body.sequence).put(body.snapshotTick);
          out.put(body.snapshotsLost).put(body.snapshotsLate);
          out.put(static_cast<std::uint8_t>(body.players.size()));
          for (const SyncHeld& held : body.players) {
            out.put(held.id).put(std::string_view{held.mapId});
            out.put(held.x).put(held.y).put(held.z);
          }
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
        } else if constexpr (std::is_same_v<T, PlayerState>) {
          out.put(PlayerStateKind).put(body.player).put(body.sequence).put(body.epoch);
          out.put(std::string_view{body.mapId});
          out.put(body.x).put(body.y).put(body.z).put(body.yaw);
          putMotion(out, body.motion, body.action);
          out.put(body.partner.has_value());
          if (body.partner) {
            out.put(body.partner->x).put(body.partner->y).put(body.partner->z);
            out.put(body.partner->yaw);
            putMotion(out, body.partner->motion, body.partner->action);
          }
        } else if constexpr (std::is_same_v<T, PlayerCorrection>) {
          out.put(PlayerCorrectionKind).put(body.player).put(body.epoch);
          out.put(std::string_view{body.mapId}).put(body.exit.has_value());
          if (body.exit) {
            out.put(*body.exit);
          }
          out.put(body.x).put(body.y).put(body.z).put(body.yaw);
        } else if constexpr (std::is_same_v<T, Roster>) {
          out.put(RosterKind).put(body.revision);
          out.put(static_cast<std::uint8_t>(body.players.size()));
          for (const RosterEntry& entry : body.players) {
            out.put(entry.id).put(std::string_view{entry.name});
            out.put(std::string_view{entry.partnerSpecies});
          }
        } else if constexpr (std::is_same_v<T, PartnerForm>) {
          out.put(PartnerFormKind).put(body.player).put(std::string_view{body.species});
        } else if constexpr (std::is_same_v<T, WorldAsk>) {
          out.put(WorldAskKind).put(body.ask).put(body.seen).put(std::string_view{body.mapId});
          putEdits(out, body.edits);
        } else if constexpr (std::is_same_v<T, WorldChange>) {
          out.put(WorldChangeKind).put(body.revision).put(body.origin).put(body.ask);
          out.put(std::string_view{body.mapId});
          putEdits(out, body.edits);
        } else if constexpr (std::is_same_v<T, WorldSync>) {
          out.put(WorldSyncKind).put(body.revision).put(body.part).put(body.parts);
          out.put(body.firstTrigger).put(static_cast<std::uint16_t>(body.triggers.size()));
          std::uint8_t bits = 0;
          for (std::size_t i = 0; i < body.triggers.size(); ++i) {
            if (body.triggers[i]) {
              bits = static_cast<std::uint8_t>(bits | (1U << (i % 8)));
            }
            if (i % 8 == 7 || i + 1 == body.triggers.size()) {
              out.put(bits);
              bits = 0;
            }
          }
          out.put(body.firstVariable).put(static_cast<std::uint16_t>(body.variables.size()));
          for (const std::int32_t value : body.variables) {
            out.put(value);
          }
        } else if constexpr (std::is_same_v<T, GroundItems>) {
          out.put(GroundItemsKind).put(std::string_view{body.mapId}).put(body.mapSerial);
          out.put(static_cast<std::uint8_t>(body.items.size()));
          for (const GroundEntry& entry : body.items) {
            out.put(entry.id).put(entry.slot).put(std::string_view{entry.item});
            out.put(entry.column).put(entry.row).put(entry.x).put(entry.y).put(entry.z);
          }
        } else if constexpr (std::is_same_v<T, ItemClaim>) {
          out.put(ItemClaimKind).put(body.claim).put(body.kind).put(body.key).put(body.id);
          out.put(std::string_view{body.mapId});
        } else if constexpr (std::is_same_v<T, ClaimAnswer>) {
          out.put(ClaimAnswerKind).put(body.claim).put(body.granted);
        } else if constexpr (std::is_same_v<T, PoopPiles>) {
          out.put(PoopPilesKind).put(std::string_view{body.mapId}).put(body.restart);
          out.put(static_cast<std::uint8_t>(body.piles.size()));
          for (const PileEntry& pile : body.piles) {
            out.put(pile.x).put(pile.y).put(pile.size);
          }
        } else if constexpr (std::is_same_v<T, PileAsk>) {
          out.put(PileAskKind).put(body.ask).put(body.action).put(std::string_view{body.mapId});
          out.put(body.x).put(body.y).put(body.size);
        } else if constexpr (std::is_same_v<T, PileAnswer>) {
          out.put(PileAnswerKind).put(body.ask).put(body.granted).put(body.size);
        } else if constexpr (std::is_same_v<T, Leave>) {
          out.put(LeaveKind).put(body.reason);
        } else if constexpr (std::is_same_v<T, BattleAsk>) {
          out.put(BattleAskKind).put(body.ask).put(std::string_view{body.mapId});
          putSlots(out, body.enemyNpcs);
          out.put(body.fixedFight).put(body.predefined);
          put(out, body.fighter);
        } else if constexpr (std::is_same_v<T, BattleBegin>) {
          out.put(BattleBeginKind).put(body.battle).put(body.starter).put(body.ask);
          out.put(std::string_view{body.mapId});
          putSlots(out, body.enemyNpcs);
          putTexts(out, body.species);
          out.put(body.fixedFight);
          out.put(static_cast<std::uint8_t>(body.allies.size()));
          for (const PlayerId ally : body.allies) {
            out.put(ally);
          }
          out.put(body.folder);
        } else if constexpr (std::is_same_v<T, BattleOffer>) {
          out.put(BattleOfferKind).put(body.available);
          put(out, body.fighter);
        } else if constexpr (std::is_same_v<T, BattleRefused>) {
          out.put(BattleRefusedKind).put(body.ask).put(body.reason);
        } else if constexpr (std::is_same_v<T, BattleFrame>) {
          out.put(BattleFrameKind).put(body.battle).put(body.frame).put(body.phase);
          out.put(body.command).put(body.menu).put(body.bagAvailable).put(body.finisherCharging);
          out.put(static_cast<std::uint8_t>(body.fighters.size()));
          for (const BattleFighterState& fighter : body.fighters) {
            out.put(fighter.hp).put(fighter.mp).put(fighter.maxHp).put(fighter.maxMp);
            out.put(fighter.down).put(fighter.reviveSeconds);
          }
          out.put(static_cast<std::uint8_t>(body.figures.size()));
          for (const BattleFigure& figure : body.figures) {
            put(out, figure);
          }
          out.put(static_cast<std::uint8_t>(body.throws.size()));
          for (const BattleThrow& thrown : body.throws) {
            put(out, thrown);
          }
        } else if constexpr (std::is_same_v<T, BattleAction>) {
          out.put(BattleActionMessageKind).put(body.battle).put(body.kind).put(body.value);
          out.put(std::string_view{body.item});
        } else if constexpr (std::is_same_v<T, BattleEnd>) {
          out.put(BattleEndKind).put(body.battle).put(body.outcome);
          putSlots(out, body.leftMap);
          out.put(body.bits).put(body.spoils.has_value());
          if (body.spoils) {
            put(out, *body.spoils);
          }
        } else if constexpr (std::is_same_v<T, TradeAsk>) {
          out.put(TradeAskKind).put(body.with);
        } else if constexpr (std::is_same_v<T, TradeAnswer>) {
          out.put(TradeAnswerKind).put(body.trade).put(body.accept);
        } else if constexpr (std::is_same_v<T, TradeOffer>) {
          out.put(TradeOfferKind).put(body.trade);
          putTradeItems(out, body.items);
        } else if constexpr (std::is_same_v<T, TradeConfirm>) {
          out.put(TradeConfirmKind).put(body.trade).put(body.revision).put(body.confirmed);
        } else if constexpr (std::is_same_v<T, TradeCancel>) {
          out.put(TradeCancelKind).put(body.trade);
        } else if constexpr (std::is_same_v<T, TradeState>) {
          out.put(TradeStateKind).put(body.trade);
          putTradePlayers(out, body.players);
          out.put(body.phase).put(body.revision);
          putTradeItems(out, body.offers[0]);
          putTradeItems(out, body.offers[1]);
          out.put(body.confirmed[0]).put(body.confirmed[1]);
        } else if constexpr (std::is_same_v<T, TradePrepare>) {
          out.put(TradePrepareKind).put(body.trade);
          putTradeItems(out, body.give);
          putTradeItems(out, body.get);
        } else if constexpr (std::is_same_v<T, TradeCommit>) {
          out.put(TradeCommitKind).put(body.trade);
        } else if constexpr (std::is_same_v<T, TradeRevert>) {
          out.put(TradeRevertKind).put(body.trade);
        } else if constexpr (std::is_same_v<T, TradeReply>) {
          out.put(TradeReplyKind).put(body.trade).put(body.step).put(body.check);
        } else if constexpr (std::is_same_v<T, TradeClosed>) {
          out.put(TradeClosedKind).put(body.trade).put(body.outcome).put(body.by);
        } else if constexpr (std::is_same_v<T, WorldClock>) {
          out.put(WorldClockKind).put(body.elapsedFrames).put(body.running);
          out.put(body.speedThousandths);
        } else if constexpr (std::is_same_v<T, TrainingStatus>) {
          out.put(TrainingStatusKind).put(body.player).put(std::string_view{body.course});
        } else if constexpr (std::is_same_v<T, EventStatus>) {
          out.put(EventStatusKind).put(body.player).put(std::string_view{body.mapId}).put(body.npc);
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
  if (bytes.empty() || !coopKind(bytes.front())) {
    return std::optional<Message>{};
  }
  net::WireReader in{bytes};
  const std::uint8_t kind = in.get<std::uint8_t>();
  std::optional<Message> message;
  switch (kind) {
  case HelloKind: {
    Hello hello{.identity = getIdentity(in), .playerName = in.text(), .rejoinKey = 0};
    if (in.ok() && !in.done()) {
      hello.rejoinKey = in.get<std::uint64_t>();
    }
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
        reason > static_cast<std::uint8_t>(RejectReason::Closed) ||
        (welcome.accepted && !validGuest(welcome.assignedPlayer)) ||
        (welcome.accepted && reason != static_cast<std::uint8_t>(RejectReason::None)) ||
        (!welcome.accepted && reason == static_cast<std::uint8_t>(RejectReason::None))) {
      return core::fail("invalid cooperative welcome");
    }
    welcome.reason = static_cast<RejectReason>(reason);
    if (welcome.accepted && in.ok() && !in.done()) {
      welcome.hostName = in.text();
    }
    if (welcome.accepted && in.ok() && !in.done()) {
      welcome.seatKey = in.get<std::uint64_t>();
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
      const std::optional<EntityState> entity = readEntity(in);
      if (!entity || !validEntity(*entity) || !ids.insert(entity->networkId).second) {
        return core::fail("invalid or duplicate entity in cooperative join state");
      }
      state.entities.push_back(*entity);
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
      const std::optional<EntityState> entity = readEntity(in);
      if (!entity || !validEntity(*entity) || !ids.insert(entity->networkId).second) {
        return core::fail("invalid or duplicate entity in cooperative snapshot");
      }
      snapshot.entities.push_back(*entity);
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
    if (!validPlayer(view.player) || view.mapId.empty() || view.left >= view.right ||
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
  case BattleAskKind: {
    BattleAsk ask;
    ask.ask = in.get<std::uint32_t>();
    ask.mapId = in.text();
    if (!getSlots(in, ask.enemyNpcs, MaxBattleEnemies) || !getFlag(in, ask.fixedFight) ||
        !getFlag(in, ask.predefined) || !getFighter(in, ask.fighter) || ask.ask == 0 ||
        ask.mapId.empty() || ask.enemyNpcs.empty()) {
      return core::fail("invalid cooperative battle ask");
    }
    message = std::move(ask);
    break;
  }
  case BattleBeginKind: {
    BattleBegin begin;
    begin.battle = in.get<std::uint32_t>();
    begin.starter = in.get<PlayerId>();
    begin.ask = in.get<std::uint32_t>();
    begin.mapId = in.text();
    if (!getSlots(in, begin.enemyNpcs, MaxBattleEnemies) ||
        !getTexts(in, begin.species, MaxBattleFighters) || !getFlag(in, begin.fixedFight)) {
      return core::fail("invalid cooperative battle begin");
    }
    const std::uint8_t allies = in.get<std::uint8_t>();
    for (std::uint8_t i = 0; i < allies && in.ok() && allies < MaxPlayers; ++i) {
      begin.allies.push_back(in.get<PlayerId>());
    }
    if (!getFlag(in, begin.folder) || !in.ok() || allies >= MaxPlayers || begin.battle == 0 ||
        !validPlayer(begin.starter) || begin.mapId.empty() || begin.enemyNpcs.empty() ||
        begin.species.size() != begin.enemyNpcs.size() + 1 + begin.allies.size() ||
        (begin.starter != HostPlayerId) != (begin.ask != 0) ||
        !std::ranges::all_of(begin.allies, [&](PlayerId ally) {
          return validPlayer(ally) && ally != begin.starter;
        })) {
      return core::fail("invalid cooperative battle begin");
    }
    message = std::move(begin);
    break;
  }
  case BattleOfferKind: {
    BattleOffer offer;
    if (!getFlag(in, offer.available) || !getFighter(in, offer.fighter)) {
      return core::fail("invalid cooperative battle offer");
    }
    message = std::move(offer);
    break;
  }
  case BattleRefusedKind: {
    BattleRefused refused;
    refused.ask = in.get<std::uint32_t>();
    const auto reason = in.get<std::uint8_t>();
    if (!in.ok() || refused.ask == 0 || reason > static_cast<std::uint8_t>(BattleRefusal::Failed)) {
      return core::fail("invalid cooperative battle refusal");
    }
    refused.reason = static_cast<BattleRefusal>(reason);
    message = refused;
    break;
  }
  case BattleFrameKind: {
    BattleFrame frame;
    frame.battle = in.get<std::uint32_t>();
    frame.frame = in.get<std::uint32_t>();
    const auto phase = in.get<std::uint8_t>();
    frame.command = in.get<std::uint8_t>();
    frame.menu = in.get<std::uint16_t>();
    if (!getFlag(in, frame.bagAvailable) || !getFlag(in, frame.finisherCharging) ||
        frame.battle == 0 || phase > static_cast<std::uint8_t>(BattlePhase::Closing) ||
        frame.command > LastBattleCommand) {
      return core::fail("invalid cooperative battle frame");
    }
    frame.phase = static_cast<BattlePhase>(phase);
    const std::uint8_t fighters = in.get<std::uint8_t>();
    if (!in.ok() || fighters > MaxBattleFighters) {
      return core::fail("invalid cooperative battle frame fighters");
    }
    for (std::uint8_t i = 0; i < fighters && in.ok(); ++i) {
      BattleFighterState fighter{.hp = in.get<std::int32_t>(),
                                 .mp = in.get<std::int32_t>(),
                                 .maxHp = in.get<std::int32_t>(),
                                 .maxMp = in.get<std::int32_t>()};
      if (!getFlag(in, fighter.down)) {
        return core::fail("invalid cooperative battle fighter");
      }
      fighter.reviveSeconds = in.get<std::uint8_t>();
      frame.fighters.push_back(fighter);
    }
    const std::uint8_t figures = in.get<std::uint8_t>();
    if (!in.ok() || figures > MaxBattleFigures) {
      return core::fail("invalid cooperative battle frame figures");
    }
    for (std::uint8_t i = 0; i < figures && in.ok(); ++i) {
      BattleFigure figure;
      if (!getFigure(in, figure)) {
        return core::fail("invalid cooperative battle figure");
      }
      frame.figures.push_back(figure);
    }
    const std::uint8_t throws = in.get<std::uint8_t>();
    if (!in.ok() || throws > MaxBattleThrows) {
      return core::fail("invalid cooperative battle frame throws");
    }
    for (std::uint8_t i = 0; i < throws && in.ok(); ++i) {
      BattleThrow thrown;
      if (!getThrow(in, thrown)) {
        return core::fail("invalid cooperative battle throw");
      }
      frame.throws.push_back(std::move(thrown));
    }
    if (bytes.size() + 1 > net::MaxDatagram) {
      return core::fail("cooperative battle frame exceeds the unreliable datagram limit");
    }
    message = std::move(frame);
    break;
  }
  case BattleActionMessageKind: {
    BattleAction action;
    action.battle = in.get<std::uint32_t>();
    const auto actionKind = in.get<std::uint8_t>();
    action.value = in.get<std::int32_t>();
    action.item = in.text();
    if (!in.ok() || action.battle == 0 ||
        actionKind > static_cast<std::uint8_t>(BattleActionKind::Stance)) {
      return core::fail("invalid cooperative battle action");
    }
    action.kind = static_cast<BattleActionKind>(actionKind);
    if ((action.kind == BattleActionKind::Command &&
         (action.value <= 0 || action.value > LastBattleCommand)) ||
        (action.kind == BattleActionKind::ThrowItem &&
         (action.item.empty() || action.item.size() > MaxBattleText || action.value < 0 ||
          std::cmp_greater(action.value, MaxBattleFighters))) ||
        (action.kind == BattleActionKind::Order &&
         (action.value < 0 ||
          action.value >= battleOrderValue(0, static_cast<std::int32_t>(MaxBattleFighters) + 1))) ||
        (action.kind == BattleActionKind::Stance &&
         (action.value < 0 || action.value > LastStance))) {
      return core::fail("invalid cooperative battle action value");
    }
    message = std::move(action);
    break;
  }
  case BattleEndKind: {
    BattleEnd end;
    end.battle = in.get<std::uint32_t>();
    const auto outcome = in.get<std::uint8_t>();
    if (!in.ok() || end.battle == 0 || outcome > static_cast<std::uint8_t>(BattleOutcome::Defeat) ||
        !getSlots(in, end.leftMap, MaxBattleLeavers)) {
      return core::fail("invalid cooperative battle end");
    }
    end.outcome = static_cast<BattleOutcome>(outcome);
    end.bits = in.get<std::int32_t>();
    bool spoils = false;
    if (!getFlag(in, spoils)) {
      return core::fail("invalid cooperative battle end");
    }
    if (spoils) {
      BattleSpoils read;
      if (!getSpoils(in, read)) {
        return core::fail("invalid cooperative battle spoils");
      }
      end.spoils = std::move(read);
    }
    message = std::move(end);
    break;
  }
  case PlayerStateKind: {
    PlayerState state;
    state.player = in.get<PlayerId>();
    state.sequence = in.get<std::uint32_t>();
    state.epoch = in.get<std::uint16_t>();
    state.mapId = in.text();
    state.x = in.get<std::int32_t>();
    state.y = in.get<std::int32_t>();
    state.z = in.get<std::int32_t>();
    state.yaw = in.get<std::uint16_t>();
    bool motionKnown = getMotion(in, state.motion, state.action);
    if (in.get<bool>()) {
      HeldPartner partner{.x = in.get<std::int32_t>(),
                          .y = in.get<std::int32_t>(),
                          .z = in.get<std::int32_t>(),
                          .yaw = in.get<std::uint16_t>()};
      motionKnown = getMotion(in, partner.motion, partner.action) && motionKnown &&
                    validCoordinate(partner.x, MaxWorldCoordinate) &&
                    validCoordinate(partner.y, MaxWorldCoordinate) &&
                    validCoordinate(partner.z, MaxWorldCoordinate);
      state.partner = partner;
    }
    if (!in.ok() || !validGuest(state.player) || state.mapId.empty() || !motionKnown ||
        !validCoordinate(state.x, MaxWorldCoordinate) ||
        !validCoordinate(state.y, MaxWorldCoordinate) ||
        !validCoordinate(state.z, MaxWorldCoordinate)) {
      return core::fail("invalid cooperative player state");
    }
    message = std::move(state);
    break;
  }
  case PlayerCorrectionKind: {
    PlayerCorrection correction;
    correction.player = in.get<PlayerId>();
    correction.epoch = in.get<std::uint16_t>();
    correction.mapId = in.text();
    bool hasExit = false;
    if (!getFlag(in, hasExit)) {
      return core::fail("invalid cooperative correction flag");
    }
    if (hasExit) {
      correction.exit = in.get<std::int32_t>();
    }
    correction.x = in.get<std::int32_t>();
    correction.y = in.get<std::int32_t>();
    correction.z = in.get<std::int32_t>();
    correction.yaw = in.get<std::uint16_t>();
    if (!in.ok() || !validGuest(correction.player) || correction.mapId.empty() ||
        (correction.exit && *correction.exit < 0) ||
        !validCoordinate(correction.x, MaxWorldCoordinate) ||
        !validCoordinate(correction.y, MaxWorldCoordinate) ||
        !validCoordinate(correction.z, MaxWorldCoordinate)) {
      return core::fail("invalid cooperative player correction");
    }
    message = std::move(correction);
    break;
  }
  case RosterKind: {
    Roster roster;
    roster.revision = in.get<std::uint32_t>();
    const std::uint8_t count = in.get<std::uint8_t>();
    if (count == 0 || count > MaxPlayers) {
      return core::fail(std::format("cooperative roster has {} players", count));
    }
    std::unordered_set<PlayerId> ids;
    for (std::uint8_t i = 0; i < count && in.ok(); ++i) {
      RosterEntry entry{.id = in.get<PlayerId>(), .name = in.text(), .partnerSpecies = in.text()};
      if (!validPlayer(entry.id) || !ids.insert(entry.id).second ||
          entry.name.size() > MaxRosterText || entry.partnerSpecies.size() > MaxRosterText) {
        return core::fail("invalid or duplicate player in cooperative roster");
      }
      roster.players.push_back(std::move(entry));
    }
    if (!in.ok() || !ids.contains(HostPlayerId)) {
      return core::fail("cooperative roster has no host");
    }
    message = std::move(roster);
    break;
  }
  case WorldAskKind: {
    WorldAsk ask{.ask = in.get<std::uint32_t>(),
                 .seen = in.get<std::uint32_t>(),
                 .mapId = in.text(),
                 .edits = {}};
    if (!getEdits(in, ask.edits) || ask.mapId.empty()) {
      return core::fail("invalid cooperative world ask");
    }
    message = std::move(ask);
    break;
  }
  case WorldChangeKind: {
    WorldChange change{.revision = in.get<std::uint32_t>(),
                       .origin = in.get<PlayerId>(),
                       .ask = in.get<std::uint32_t>(),
                       .mapId = in.text(),
                       .edits = {}};
    if (!getEdits(in, change.edits) || !validPlayer(change.origin) || change.revision == 0) {
      return core::fail("invalid cooperative world change");
    }
    message = std::move(change);
    break;
  }
  case WorldSyncKind: {
    WorldSync sync{.revision = in.get<std::uint32_t>(),
                   .part = in.get<std::uint16_t>(),
                   .parts = in.get<std::uint16_t>(),
                   .firstTrigger = in.get<std::int32_t>(),
                   .triggers = {},
                   .firstVariable = 0,
                   .variables = {}};
    const std::uint16_t triggers = in.get<std::uint16_t>();
    if (!in.ok() || triggers > MaxSyncTriggers || sync.parts == 0 || sync.part >= sync.parts ||
        sync.firstTrigger < 0) {
      return core::fail("invalid cooperative world sync");
    }
    sync.triggers.reserve(triggers);
    for (std::size_t byte = 0; byte < (triggers + 7U) / 8U && in.ok(); ++byte) {
      const std::uint8_t bits = in.get<std::uint8_t>();
      for (std::size_t bit = 0; bit < 8 && sync.triggers.size() < triggers; ++bit) {
        sync.triggers.push_back(((bits >> bit) & 1U) != 0);
      }
    }
    sync.firstVariable = in.get<std::int32_t>();
    const std::uint16_t variables = in.get<std::uint16_t>();
    if (!in.ok() || variables > MaxSyncVariables || sync.firstVariable < 0) {
      return core::fail("invalid cooperative world sync variables");
    }
    for (std::uint16_t i = 0; i < variables && in.ok(); ++i) {
      sync.variables.push_back(in.get<std::int32_t>());
    }
    message = std::move(sync);
    break;
  }
  case GroundItemsKind: {
    GroundItems ground{.mapId = in.text(), .mapSerial = in.get<std::uint32_t>(), .items = {}};
    const std::uint8_t count = in.get<std::uint8_t>();
    if (!in.ok() || ground.mapId.empty() || count > MaxGroundEntries) {
      return core::fail("invalid cooperative ground items");
    }
    std::unordered_set<std::uint8_t> slots;
    for (std::uint8_t i = 0; i < count && in.ok(); ++i) {
      GroundEntry entry{.id = in.get<std::uint32_t>(),
                        .slot = in.get<std::uint8_t>(),
                        .item = in.text(),
                        .column = in.get<std::int16_t>(),
                        .row = in.get<std::int16_t>(),
                        .x = in.get<std::int32_t>(),
                        .y = in.get<std::int32_t>(),
                        .z = in.get<std::int32_t>()};
      if (entry.id == 0 || entry.item.empty() || entry.item.size() > MaxGroundItemId ||
          !slots.insert(entry.slot).second || !validCoordinate(entry.x, MaxWorldCoordinate) ||
          !validCoordinate(entry.y, MaxWorldCoordinate) ||
          !validCoordinate(entry.z, MaxWorldCoordinate)) {
        return core::fail("invalid or duplicate cooperative ground item");
      }
      ground.items.push_back(std::move(entry));
    }
    message = std::move(ground);
    break;
  }
  case ItemClaimKind: {
    ItemClaim claim{
        .claim = in.get<std::uint32_t>(), .kind = ClaimKind::Chest, .key = 0, .id = 0, .mapId = {}};
    const std::uint8_t claimKind = in.get<std::uint8_t>();
    claim.key = in.get<std::int32_t>();
    claim.id = in.get<std::uint32_t>();
    claim.mapId = in.text();
    if (!in.ok() || claimKind > static_cast<std::uint8_t>(ClaimKind::GroundItem) ||
        claim.claim == 0 || claim.key < 0 || claim.mapId.empty()) {
      return core::fail("invalid cooperative item claim");
    }
    claim.kind = static_cast<ClaimKind>(claimKind);
    if (claim.kind == ClaimKind::GroundItem && claim.id == 0) {
      return core::fail("cooperative item claim names no ground item");
    }
    message = std::move(claim);
    break;
  }
  case ClaimAnswerKind: {
    ClaimAnswer answer{.claim = in.get<std::uint32_t>(), .granted = in.get<bool>()};
    if (!in.ok() || answer.claim == 0) {
      return core::fail("invalid cooperative claim answer");
    }
    message = answer;
    break;
  }
  case PoopPilesKind: {
    PoopPiles piles{.mapId = in.text(), .restart = in.get<bool>(), .piles = {}};
    const std::uint8_t count = in.get<std::uint8_t>();
    if (!in.ok() || piles.mapId.size() > MaxPileMapText || count > MaxMapPiles ||
        (piles.mapId.empty() && (!piles.restart || count != 0))) {
      return core::fail("invalid cooperative poop piles");
    }
    for (std::uint8_t i = 0; i < count && in.ok(); ++i) {
      PileEntry pile{
          .x = in.get<std::int16_t>(), .y = in.get<std::int16_t>(), .size = in.get<std::int32_t>()};
      if (pile.size <= 0) {
        return core::fail("invalid cooperative poop pile");
      }
      piles.piles.push_back(pile);
    }
    message = std::move(piles);
    break;
  }
  case PileAskKind: {
    PileAsk ask{.ask = in.get<std::uint32_t>(), .action = PileAction::Leave, .mapId = {}};
    const std::uint8_t action = in.get<std::uint8_t>();
    ask.mapId = in.text();
    ask.x = in.get<std::int16_t>();
    ask.y = in.get<std::int16_t>();
    ask.size = in.get<std::int32_t>();
    if (!in.ok() || ask.ask == 0 || action > static_cast<std::uint8_t>(PileAction::Eat) ||
        ask.mapId.empty() || ask.mapId.size() > MaxPileMapText) {
      return core::fail("invalid cooperative pile ask");
    }
    ask.action = static_cast<PileAction>(action);
    if (ask.action == PileAction::Leave && ask.size <= 0) {
      return core::fail("cooperative pile ask leaves no pile");
    }
    message = std::move(ask);
    break;
  }
  case PileAnswerKind: {
    PileAnswer answer{
        .ask = in.get<std::uint32_t>(), .granted = in.get<bool>(), .size = in.get<std::int32_t>()};
    if (!in.ok() || answer.ask == 0 || (answer.granted && answer.size <= 0)) {
      return core::fail("invalid cooperative pile answer");
    }
    message = answer;
    break;
  }
  case LobbyKind: {
    Lobby lobby;
    lobby.capacity = in.get<std::uint8_t>();
    const std::uint8_t count = in.get<std::uint8_t>();
    if (lobby.capacity < 2 || lobby.capacity > MaxPlayers || count == 0 || count > lobby.capacity) {
      return core::fail("invalid cooperative lobby size");
    }
    std::unordered_set<PlayerId> ids;
    for (std::uint8_t i = 0; i < count && in.ok(); ++i) {
      LobbySeat seat{.id = in.get<PlayerId>(), .name = in.text(), .state = SeatState::Connected};
      const std::uint8_t state = in.get<std::uint8_t>();
      if (!validPlayer(seat.id) || !ids.insert(seat.id).second ||
          seat.name.size() > MaxRosterText || state > static_cast<std::uint8_t>(SeatState::Ready)) {
        return core::fail("invalid or duplicate seat in cooperative lobby");
      }
      seat.state = static_cast<SeatState>(state);
      lobby.seats.push_back(std::move(seat));
    }
    if (!in.ok() || !ids.contains(HostPlayerId)) {
      return core::fail("cooperative lobby has no host");
    }
    // An older host's lobby ends here: its game is not under way.
    if (!in.done() && (!getFlag(in, lobby.playing) || !getFlag(in, lobby.open))) {
      return core::fail("invalid cooperative lobby game state");
    }
    if (!in.done()) {
      const auto difficulty = in.get<std::uint8_t>();
      if (!in.ok() || difficulty > static_cast<std::uint8_t>(CoopDifficulty::Hard)) {
        return core::fail("invalid cooperative lobby difficulty");
      }
      lobby.difficulty = static_cast<CoopDifficulty>(difficulty);
    }
    message = std::move(lobby);
    break;
  }
  case SyncWatchKind: {
    bool on = false;
    if (!getFlag(in, on)) {
      return core::fail("invalid cooperative sync watch");
    }
    message = SyncWatch{.on = on};
    break;
  }
  case SyncReportKind: {
    SyncReport report;
    report.sequence = in.get<std::uint32_t>();
    report.snapshotTick = in.get<std::uint32_t>();
    report.snapshotsLost = in.get<std::uint32_t>();
    report.snapshotsLate = in.get<std::uint32_t>();
    const std::uint8_t count = in.get<std::uint8_t>();
    if (count > MaxPlayers) {
      return core::fail("cooperative sync report has too many players");
    }
    std::unordered_set<PlayerId> ids;
    for (std::uint8_t i = 0; i < count && in.ok(); ++i) {
      SyncHeld held;
      held.id = in.get<PlayerId>();
      held.mapId = in.text();
      held.x = in.get<std::int32_t>();
      held.y = in.get<std::int32_t>();
      held.z = in.get<std::int32_t>();
      if (!validPlayer(held.id) || !ids.insert(held.id).second || held.mapId.size() > 255 ||
          !validCoordinate(held.x, MaxWorldCoordinate) ||
          !validCoordinate(held.y, MaxWorldCoordinate) ||
          !validCoordinate(held.z, MaxWorldCoordinate)) {
        return core::fail("invalid player in cooperative sync report");
      }
      report.players.push_back(std::move(held));
    }
    message = std::move(report);
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
  case TradeAskKind: {
    TradeAsk ask{.with = in.get<PlayerId>()};
    if (!in.ok() || !validPlayer(ask.with)) {
      return core::fail("invalid cooperative trade ask");
    }
    message = ask;
    break;
  }
  case TradeAnswerKind: {
    TradeAnswer answer;
    answer.trade = in.get<std::uint32_t>();
    if (!getFlag(in, answer.accept) || answer.trade == 0) {
      return core::fail("invalid cooperative trade answer");
    }
    message = answer;
    break;
  }
  case TradeOfferKind: {
    TradeOffer offer;
    offer.trade = in.get<std::uint32_t>();
    if (!in.ok() || offer.trade == 0 || !getTradeItems(in, offer.items)) {
      return core::fail("invalid cooperative trade offer");
    }
    message = std::move(offer);
    break;
  }
  case TradeConfirmKind: {
    TradeConfirm confirm;
    confirm.trade = in.get<std::uint32_t>();
    confirm.revision = in.get<std::uint32_t>();
    if (!getFlag(in, confirm.confirmed) || confirm.trade == 0) {
      return core::fail("invalid cooperative trade confirmation");
    }
    message = confirm;
    break;
  }
  case TradeCancelKind: {
    TradeCancel cancel{.trade = in.get<std::uint32_t>()};
    if (!in.ok() || cancel.trade == 0) {
      return core::fail("invalid cooperative trade cancel");
    }
    message = cancel;
    break;
  }
  case TradeStateKind: {
    TradeState state;
    state.trade = in.get<std::uint32_t>();
    state.players = {in.get<PlayerId>(), in.get<PlayerId>()};
    const std::uint8_t phase = in.get<std::uint8_t>();
    state.revision = in.get<std::uint32_t>();
    if (!in.ok() || state.trade == 0 || !validPlayer(state.players[0]) ||
        !validPlayer(state.players[1]) || state.players[0] == state.players[1] ||
        phase > static_cast<std::uint8_t>(TradePhase::Executing) ||
        !getTradeItems(in, state.offers[0]) || !getTradeItems(in, state.offers[1]) ||
        !getFlag(in, state.confirmed[0]) || !getFlag(in, state.confirmed[1])) {
      return core::fail("invalid cooperative trade state");
    }
    state.phase = static_cast<TradePhase>(phase);
    message = std::move(state);
    break;
  }
  case TradePrepareKind: {
    TradePrepare prepare;
    prepare.trade = in.get<std::uint32_t>();
    if (!in.ok() || prepare.trade == 0 || !getTradeItems(in, prepare.give) ||
        !getTradeItems(in, prepare.get)) {
      return core::fail("invalid cooperative trade prepare");
    }
    message = std::move(prepare);
    break;
  }
  case TradeCommitKind:
  case TradeRevertKind: {
    const std::uint32_t trade = in.get<std::uint32_t>();
    if (!in.ok() || trade == 0) {
      return core::fail("invalid cooperative trade commit or revert");
    }
    message = kind == TradeCommitKind ? Message{TradeCommit{trade}} : Message{TradeRevert{trade}};
    break;
  }
  case TradeReplyKind: {
    TradeReply reply;
    reply.trade = in.get<std::uint32_t>();
    const std::uint8_t step = in.get<std::uint8_t>();
    const std::uint8_t check = in.get<std::uint8_t>();
    if (!in.ok() || reply.trade == 0 || step > static_cast<std::uint8_t>(TradeStep::Applied) ||
        check > static_cast<std::uint8_t>(TradeCheck::NoRoom)) {
      return core::fail("invalid cooperative trade reply");
    }
    reply.step = static_cast<TradeStep>(step);
    reply.check = static_cast<TradeCheck>(check);
    message = reply;
    break;
  }
  case TradeClosedKind: {
    TradeClosed closed;
    closed.trade = in.get<std::uint32_t>();
    const std::uint8_t outcome = in.get<std::uint8_t>();
    closed.by = in.get<PlayerId>();
    if (!in.ok() || closed.trade == 0 ||
        outcome > static_cast<std::uint8_t>(TradeOutcome::NoRoom) || !validPlayer(closed.by)) {
      return core::fail("invalid cooperative trade close");
    }
    closed.outcome = static_cast<TradeOutcome>(outcome);
    message = closed;
    break;
  }
  case WorldClockKind: {
    WorldClock clock;
    clock.elapsedFrames = in.get<std::int64_t>();
    if (!getFlag(in, clock.running)) {
      return core::fail("invalid cooperative world clock");
    }
    clock.speedThousandths = in.get<std::uint32_t>();
    if (!in.ok() || clock.elapsedFrames < 0) {
      return core::fail("invalid cooperative world clock");
    }
    message = clock;
    break;
  }
  case PartnerFormKind: {
    PartnerForm form{.player = in.get<PlayerId>(), .species = in.text()};
    if (!in.ok() || !validGuest(form.player) || form.species.size() > MaxRosterText) {
      return core::fail("invalid cooperative partner form");
    }
    message = std::move(form);
    break;
  }
  case TrainingStatusKind: {
    TrainingStatus status;
    status.player = in.get<PlayerId>();
    status.course = in.text();
    if (!in.ok() || !validPlayer(status.player) || status.course.size() > MaxCourseText) {
      return core::fail("invalid cooperative training status");
    }
    message = std::move(status);
    break;
  }
  case EventStatusKind: {
    EventStatus status;
    status.player = in.get<PlayerId>();
    status.mapId = in.text();
    status.npc = in.get<std::uint16_t>();
    if (!in.ok() || !validPlayer(status.player) || status.mapId.size() > MaxEventMapText) {
      return core::fail("invalid cooperative event status");
    }
    message = std::move(status);
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
