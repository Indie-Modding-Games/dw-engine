#include "coop_gameplay_bridge.hpp"

#include <algorithm>

namespace dwe::app {

void CoopGameplayBridge::poll(net::Millis now) {
  link_->poll(now);
  remoteController_->setNow(now);
  while (auto message = link_->receive()) {
    if (const auto* commands = std::get_if<game::coop::Commands>(&*message)) {
      (void)remoteController_->receive(*commands, now);
    } else if (const auto* snapshot = std::get_if<game::coop::Snapshot>(&*message)) {
      (void)snapshots_.push(*snapshot);
    } else if (const auto* chunk = std::get_if<game::coop::ProfileChunk>(&*message)) {
      if (!profileTransferId_ || *profileTransferId_ != chunk->transferId) {
        if (auto begun = profileAssembler_.begin(chunk->transferId, chunk->count, chunk->kind);
            !begun) {
          profileTransferId_.reset();
          continue;
        }
        profileTransferId_ = chunk->transferId;
      }
      auto assembled = profileAssembler_.push(*chunk);
      if (!assembled) {
        profileAssembler_.reset();
        profileTransferId_.reset();
      } else if (*assembled) {
        profiles_.push_back({.kind = chunk->kind,
                             .transferId = chunk->transferId,
                             .bytes = std::move(**assembled)});
        profileAssembler_.reset();
        profileTransferId_.reset();
      }
    } else {
      incoming_.push_back(std::move(*message));
    }
  }
}

core::Result<void> CoopGameplayBridge::sendLocalField(const PlayerCommands& commands) {
  auto networkCommand = toCoopCommand(commands);
  if (!networkCommand) return std::unexpected{networkCommand.error()};
  return link_->send(game::coop::Message{std::move(*networkCommand)});
}

core::Result<void> CoopGameplayBridge::sendHostSnapshot(const game::coop::Snapshot& snapshot) {
  return link_->send(game::coop::Message{snapshot});
}

core::Result<void> CoopGameplayBridge::sendProfile(game::coop::ProfilePayloadKind kind,
                                                    std::uint32_t transferId,
                                                    std::span<const std::uint8_t> bytes) {
  if (transferId == 0 || bytes.empty() || bytes.size() > game::coop::MaxProfileBytes) {
    return core::fail("invalid cooperative profile transfer size or id");
  }
  const std::size_t chunkCount =
      (bytes.size() + game::coop::MaxProfileChunkBytes - 1) / game::coop::MaxProfileChunkBytes;
  for (std::size_t index = 0; index < chunkCount; ++index) {
    const std::size_t offset = index * game::coop::MaxProfileChunkBytes;
    const std::size_t count = std::min(game::coop::MaxProfileChunkBytes, bytes.size() - offset);
    game::coop::ProfileChunk chunk{.transferId = transferId,
                                   .index = static_cast<std::uint16_t>(index),
                                   .count = static_cast<std::uint16_t>(chunkCount),
                                   .kind = kind,
                                   .bytes = net::Bytes{bytes.data() + offset,
                                                       bytes.data() + offset + count}};
    if (auto sent = link_->send(game::coop::Message{std::move(chunk)}); !sent) return sent;
  }
  return {};
}

std::optional<game::coop::Message> CoopGameplayBridge::receive() {
  if (incoming_.empty()) return std::nullopt;
  game::coop::Message message = std::move(incoming_.front());
  incoming_.pop_front();
  return message;
}

std::optional<CoopGameplayBridge::ReceivedProfile> CoopGameplayBridge::receiveProfile() {
  if (profiles_.empty()) return std::nullopt;
  ReceivedProfile profile = std::move(profiles_.front());
  profiles_.pop_front();
  return profile;
}

std::optional<PresentedCoopWorld> CoopGameplayBridge::presentedWorld(float alpha) const {
  return snapshots_.sample(alpha);
}

} // namespace dwe::app
