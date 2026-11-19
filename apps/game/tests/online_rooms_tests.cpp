#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "online_rooms.hpp"

using namespace dwe::app;

namespace {

// A backend that records what the rooms asked and plays only the campaign, as the co-op link.
class RecordingBackend final : public RoomBackend {
public:
  void host(bool listed, double /*nowSeconds*/) override {
    hosted = listed;
    room_ = RoomView{.code = "AB23",
                     .name = {},
                     .nameKey = {},
                     .isHost = true,
                     .listed = listed,
                     .phase = RoomPhase::Open,
                     .capacity = 2,
                     .members = {unnamedMember(true, false, true)},
                     .error = {},
                     .canStart = false,
                     .canReady = false,
                     .localReady = false,
                     .mode = {}};
  }
  void join(const RoomListing& listing, double /*nowSeconds*/) override { joined = listing.roomId; }
  void joinByCode(std::string_view code, double /*nowSeconds*/) override {
    joinedCode = std::string{code};
  }
  void leave() override { room_.reset(); }
  void setReady(bool /*ready*/) override {}
  std::optional<std::string> start() override { return std::nullopt; }
  void tick(double /*nowSeconds*/) override {}
  [[nodiscard]] std::optional<RoomView> room() const override { return room_; }
  [[nodiscard]] bool playable(std::string_view mode) const override { return mode == "campaign"; }

  std::optional<bool> hosted;
  std::string joined;
  std::string joinedCode;

private:
  std::optional<RoomView> room_;
};

// A directory with a fixed list, ready at once.
class FixedDirectory final : public RoomDirectory {
public:
  explicit FixedDirectory(std::vector<RoomListing> rooms) : rooms_{std::move(rooms)} {}
  void refresh(double /*nowSeconds*/) override {}
  void tick(double /*nowSeconds*/) override {}
  [[nodiscard]] DirectoryState state() const override { return DirectoryState::Ready; }
  [[nodiscard]] std::span<const RoomListing> rooms() const override { return rooms_; }
  [[nodiscard]] std::string_view error() const override { return {}; }

private:
  std::vector<RoomListing> rooms_;
};

RoomListing listing(std::string id, std::string mode, int players) {
  return {.roomId = id,
          .joinCode = id,
          .name = "Room " + id,
          .message = {},
          .hostName = {},
          .players = players,
          .capacity = 2,
          .createdAt = 0.0,
          .state = players < 2 ? ListingState::Waiting : ListingState::Full,
          .nameKey = {},
          .mode = std::move(mode),
          .pingMs = 50};
}

// The rooms over the sample source, switched on as the developer tools do.
OnlineRooms sampleRooms(bool empty = false, double connectSeconds = 0.0) {
  auto directory = std::make_unique<SampleDirectory>(empty);
  auto backend = std::make_unique<SampleRoomBackend>(*directory, connectSeconds);
  OnlineRooms rooms{OnlineRooms::Desc{.directory = std::make_unique<UnlistedDirectory>(),
                                      .backend = std::make_unique<RecordingBackend>(),
                                      .sampleDirectory = std::move(directory),
                                      .sampleBackend = std::move(backend),
                                      .sampleOffered = true,
                                      .codeLength = 4,
                                      .profile = {}}};
  rooms.setSample(true, 0.0);
  return rooms;
}

dwe::ui::UiValue value(const OnlineRooms& rooms, std::string_view name) {
  const auto settings = rooms.settings();
  const auto found = std::ranges::find(settings, name, &dwe::screens::ScreenSetting::name);
  REQUIRE_MESSAGE(found != settings.end(), name);
  return found->value;
}

std::string text(const OnlineRooms& rooms, std::string_view name) {
  return std::get<std::string>(value(rooms, name));
}

bool flag(const OnlineRooms& rooms, std::string_view name) {
  return std::get<bool>(value(rooms, name));
}

double number(const OnlineRooms& rooms, std::string_view name) {
  return std::get<double>(value(rooms, name));
}

} // namespace

