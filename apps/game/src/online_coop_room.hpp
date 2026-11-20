#pragma once

#include "coop_screen.hpp"
#include "online_rooms.hpp"

namespace dwe::app {

// The rooms screens over today's co-op connection (CoopScreen): two players, a campaign room, no
// names and no public listing yet. What the network side adds later (#291 public hosting, #292
// room state with names, modes and more players) replaces this backend, not the screens.
class CoopRoomBackend final : public RoomBackend {
public:
  explicit CoopRoomBackend(CoopScreen& coop) : coop_{&coop} {}

  // A listed (public) room goes in the signaling server's public list (/v1/host?public=1).
  void host(bool listed, double nowSeconds) override;
  void join(const RoomListing& listing, double nowSeconds) override;
  void joinByCode(std::string_view code, double nowSeconds) override;
  void leave() override;
  void setReady(bool ready) override;
  std::optional<std::string> start() override;
  void setOpenToNewPlayers(bool open) override;
  void tick(double /*nowSeconds*/) override {}
  [[nodiscard]] std::optional<RoomView> room() const override;
  // Only the campaign plays over the co-op link; the versus has its own connection.
  [[nodiscard]] bool playable(std::string_view mode) const override { return mode == "campaign"; }

private:
  CoopScreen* coop_;
  bool listed_ = true;
};

} // namespace dwe::app
