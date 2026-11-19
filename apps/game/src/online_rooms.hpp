#pragma once

#include <dwe/net/room_session.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "online_profile.hpp"

namespace dwe::app {

// The online rooms behind the «Search rooms» and «Room» screens (ui/online_rooms, ui/online_room),
// published to the UI API's online namespace (ui/api/online.lua). The screens only see what this
// publishes; where each piece comes from is behind two seams, so the network side (the public room
// directory and the room state messages, #291/#292) plugs in without touching the screens:
// - RoomDirectory lists the open public rooms.
// - RoomBackend opens, joins and runs the room the player is in.
// The development source (SampleDirectory, SampleRoomBackend) fills both with sample data, marked
// as such on screen, to look at the screens without a second machine.

// Most players a room holds: its host and up to three guests (docs/architecture/coop-browser-4p.md
// in #339). The co-op link plays one guest today, so its rooms say capacity 2 until the protocol,
// runtime and simulation take four.
inline constexpr int MaxRoomPlayers = 4;

// A public room's state in the directory: waiting for players, full (shown, but disabled), or
// starting its game.
enum class ListingState { Waiting, Full, Starting };

[[nodiscard]] std::string_view listingStateName(ListingState state);

// A room of the public list, with the fields of the directory's RoomListing (#339:
// apps/versus-signal/src/room.ts) plus the two the screens show that it does not carry yet.
struct RoomListing {
  std::string roomId;
  std::string joinCode;
  // The name and message the host gave it (plain text, bounded by the directory); a sample room
  // carries a text key for its name instead (nameKey).
  std::string name;
  std::string message;
  std::string hostName;
  int players = 1;
  int capacity = MaxRoomPlayers;
  // When it opened, in milliseconds since the epoch.
  double createdAt = 0.0;
  ListingState state = ListingState::Waiting;
  // Not in the directory yet: the sample's text key for its name, the game mode (campaign, versus,
  // training or free; RoomModes; the directory lists co-op rooms only, so campaign) and the round
  // trip in milliseconds (negative when nobody measured it).
  std::string nameKey;
  std::string mode = "campaign";
  int pingMs = -1;
};

// Whether a listed room takes one more player: waiting and not full.
[[nodiscard]] inline bool hasSeat(const RoomListing& room) {
  return room.state == ListingState::Waiting && room.players < room.capacity;
}

// What the list is doing: asking, ready with its rooms, or unavailable (with an error code).
enum class DirectoryState { Loading, Ready, Unavailable };

class RoomDirectory {
public:
  RoomDirectory() = default;
  RoomDirectory(const RoomDirectory&) = delete;
  RoomDirectory& operator=(const RoomDirectory&) = delete;
  RoomDirectory(RoomDirectory&&) = delete;
  RoomDirectory& operator=(RoomDirectory&&) = delete;
  virtual ~RoomDirectory() = default;