TEST_CASE("without the network's list the rooms say it is unavailable and join by code") {
  auto backend = std::make_unique<RecordingBackend>();
  RecordingBackend* seen = backend.get();
  OnlineRooms rooms{OnlineRooms::Desc{.directory = std::make_unique<UnlistedDirectory>(),
                                      .backend = std::move(backend),
                                      .sampleDirectory = {},
                                      .sampleBackend = {},
                                      .sampleOffered = false,
                                      .codeLength = 4,
                                      .profile = {}}};
  CHECK(rooms.handle("online_open", {}, false, 0.0));
  CHECK(text(rooms, "online.list.state") == "unavailable");
  CHECK(text(rooms, "online.list.error") == "directory_unavailable");
  CHECK(number(rooms, "online.rooms") == 0.0);
  // The sample source is not offered without the developer tools.
  CHECK(rooms.handle("online_sample", {}, true, 0.0));
  CHECK_FALSE(rooms.sample());
  // A code with letters that cannot be in one keeps only those that can.
  CHECK(rooms.handle("online_code", "k7-q2", false, 0.0));
  CHECK(text(rooms, "online.code") == "k7q2");
  CHECK(rooms.handle("online_join_code", {}, false, 0.0));
  CHECK(seen->joinedCode == "k7q2");
  // Too short: a notice instead of a join.
  seen->joinedCode.clear();
  CHECK(rooms.handle("coop_join", "K7", false, 0.0));
  CHECK(seen->joinedCode.empty());
  CHECK(text(rooms, "online.notice") == "bad_code");
  // Without a code, coop_join is the co-op lobby's own code entry, not the rooms'.
  CHECK_FALSE(rooms.handle("coop_join", {}, false, 0.0));
  CHECK_FALSE(rooms.handle("versus_action", {}, false, 0.0));
}

TEST_CASE("the keyboard types into the field the screen gave it, and Enter joins the code") {
  auto backend = std::make_unique<RecordingBackend>();
  RecordingBackend* seen = backend.get();
  OnlineRooms rooms{OnlineRooms::Desc{.directory = std::make_unique<UnlistedDirectory>(),
                                      .backend = std::move(backend),
                                      .sampleDirectory = {},
                                      .sampleBackend = {},
                                      .sampleOffered = false,
                                      .codeLength = 4,
                                      .profile = {}}};
  static_cast<void>(rooms.handle("online_open", {}, false, 0.0));
  CHECK_FALSE(rooms.takesText());
  static_cast<void>(rooms.handle("online_field", "filter", false, 0.0));
  CHECK(rooms.takesText());
  rooms.type("Sala del", false, 0.0);
  rooms.type(" lunes", true, 0.0);
  CHECK(text(rooms, "online.filter") == "Sala del lunes");
  CHECK_FALSE(rooms.takesText());
  static_cast<void>(rooms.handle("online_field", "code", false, 0.0));
  rooms.type("a b!c9z", false, 0.0);
  CHECK(text(rooms, "online.code") == "abc9");
  rooms.type({}, true, 0.0);
  CHECK(seen->joinedCode == "abc9");
  CHECK_FALSE(rooms.takesText());
}

TEST_CASE("the sample list answers after a moment, more than a page, with full rooms") {
  OnlineRooms rooms = sampleRooms();
  static_cast<void>(rooms.handle("online_open", {}, false, 0.0));
  rooms.tick(0.1);
  CHECK(text(rooms, "online.list.state") == "loading");
  rooms.tick(1.0);
  CHECK(text(rooms, "online.list.state") == "ready");
  CHECK(number(rooms, "online.rooms") > 5.0);
  CHECK(flag(rooms, "online.sample"));
  // The third sample room is full (four players) and cannot be joined; its name is a text key.
  CHECK(number(rooms, "online.room.2.capacity") == 4.0);
  CHECK(text(rooms, "online.room.2.state") == "full");
  CHECK_FALSE(flag(rooms, "online.room.2.joinable"));
  CHECK(text(rooms, "online.room.1.state") == "waiting");
  CHECK(text(rooms, "online.room.2.name_key").starts_with("online.sample."));
  CHECK(text(rooms, "online.room.0.name").empty());
}

TEST_CASE("joining a listed sample room puts the player beside its host, and readying shows") {
  OnlineRooms rooms = sampleRooms();
  rooms.tick(1.0);
  const std::string id = text(rooms, "online.room.0.id");
  CHECK(rooms.handle("online_join", id, false, 1.0));
  REQUIRE(flag(rooms, "online.current.active"));
  CHECK_FALSE(flag(rooms, "online.current.host"));
  CHECK(text(rooms, "online.current.phase") == "connected");
  CHECK(number(rooms, "online.current.members") == 2.0);
  CHECK(flag(rooms, "online.member.0.host"));
  CHECK_FALSE(flag(rooms, "online.member.0.ready"));
  CHECK(flag(rooms, "online.member.1.local"));
  CHECK(flag(rooms, "online.current.can_ready"));
  static_cast<void>(rooms.handle("coop_ready", {}, false, 1.0));
  CHECK(flag(rooms, "online.current.ready"));
  CHECK(text(rooms, "online.current.phase") == "ready");
  static_cast<void>(rooms.handle("online_leave", {}, false, 1.0));
  CHECK_FALSE(flag(rooms, "online.current.active"));
  // A room with three players in it: the fourth seat is this player's.
  static_cast<void>(rooms.handle("online_join", text(rooms, "online.room.1.id"), false, 2.0));
  CHECK(number(rooms, "online.current.capacity") == 4.0);
  CHECK(number(rooms, "online.current.members") == 4.0);
  CHECK(flag(rooms, "online.member.3.local"));
}

