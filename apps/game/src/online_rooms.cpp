#include "online_rooms.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <utility>

namespace dwe::app {
namespace {

// How long the sample list takes to answer, as if asking a server.
constexpr double SampleAnswerSeconds = 0.6;
// A sample guest joins a hosted sample room every this long, and is ready as long after joining.
constexpr double SampleGuestSeconds = 2.0;
// How many sample guests join a hosted sample room: one seat stays free, to show a waiting one.
constexpr int SampleGuests = MaxRoomPlayers - 2;
constexpr std::string_view SampleCode = "K7Q2";

RoomListing sampleRoom(std::string_view code, int number, std::string_view mode, int players,
                       int pingMs) {
  return {.roomId = std::string{code},
          .joinCode = std::string{code},
          .name = {},
          .message = {},
          .hostName = {},
          .players = players,
          .capacity = MaxRoomPlayers,
          .createdAt = 0.0,
          .state = players < MaxRoomPlayers ? ListingState::Waiting : ListingState::Full,
          .nameKey = std::format("online.sample.room_{}", number),
          .mode = std::string{mode},
          .pingMs = pingMs};
}

std::vector<RoomListing> sampleRooms() {
  return {
      sampleRoom("H3MP", 1, "campaign", 1, 38),  sampleRoom("T9RX", 2, "campaign", 3, 72),
      sampleRoom("B4NK", 3, "versus", 4, 64),    sampleRoom("W2CJ", 4, "campaign", 2, 140),
      sampleRoom("Z8LD", 5, "versus", 1, 210),   sampleRoom("P5QF", 6, "campaign", 4, 95),
      sampleRoom("Y6VS", 7, "campaign", 1, 320),
  };
}

RoomMember sampleMember(int number, bool host, bool ready) {
  return {.name = {},
          .nameKey = std::format("online.sample.player_{}", number),
          .host = host,
          .ready = ready,
          .local = false,
          .icon = std::string{OnlineProfile::Icons.at(static_cast<std::size_t>(number) %
                                                      OnlineProfile::Icons.size())},
          .arriving = false};
}

std::string upper(std::string_view text) {
  std::string out{text};
  std::ranges::transform(out, out.begin(), [](unsigned char letter) {
    return static_cast<char>(std::toupper(letter));
  });
  return out;
}

bool codeCharacter(char value) {
  return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z');
}

// The letters a room filter keeps: printable text, one line.
bool filterCharacter(char value) {
  return static_cast<unsigned char>(value) >= ' ' && value != '\x7f';
}

constexpr std::size_t FilterLength = 24;

} // namespace

std::string_view listingStateName(ListingState state) {
  switch (state) {
  case ListingState::Waiting:
    return "waiting";
  case ListingState::Full:
    return "full";
  case ListingState::Starting:
    return "starting";
  case ListingState::InProgress:
    return "in_progress";
  }
  return "full";
}

RoomView closedRoom(std::string code, std::string error) {
  return {.code = std::move(code),
          .name = {},
          .nameKey = {},
          .isHost = false,
          .listed = false,
          .phase = RoomPhase::Closed,
          .capacity = 2,
          .members = {},
          .error = std::move(error),
          .canStart = false,
          .canReady = false,
          .localReady = false,
          .mode = {},
          .inProgress = false,
          .openToNewPlayers = true};
}

void SampleDirectory::refresh(double nowSeconds) {
  askedAt_ = nowSeconds;
  state_ = DirectoryState::Loading;
}

void SampleDirectory::tick(double nowSeconds) {
  if (!askedAt_) {
    refresh(nowSeconds);
  }
  if (state_ == DirectoryState::Loading && nowSeconds - *askedAt_ >= SampleAnswerSeconds) {
    rooms_ = empty_ ? std::vector<RoomListing>{} : sampleRooms();
    state_ = DirectoryState::Ready;
  }
}

void SampleRoomBackend::host(bool listed, double nowSeconds) {
  room_ = RoomView{.code = std::string{SampleCode},
                   .name = {},
                   .nameKey = "online.sample.my_room",
                   .isHost = true,
                   .listed = listed,
                   .phase = RoomPhase::Open,
                   .capacity = MaxRoomPlayers,
                   .members = {unnamedMember(true, false, true)},
                   .error = {},
                   .canStart = false,
                   .canReady = false,
                   .localReady = false,
                   .mode = {},
                   .inProgress = false,
                   .openToNewPlayers = true};
  openedAt_ = nowSeconds;
  arrive(nowSeconds);
}

void SampleRoomBackend::arrive(double nowSeconds) {
  pending_.reset();
  if (connectSeconds_ <= 0.0 || !room_) {
    return;
  }
  pending_ = room_;
  RoomView& connecting = *room_;
  connecting.phase = RoomPhase::Connecting;
  connecting.error.clear();
  connecting.members.clear();
  connecting.canStart = false;
  connecting.canReady = false;
  if (connecting.isHost) {
    // The server has not made the room yet: no code to share.
    connecting.code.clear();
  }
  openedAt_ = nowSeconds;
}

void SampleRoomBackend::join(const RoomListing& listing, double nowSeconds) {
  joinNow(listing, nowSeconds);
  arrive(nowSeconds);
}

void SampleRoomBackend::joinNow(const RoomListing& listing, double nowSeconds) {
  if (!hasSeat(listing)) {
    room_ = closedRoom(listing.joinCode, "room_full");
    return;
  }
  // Its host, the guests already in it (the first of them ready) and this player.
  std::vector<RoomMember> members{sampleMember(1, true, false)};
  for (int guest = 2; guest <= listing.players; ++guest) {
    members.push_back(sampleMember(guest, false, guest == 2));
  }
  members.push_back(unnamedMember(false, false, true));
  room_ = RoomView{.code = listing.joinCode,
                   .name = listing.name,
                   .nameKey = listing.nameKey,
                   .isHost = false,
                   .listed = true,
                   .phase = RoomPhase::Connected,
                   .capacity = listing.capacity,
                   .members = std::move(members),
                   .error = {},
                   .canStart = false,
                   .canReady = true,
                   .localReady = false,
                   .mode = listing.mode,
                   .inProgress = listing.state == ListingState::InProgress,
                   .openToNewPlayers = true};
  openedAt_ = nowSeconds;
}

void SampleRoomBackend::joinByCode(std::string_view code, double nowSeconds) {
  const std::string wanted = upper(code);
  const auto known = known_->rooms();
  const auto found = std::ranges::find(known, wanted, &RoomListing::joinCode);
  if (found == known.end()) {
    room_ = closedRoom(wanted, "wrong_code");
  } else {
    joinNow(*found, nowSeconds);
    if (room_ && room_->phase != RoomPhase::Closed) {
      // Joined by its code: as a private room would be.
      room_->listed = false;
    }
  }
  arrive(nowSeconds);
}

void SampleRoomBackend::setReady(bool ready) {
  if (!room_ || room_->isHost || !room_->canReady) {
    return;
  }
  room_->localReady = ready;
  for (RoomMember& member : room_->members) {
    if (member.local) {
      member.ready = ready;
    }
  }
  room_->phase = ready ? RoomPhase::Ready : RoomPhase::Connected;
}

std::optional<std::string> SampleRoomBackend::start() {
  return std::string{"sample_start"};
}

void SampleRoomBackend::tick(double nowSeconds) {
  if (pending_) {
    if (nowSeconds - openedAt_ < connectSeconds_) {
      return;
    }
    room_ = std::move(pending_);
    pending_.reset();
    openedAt_ = nowSeconds;
  }
  if (!room_ || !room_->isHost || room_->phase == RoomPhase::Closed) {
    return;
  }
  const double open = nowSeconds - openedAt_;
  const auto guests = static_cast<int>(room_->members.size()) - 1;
  if (guests < SampleGuests && open >= (guests + 1) * SampleGuestSeconds) {
    room_->members.push_back(sampleMember(guests + 2, false, false));
  }
  bool allReady = true;
  for (std::size_t guest = 1; guest < room_->members.size(); ++guest) {
    const double joined = static_cast<double>(guest) * SampleGuestSeconds;
    room_->members[guest].ready = open >= joined + SampleGuestSeconds;
    allReady = allReady && room_->members[guest].ready;
  }
  const bool anyGuest = room_->members.size() > 1;
  room_->canStart = anyGuest && allReady;
  room_->phase = !anyGuest ? RoomPhase::Open : allReady ? RoomPhase::Ready : RoomPhase::Connected;
}

OnlineRooms::OnlineRooms(Desc desc)
    : desc_{std::move(desc)}, sampleOffered_{desc_.sampleOffered}, profile_{desc_.profile} {}

void OnlineRooms::recordVersus(bool won) {
  profile_.recordVersus(won);
  profileChanged_ = true;
}

void OnlineRooms::commitName() {
  field_ = OnlineField::None;
  if (profile_.setName(nameDraft_)) {
    profileChanged_ = true;
  } else {
    notice_ = "name_empty";
  }
  nameDraft_ = profile_.named() ? profile_.name() : std::string{};
}

void OnlineRooms::retry(double nowSeconds) {
  quick_.clear();
  notice_.clear();
  backend().leave();
  switch (attempt_.kind) {
  case Attempt::Kind::Host:
    mode_ = "campaign";
    backend().host(attempt_.listed, nowSeconds);
    break;
  case Attempt::Kind::Listing:
    mode_ = attempt_.listing.mode;
    backend().join(attempt_.listing, nowSeconds);
    break;
  case Attempt::Kind::Code:
    mode_ = "campaign";
    backend().joinByCode(attempt_.code, nowSeconds);
    break;
  case Attempt::Kind::None:
    break;
  }
}

RoomDirectory& OnlineRooms::directory() const {
  return sample_ && desc_.sampleDirectory ? *desc_.sampleDirectory : *desc_.directory;
}

RoomBackend& OnlineRooms::backend() const {
  return sample_ && desc_.sampleBackend ? *desc_.sampleBackend : *desc_.backend;
}

void OnlineRooms::setSample(bool on, double nowSeconds) {
  if (on == sample_ || (on && (!desc_.sampleDirectory || !desc_.sampleBackend))) {
    return;
  }
  backend().leave();
  sample_ = on;
  quick_.clear();
  notice_.clear();
  directory().refresh(nowSeconds);
}

bool OnlineRooms::modeAvailable(std::string_view mode) const {
  const auto found = std::ranges::find(RoomModes, mode, &RoomMode::id);
  return found != RoomModes.end() && found->exists && backend().playable(mode);
}

bool OnlineRooms::handle(std::string_view name, const std::string& text, bool flag,
                         double nowSeconds) {
  // The room steps the co-op lobby already takes (CoopScreen: coop_host, coop_join with a code,
  // coop_ready, coop_start) go through the backend, which hands them to the co-op lobby (or plays
  // them on the sample rooms); a coop_join without a code is the lobby's own code entry.
  if (name == "coop_host") {
    quick_.clear();
    notice_.clear();
    mode_ = "campaign";
    attempt_ = Attempt{.kind = Attempt::Kind::Host, .listed = flag, .listing = {}, .code = {}};
    backend().host(flag, nowSeconds);
    return true;
  }
  if (name == "coop_join") {
    if (text.empty()) {
      return false;
    }
    code_.clear();
    static_cast<void>(handle("online_code", text, false, nowSeconds));
    joinTyped(nowSeconds);
    return true;
  }
  if (name == "coop_ready") {
    // A toggle, as the lobby's.
    if (const auto room = backend().room()) {
      backend().setReady(!room->localReady);
    }
    return true;
  }
  if (name == "coop_start") {
    if (auto refused = backend().start()) {
      notice_ = std::move(*refused);
    }
    return true;
  }
  if (!name.starts_with("online_")) {
    return false;
  }
  if (name == "online_open") {
    open_ = true;
    field_ = OnlineField::None;
    quick_.clear();
    notice_.clear();
    if (!inRoom()) {
      directory().refresh(nowSeconds);
    }
  } else if (name == "online_close") {
    open_ = false;
    field_ = OnlineField::None;
  } else if (name == "online_refresh") {
    quick_.clear();
    directory().refresh(nowSeconds);
  } else if (name == "online_field") {
    field_ = text == "filter" ? OnlineField::Filter
             : text == "code" ? OnlineField::Code
             : text == "name" ? OnlineField::Name
                              : OnlineField::None;
    if (field_ == OnlineField::Name) {
      nameDraft_ = profile_.named() ? profile_.name() : std::string{};
    }
  } else if (name == "online_profile_name") {
    // The name being typed, as the screen has it now (an erased letter).
    nameDraft_ = OnlineProfile::cleanName(text);
    if (text.ends_with(' ') && !nameDraft_.empty()) {
      nameDraft_.push_back(' ');
    }
  } else if (name == "online_profile_save") {
    commitName();
  } else if (name == "online_profile_icon") {
    if (profile_.setIcon(text)) {
      profileChanged_ = true;
    }
  } else if (name == "online_profile_relay") {
    if (profile_.relayOnly() != flag) {
      profile_.setRelayOnly(flag);
      profileChanged_ = true;
    }
  } else if (name == "online_retry") {
    retry(nowSeconds);
  } else if (name == "online_filter") {
    filter_.clear();
    for (const char value : text) {
      if (filterCharacter(value) && filter_.size() < FilterLength) {
        filter_.push_back(value);
      }
    }
  } else if (name == "online_code") {
    code_.clear();
    for (const char value : text) {
      if (codeCharacter(value) && code_.size() < desc_.codeLength) {
        code_.push_back(value);
      }
    }
  } else if (name == "online_join") {
    quick_.clear();
    notice_.clear();
    const auto rooms = directory().rooms();
    const auto found = std::ranges::find(rooms, text, &RoomListing::roomId);
    if (found == rooms.end()) {
      notice_ = "room_gone";
    } else if (!hasSeat(*found)) {
      notice_ = "room_full";
    } else if (!modeAvailable(found->mode)) {
      notice_ = "mode_unavailable";
    } else {
      mode_ = found->mode;
      attempt_ =
          Attempt{.kind = Attempt::Kind::Listing, .listed = true, .listing = *found, .code = {}};
      backend().join(*found, nowSeconds);
    }
  } else if (name == "online_join_code") {
    joinTyped(nowSeconds);
  } else if (name == "online_quick") {
    quickMatch(nowSeconds);
  } else if (name == "online_open_to_join") {
    backend().setOpenToNewPlayers(flag);
  } else if (name == "online_mode") {
    const auto room = backend().room();
    if (room && room->isHost && modeAvailable(text)) {
      mode_ = text;
    }
  } else if (name == "online_leave") {
    backend().leave();
    notice_.clear();
    directory().refresh(nowSeconds);
  } else if (name == "online_sample") {
    if (sampleOffered_) {
      setSample(flag, nowSeconds);
    }
  } else if (name == "online_dismiss") {
    notice_.clear();
    quick_.clear();
    // A room that failed to open or closed stays until the player has read why.
    if (const auto room = backend().room(); room && room->phase == RoomPhase::Closed) {
      backend().leave();
    }
  } else {
    return false;
  }
  return true;
}

void OnlineRooms::rejoin(std::string_view code, double nowSeconds) {
  static_cast<void>(handle("online_open", {}, false, nowSeconds));
  static_cast<void>(handle("online_code", std::string{code}, false, nowSeconds));
  joinTyped(nowSeconds);
}

void OnlineRooms::joinTyped(double nowSeconds) {
  quick_.clear();
  notice_.clear();
  field_ = OnlineField::None;
  if (code_.size() != desc_.codeLength) {
    notice_ = "bad_code";
    return;
  }
  mode_ = "campaign";
  attempt_ = Attempt{.kind = Attempt::Kind::Code, .listed = false, .listing = {}, .code = code_};
  backend().joinByCode(code_, nowSeconds);
}

void OnlineRooms::quickMatch(double nowSeconds) {
  notice_.clear();
  quick_.clear();
  if (directory().state() != DirectoryState::Ready) {
    quick_ = "none";
    return;
  }
  for (const RoomListing& room : directory().rooms()) {
    if (hasSeat(room) && modeAvailable(room.mode)) {
      mode_ = room.mode;
      attempt_ =
          Attempt{.kind = Attempt::Kind::Listing, .listed = true, .listing = room, .code = {}};
      backend().join(room, nowSeconds);
      return;
    }
  }
  quick_ = "none";
}

void OnlineRooms::type(std::string_view text, bool enter, double nowSeconds) {
  if (!takesText()) {
    return;
  }
  if (field_ == OnlineField::Name) {
    // Letters as typed (a letter may take several bytes); cleanName keeps the name's length.
    std::string draft = nameDraft_;
    draft.append(text);
    nameDraft_ = OnlineProfile::cleanName(draft);
    if (draft.ends_with(' ') && !nameDraft_.empty()) {
      nameDraft_.push_back(' ');
    }
    if (enter) {
      commitName();
    }
    return;
  }
  std::string& typed = field_ == OnlineField::Code ? code_ : filter_;
  const std::size_t limit = field_ == OnlineField::Code ? desc_.codeLength : FilterLength;
  for (const char value : text) {
    const bool fits = field_ == OnlineField::Code ? codeCharacter(value) : filterCharacter(value);
    if (fits && typed.size() < limit) {
      typed.push_back(value);
    }
  }
  if (enter) {
    if (field_ == OnlineField::Code) {
      joinTyped(nowSeconds);
    } else {
      field_ = OnlineField::None;
    }
  }
}

void OnlineRooms::tick(double nowSeconds) {
  directory().tick(nowSeconds);
  backend().tick(nowSeconds);
}

screens::ScreenSettings OnlineRooms::settings() const {
  using screens::listEntryName;
  screens::ScreenSettings out;
  const auto add = [&out](std::string name, ui::UiValue value) {
    out.push_back({.name = std::move(name), .value = std::move(value)});
  };
  add("online.open", open_);
  add("online.sample", sample_);
  add("online.sample_offered", sampleOffered_);
  const DirectoryState state = directory().state();
  add("online.list.state", std::string{state == DirectoryState::Loading ? "loading"
                                       : state == DirectoryState::Ready ? "ready"
                                                                        : "unavailable"});
  add("online.list.error", std::string{directory().error()});
  const auto rooms = directory().rooms();
  add("online.rooms", static_cast<double>(rooms.size()));
  for (std::size_t index = 0; index < rooms.size(); ++index) {
    const RoomListing& room = rooms[index];
    add(listEntryName("online.room", index, "id"), room.roomId);
    add(listEntryName("online.room", index, "code"), room.joinCode);
    add(listEntryName("online.room", index, "name"), room.name);
    add(listEntryName("online.room", index, "name_key"), room.nameKey);
    add(listEntryName("online.room", index, "message"), room.message);
    add(listEntryName("online.room", index, "host_name"), room.hostName);
    add(listEntryName("online.room", index, "state"), std::string{listingStateName(room.state)});
    add(listEntryName("online.room", index, "mode"), room.mode);
    add(listEntryName("online.room", index, "players"), static_cast<double>(room.players));
    add(listEntryName("online.room", index, "capacity"), static_cast<double>(room.capacity));
    add(listEntryName("online.room", index, "ping"), static_cast<double>(room.pingMs));
    add(listEntryName("online.room", index, "joinable"), hasSeat(room) && modeAvailable(room.mode));
  }
  add("online.field", std::string{field_ == OnlineField::Filter ? "filter"
                                  : field_ == OnlineField::Code ? "code"
                                  : field_ == OnlineField::Name ? "name"
                                                                : ""});
  add("online.profile.name", profile_.name());
  add("online.profile.named", profile_.named());
  add("online.profile.icon", profile_.icon());
  add("online.profile.relay_only", profile_.relayOnly());
  add("online.profile.wins", static_cast<double>(profile_.wins()));
  add("online.profile.losses", static_cast<double>(profile_.losses()));
  add("online.profile.draft", nameDraft_);
  add("online.icons", static_cast<double>(OnlineProfile::Icons.size()));
  for (std::size_t index = 0; index < OnlineProfile::Icons.size(); ++index) {
    add(listEntryName("online.icon", index, "id"), std::string{OnlineProfile::Icons.at(index)});
  }
  add("online.can_retry", attempt_.kind != Attempt::Kind::None);
  add("online.filter", filter_);
  add("online.code", code_);
  add("online.code_length", static_cast<double>(desc_.codeLength));
  add("online.quick", quick_);
  add("online.notice", notice_);
  add("online.modes", static_cast<double>(RoomModes.size()));
  for (std::size_t index = 0; index < RoomModes.size(); ++index) {
    add(listEntryName("online.mode", index, "id"), std::string{RoomModes[index].id});
    add(listEntryName("online.mode", index, "available"), modeAvailable(RoomModes[index].id));
  }
  const auto room = backend().room();
  add("online.current.active", room.has_value());
  if (room) {
    add("online.current.code", room->code);
    add("online.current.name", room->name);
    add("online.current.name_key", room->nameKey);
    add("online.current.host", room->isHost);
    add("online.current.listed", room->listed);
    add("online.current.phase", std::string{roomPhaseName(room->phase)});
    add("online.current.mode", room->isHost || room->mode.empty() ? mode_ : room->mode);
    add("online.current.capacity", static_cast<double>(room->capacity));
    add("online.current.error", room->error);
    add("online.current.can_start", room->canStart);
    add("online.current.can_ready", room->canReady);
    add("online.current.ready", room->localReady);
    add("online.current.members", static_cast<double>(room->members.size()));
    add("online.current.in_progress", room->inProgress);
    add("online.current.open_to_join", room->openToNewPlayers);
    for (std::size_t index = 0; index < room->members.size(); ++index) {
      const RoomMember& member = room->members[index];
      // This player shows with its profile's name and icon; the others with theirs when known.
      add(listEntryName("online.member", index, "name"),
          member.local && member.name.empty() ? profile_.name() : member.name);
      add(listEntryName("online.member", index, "icon"),
          member.local ? profile_.icon()
                       : (member.icon.empty() ? std::string{"egg"} : member.icon));
      add(listEntryName("online.member", index, "name_key"), member.nameKey);
      add(listEntryName("online.member", index, "host"), member.host);
      add(listEntryName("online.member", index, "ready"), member.ready);
      add(listEntryName("online.member", index, "local"), member.local);
      add(listEntryName("online.member", index, "arriving"), member.arriving);
    }
  }
  return out;
}

} // namespace dwe::app