  // Asks for the list again.
  virtual void refresh(double nowSeconds) = 0;
  virtual void tick(double nowSeconds) = 0;
  [[nodiscard]] virtual DirectoryState state() const = 0;
  [[nodiscard]] virtual std::span<const RoomListing> rooms() const = 0;
  [[nodiscard]] virtual std::string_view error() const = 0;
};

// A directory that lists nothing: rooms are joined by code only (the tests; the game reads the
// signaling server's list, online_signal_directory.hpp).
class UnlistedDirectory final : public RoomDirectory {
public:
  void refresh(double /*nowSeconds*/) override {}
  void tick(double /*nowSeconds*/) override {}
  [[nodiscard]] DirectoryState state() const override { return DirectoryState::Unavailable; }
  [[nodiscard]] std::span<const RoomListing> rooms() const override { return {}; }
  [[nodiscard]] std::string_view error() const override { return "directory_unavailable"; }
};

// Sample rooms for development: a short wait as if asking a server, then a fixed list (some full,
// some versus, more than a page). Their names are text keys, never species or brand names.
class SampleDirectory final : public RoomDirectory {
public:
  explicit SampleDirectory(bool empty = false) : empty_{empty} {}
  void refresh(double nowSeconds) override;
  void tick(double nowSeconds) override;
  [[nodiscard]] DirectoryState state() const override { return state_; }
  [[nodiscard]] std::span<const RoomListing> rooms() const override { return rooms_; }
  [[nodiscard]] std::string_view error() const override { return {}; }

private:
  bool empty_ = false;
  DirectoryState state_ = DirectoryState::Loading;
  std::optional<double> askedAt_;
  std::vector<RoomListing> rooms_;
};

// The room's life: being opened or joined, open (waiting for players), connected, everyone ready,
// playing, or closed (left or failed; the error says why). The network's room session keeps it
// (dwe/net/room_session.hpp); the sample rooms set it themselves.
using net::RoomPhase;
using net::roomPhaseName;

struct RoomMember {
  // The player's name; empty when the room does not carry names yet (the screens name them).
  std::string name;
  std::string nameKey;
  bool host = false;
  bool ready = false;
  // The player at this machine.
  bool local = false;
  // Its profile's icon (OnlineProfile::Icons); empty when the room does not carry it.
  std::string icon;
};

// A member with no name of its own (the screens name it by its place).
[[nodiscard]] inline RoomMember unnamedMember(bool host, bool ready, bool local) {
  return {.name = {}, .nameKey = {}, .host = host, .ready = ready, .local = local, .icon = {}};
}

// The room the player is in.
struct RoomView {
  std::string code;
  std::string name;
  std::string nameKey;
  bool isHost = false;
  // Listed in the public directory (public) or reachable only by its code (private).
  bool listed = true;
  RoomPhase phase = RoomPhase::Connecting;
  int capacity = 2;
  std::vector<RoomMember> members;
  // Why the room closed or an action failed (an error code, online.error.<code>).
  std::string error;
  // The host can start now; the guest can mark itself ready now.
  bool canStart = false;
  bool canReady = false;
  bool localReady = false;
  // The mode the room was listed with, when it was joined from the list.
  std::string mode;
};

// A room that could not be opened or joined, and why.
[[nodiscard]] RoomView closedRoom(std::string code, std::string error);

// Opens, joins and runs the room. The co-op connection is one today (CoopScreen); the room state
// messages of the network side (#292) will carry the members, their names and the mode.
class RoomBackend {
public:
  RoomBackend() = default;
  RoomBackend(const RoomBackend&) = delete;
  RoomBackend& operator=(const RoomBackend&) = delete;
  RoomBackend(RoomBackend&&) = delete;
  RoomBackend& operator=(RoomBackend&&) = delete;
  virtual ~RoomBackend() = default;

  virtual void host(bool listed, double nowSeconds) = 0;
  virtual void join(const RoomListing& listing, double nowSeconds) = 0;
  virtual void joinByCode(std::string_view code, double nowSeconds) = 0;
  virtual void leave() = 0;
  virtual void setReady(bool ready) = 0;
  // Asks to start the room's game; false with a notice code when it cannot.
  virtual std::optional<std::string> start() = 0;
  virtual void tick(double nowSeconds) = 0;
  [[nodiscard]] virtual std::optional<RoomView> room() const = 0;
  // Whether a mode can be played over this backend's rooms.
  [[nodiscard]] virtual bool playable(std::string_view mode) const = 0;
};

// Sample room for development: hosting opens a room with a sample code that a sample guest joins
// and readies a moment later; joining puts the player beside a sample host. Starting says it is a
// sample instead of starting anything.
class SampleRoomBackend final : public RoomBackend {
public:
  // The sample list, to join one of its rooms by its code.
  // `connectSeconds`: how long getting into a room takes (the screens' «Connecting» step); none
  // by default.
  explicit SampleRoomBackend(const RoomDirectory& known, double connectSeconds = 0.0)
      : known_{&known}, connectSeconds_{connectSeconds} {}
  void host(bool listed, double nowSeconds) override;
  void join(const RoomListing& listing, double nowSeconds) override;
  void joinByCode(std::string_view code, double nowSeconds) override;
  void leave() override {
    room_.reset();
    pending_.reset();
  }
  void setReady(bool ready) override;
  std::optional<std::string> start() override;
  void tick(double nowSeconds) override;
  [[nodiscard]] std::optional<RoomView> room() const override { return room_; }
  [[nodiscard]] bool playable(std::string_view /*mode*/) const override { return true; }

private:
  void joinNow(const RoomListing& listing, double nowSeconds);
  // The room just asked for shows as connecting first, for connectSeconds_.
  void arrive(double nowSeconds);