TEST_CASE("a hosted sample room is public or private as asked, and its guests ready in time") {
  OnlineRooms rooms = sampleRooms();
  static_cast<void>(rooms.handle("coop_host", {}, false, 0.0));
  CHECK_FALSE(flag(rooms, "online.current.listed"));
  CHECK(flag(rooms, "online.current.host"));
  CHECK(text(rooms, "online.current.phase") == "open");
  // The host has no ready mark of its own.
  CHECK_FALSE(flag(rooms, "online.member.0.ready"));
  CHECK_FALSE(flag(rooms, "online.current.can_start"));
  CHECK(number(rooms, "online.current.capacity") == 4.0);
  rooms.tick(2.5);
  CHECK(text(rooms, "online.current.phase") == "connected");
  // The first guest is ready as a second one joins: not everyone is ready yet.
  rooms.tick(4.5);
  CHECK(number(rooms, "online.current.members") == 3.0);
  CHECK(flag(rooms, "online.member.1.ready"));
  CHECK_FALSE(flag(rooms, "online.current.can_start"));
  rooms.tick(6.5);
  CHECK(text(rooms, "online.current.phase") == "ready");
  CHECK(flag(rooms, "online.current.can_start"));
  CHECK(number(rooms, "online.current.members") == 3.0);
  // The sample does not pretend to start a game.
  static_cast<void>(rooms.handle("coop_start", {}, false, 5.0));
  CHECK(text(rooms, "online.notice") == "sample_start");
  // The host changes the mode; a mode rooms cannot play stays as it was.
  static_cast<void>(rooms.handle("online_mode", "versus", false, 5.0));
  CHECK(text(rooms, "online.current.mode") == "versus");
  static_cast<void>(rooms.handle("online_mode", "training", false, 5.0));
  CHECK(text(rooms, "online.current.mode") == "versus");
  static_cast<void>(rooms.handle("online_leave", {}, false, 5.0));
  static_cast<void>(rooms.handle("coop_host", {}, true, 6.0));
  CHECK(flag(rooms, "online.current.listed"));
}

TEST_CASE("a wrong sample code closes the attempt with why, and dismissing clears it") {
  OnlineRooms rooms = sampleRooms();
  rooms.tick(1.0);
  static_cast<void>(rooms.handle("coop_join", "ZZZZ", false, 1.0));
  REQUIRE(flag(rooms, "online.current.active"));
  CHECK(text(rooms, "online.current.phase") == "closed");
  CHECK(text(rooms, "online.current.error") == "wrong_code");
  static_cast<void>(rooms.handle("online_dismiss", {}, false, 1.0));
  CHECK_FALSE(flag(rooms, "online.current.active"));
  // A listed room's code joins it, as a private room would be.
  static_cast<void>(rooms.handle("coop_join", text(rooms, "online.room.1.code"), false, 1.0));
  CHECK(text(rooms, "online.current.phase") == "connected");
  CHECK_FALSE(flag(rooms, "online.current.listed"));
}

TEST_CASE("the quick match joins the first open room of a playable mode, or answers none") {
  auto backend = std::make_unique<RecordingBackend>();
  RecordingBackend* seen = backend.get();
  OnlineRooms rooms{OnlineRooms::Desc{
      .directory = std::make_unique<FixedDirectory>(std::vector{listing("FULL", "campaign", 2),
                                                                listing("VRSS", "versus", 1),
                                                                listing("OPEN", "campaign", 1)}),
      .backend = std::move(backend),
      .sampleDirectory = {},
      .sampleBackend = {},
      .sampleOffered = false,
      .codeLength = 4,
      .profile = {}}};
  // The co-op link plays only the campaign: the versus room is listed but not joinable.
  CHECK(flag(rooms, "online.mode.0.available"));
  CHECK_FALSE(flag(rooms, "online.mode.1.available"));
  CHECK_FALSE(flag(rooms, "online.mode.2.available"));
  CHECK_FALSE(flag(rooms, "online.room.1.joinable"));
  static_cast<void>(rooms.handle("online_join", "VRSS", false, 0.0));
  CHECK(seen->joined.empty());
  CHECK(text(rooms, "online.notice") == "mode_unavailable");
  static_cast<void>(rooms.handle("online_quick", {}, false, 0.0));
  CHECK(seen->joined == "OPEN");
  CHECK(text(rooms, "online.quick").empty());

  OnlineRooms none = sampleRooms(true);
  none.tick(1.0);
  static_cast<void>(none.handle("online_quick", {}, false, 1.0));
  CHECK(text(none, "online.quick") == "none");
  static_cast<void>(none.handle("online_dismiss", {}, false, 1.0));
  CHECK(text(none, "online.quick").empty());
}