  const RoomDirectory* known_;
  double connectSeconds_ = 0.0;
  std::optional<RoomView> room_;
  std::optional<RoomView> pending_;
  double openedAt_ = 0.0;
};

// The game modes a room offers, in the screens' order. Training and free play exist in rooms only
// as names for now: the screens show them as unavailable.
struct RoomMode {
  std::string_view id;
  // Played over rooms at all (the backend can still refuse it, RoomBackend::playable).
  bool exists = false;
};

inline constexpr auto RoomModes = std::to_array<RoomMode>({
    {.id = "campaign", .exists = true},
    {.id = "versus", .exists = true},
    {.id = "training", .exists = false},
    {.id = "free", .exists = false},
});

// The text field of the search screen that takes the keyboard's letters.
enum class OnlineField { None, Filter, Code, Name };

// The online screens' state: the list, the text typed into the search and code fields, the mode
// the host picked, the quick match's answer and the last notice. Commands come from the UI API's
// online actions (online_*); settings() is what it publishes (online.*).
class OnlineRooms {
public:
  struct Desc {
    std::unique_ptr<RoomDirectory> directory;
    std::unique_ptr<RoomBackend> backend;
    // The sample source, swapped in by setSample (development).
    std::unique_ptr<RoomDirectory> sampleDirectory;
    std::unique_ptr<RoomBackend> sampleBackend;
    // Whether the screens may offer the sample switch (the developer tools are on).
    bool sampleOffered = false;
    std::size_t codeLength = 4;
    // The player's online profile (online_profile.hpp), as the settings keep it.
    OnlineProfile profile;
  };

  explicit OnlineRooms(Desc desc);

  // Handles an online_* command; false for any other.
  bool handle(std::string_view name, const std::string& text, bool flag, double nowSeconds);
  void tick(double nowSeconds);
  // The keyboard's letters, while a field takes them; Enter submits the code field.
  void type(std::string_view text, bool enter, double nowSeconds);
  // The profile's name takes letters from the profile screen, which the rooms screens need not be
  // open for; the search and the code only while they are.
  [[nodiscard]] bool takesText() const {
    return field_ == OnlineField::Name || (open_ && field_ != OnlineField::None);
  }
  [[nodiscard]] bool open() const { return open_; }
  [[nodiscard]] bool inRoom() const { return backend().room().has_value(); }
  [[nodiscard]] bool sample() const { return sample_; }
  void setSampleOffered(bool offered) { sampleOffered_ = offered; }
  void setSample(bool on, double nowSeconds);

  [[nodiscard]] screens::ScreenSettings settings() const;

  [[nodiscard]] const OnlineProfile& profile() const { return profile_; }
  // Whether the profile changed since the last call (the game then writes the settings).
  bool takeProfileChanged() { return std::exchange(profileChanged_, false); }
  // An online versus ended: the profile's record counts it.
  void recordVersus(bool won);

private:
  // What the player last asked to get into a room, which «Retry» asks again.
  struct Attempt {
    enum class Kind { None, Host, Listing, Code } kind = Kind::None;
    bool listed = false;
    RoomListing listing;
    std::string code;
  };

  void retry(double nowSeconds);
  void commitName();
  [[nodiscard]] RoomDirectory& directory() const;
  [[nodiscard]] RoomBackend& backend() const;
  void quickMatch(double nowSeconds);
  void joinTyped(double nowSeconds);
  [[nodiscard]] bool modeAvailable(std::string_view mode) const;

  Desc desc_;
  bool sampleOffered_ = false;
  bool sample_ = false;
  bool open_ = false;
  OnlineField field_ = OnlineField::None;
  std::string filter_;
  std::string code_;
  std::string mode_ = "campaign";
  // The quick match's answer: "" (not asked), "none" (no room to join: offer to create one).
  std::string quick_;
  // The last thing to tell the player that is not the room's own error (online.notice.<code>).
  std::string notice_;
  OnlineProfile profile_;
  bool profileChanged_ = false;
  // The name being typed on the profile screen.
  std::string nameDraft_;
  Attempt attempt_;
};

} // namespace dwe::app