TEST_CASE("the profile's name is typed, kept, and is the player's name in a room") {
  OnlineProfile profile;
  CHECK(profile.setName("Ana"));
  auto backend = std::make_unique<RecordingBackend>();
  OnlineRooms rooms{OnlineRooms::Desc{.directory = std::make_unique<UnlistedDirectory>(),
                                      .backend = std::move(backend),
                                      .sampleDirectory = {},
                                      .sampleBackend = {},
                                      .sampleOffered = false,
                                      .codeLength = 4,
                                      .profile = profile}};
  CHECK(text(rooms, "online.profile.name") == "Ana");
  CHECK(number(rooms, "online.icons") == static_cast<double>(OnlineProfile::Icons.size()));
  CHECK(text(rooms, "online.icon.0.id") == "egg");
  // The name field starts from the kept name; erasing it all and saving keeps the old one.
  static_cast<void>(rooms.handle("online_field", "name", false, 0.0));
  CHECK(text(rooms, "online.profile.draft") == "Ana");
  static_cast<void>(rooms.handle("online_profile_name", "", false, 0.0));
  static_cast<void>(rooms.handle("online_profile_save", {}, false, 0.0));
  CHECK(text(rooms, "online.notice") == "name_empty");
  CHECK(text(rooms, "online.profile.name") == "Ana");
  CHECK_FALSE(rooms.takeProfileChanged());
  // Typed letters, then Enter, keep a new name.
  static_cast<void>(rooms.handle("online_field", "name", false, 0.0));
  static_cast<void>(rooms.handle("online_profile_name", "", false, 0.0));
  rooms.type("Bea  ", false, 0.0);
  rooms.type("Luz", true, 0.0);
  CHECK(text(rooms, "online.profile.name") == "Bea Luz");
  CHECK(rooms.takeProfileChanged());
  CHECK_FALSE(rooms.takeProfileChanged());
  static_cast<void>(rooms.handle("online_profile_icon", "dragon", false, 0.0));
  CHECK(text(rooms, "online.profile.icon") == "egg");
  static_cast<void>(rooms.handle("online_profile_icon", "tree", false, 0.0));
  CHECK(text(rooms, "online.profile.icon") == "tree");
  CHECK(rooms.profile().icon() == "tree");
  CHECK(rooms.takeProfileChanged());
  rooms.recordVersus(true);
  CHECK(number(rooms, "online.profile.wins") == 1.0);
  // The player's own seat shows the profile's name.
  static_cast<void>(rooms.handle("coop_host", {}, true, 0.0));
  CHECK(flag(rooms, "online.member.0.local"));
  CHECK(text(rooms, "online.member.0.name") == "Bea Luz");
}

TEST_CASE("a failed room is asked for again as it was asked the first time") {
  auto backend = std::make_unique<RecordingBackend>();
  RecordingBackend* seen = backend.get();
  OnlineRooms rooms{OnlineRooms::Desc{.directory = std::make_unique<UnlistedDirectory>(),
                                      .backend = std::move(backend),
                                      .sampleDirectory = {},
                                      .sampleBackend = {},
                                      .sampleOffered = false,
                                      .codeLength = 4,
                                      .profile = {}}};
  CHECK_FALSE(flag(rooms, "online.can_retry"));
  static_cast<void>(rooms.handle("coop_join", "K7Q2", false, 0.0));
  CHECK(seen->joinedCode == "K7Q2");
  CHECK(flag(rooms, "online.can_retry"));
  seen->joinedCode.clear();
  static_cast<void>(rooms.handle("online_retry", {}, false, 1.0));
  CHECK(seen->joinedCode == "K7Q2");
  static_cast<void>(rooms.handle("coop_host", {}, true, 2.0));
  seen->hosted.reset();
  static_cast<void>(rooms.handle("online_retry", {}, false, 3.0));
  REQUIRE(seen->hosted.has_value());
  CHECK(*seen->hosted);
}

TEST_CASE("getting into a sample room takes a moment the screens show as connecting") {
  OnlineRooms rooms = sampleRooms(false, 1.0);
  rooms.tick(1.0);
  static_cast<void>(rooms.handle("online_join", text(rooms, "online.room.0.id"), false, 1.0));
  REQUIRE(flag(rooms, "online.current.active"));
  CHECK(text(rooms, "online.current.phase") == "connecting");
  rooms.tick(1.5);
  CHECK(text(rooms, "online.current.phase") == "connecting");
  rooms.tick(2.5);
  CHECK(text(rooms, "online.current.phase") == "connected");
}
