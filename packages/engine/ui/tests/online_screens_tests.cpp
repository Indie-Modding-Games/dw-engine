// The online screens (content/packs/game/ui/online_hub, online_profile, online_status,
// online_rooms, online_room and coop_hud; docs/modules/apps/game.md, «Online rooms»), fed the
// settings the game publishes: each stays whole inside the window at every UI scale under every
// skin, a long player name is squeezed into its column and a long room name is cut with "...".

#include <doctest/doctest.h>

#include <array>
#include <format>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

constexpr std::array<std::string_view, 4> Skins{"original", "modern", "pixel", "devvago"};
constexpr std::string_view LongName = "WWWWWWWWWWWW";

struct Window {
  int width = 0;
  int height = 0;
  float scale = 1.0F;
};

constexpr std::array Windows{
    Window{.width = 1280, .height = 720, .scale = 1.0F},
    Window{.width = 1920, .height = 1080, .scale = 1.5F},
    Window{.width = 1024, .height = 768, .scale = 1.5F},
    Window{.width = 800, .height = 600, .scale = 2.0F},
};

void bumpRevision(Ui& ui) {
  const UiValue current = ui.setting("online.revision");
  const double* count = std::get_if<double>(&current);
  ui.setSetting("online.revision", (count == nullptr ? 0.0 : *count) + 1.0);
}

// The player's profile as the game publishes it, named `name`, with the six icons.
void publishProfile(Ui& ui, std::string_view name) {
  ui.setSetting("online.profile.name", std::string{name});
  ui.setSetting("online.profile.named", true);
  ui.setSetting("online.profile.icon", std::string{"crown"});
  ui.setSetting("online.profile.wins", 12.0);
  ui.setSetting("online.profile.losses", 3.0);
  ui.setSetting("online.profile.draft", std::string{});
  constexpr std::array<std::string_view, 6> Icons{"egg",    "device", "crown",
                                                  "swords", "tree",   "dumbbell"};
  ui.setSetting("online.icons", static_cast<double>(Icons.size()));
  for (std::size_t icon = 0; icon < Icons.size(); ++icon) {
    ui.setSetting(std::format("online.icon.{}.id", icon), std::string{Icons.at(icon)});
  }
}

// The list as the game publishes it: seven rooms, more than a page, the first one named `first`.
void publishList(Ui& ui, std::string_view first) {
  ui.setSetting("online.open", true);
  ui.setSetting("online.sample", false);
  ui.setSetting("online.sample_offered", false);
  ui.setSetting("online.list.state", std::string{"ready"});
  ui.setSetting("online.list.error", std::string{});
  constexpr int Rooms = 7;
  ui.setSetting("online.rooms", static_cast<double>(Rooms));
  for (int room = 0; room < Rooms; ++room) {
    const std::string prefix = std::format("online.room.{}.", room);
    ui.setSetting(prefix + "id", std::format("R{}AB", room + 2));
    ui.setSetting(prefix + "name",
                  room == 0 ? std::string{first} : std::format("Room number {}", room + 1));
    ui.setSetting(prefix + "name_key", std::string{});
    ui.setSetting(prefix + "mode", std::string{room % 3 == 2 ? "versus" : "campaign"});
    ui.setSetting(prefix + "players", room % 4 == 3 ? 2.0 : 1.0);
    ui.setSetting(prefix + "capacity", 2.0);
    ui.setSetting(prefix + "ping", 40.0 + 60.0 * room);
    ui.setSetting(prefix + "joinable", room % 4 != 3);
  }
  ui.setSetting("online.field", std::string{});
  ui.setSetting("online.filter", std::string{});
  ui.setSetting("online.code", std::string{});
  ui.setSetting("online.code_length", 4.0);
  ui.setSetting("online.quick", std::string{});
  ui.setSetting("online.notice", std::string{});
  constexpr std::array<std::string_view, 4> Modes{"campaign", "versus", "training", "free"};
  ui.setSetting("online.modes", static_cast<double>(Modes.size()));
  for (std::size_t mode = 0; mode < Modes.size(); ++mode) {
    ui.setSetting(std::format("online.mode.{}.id", mode), std::string{Modes.at(mode)});
    ui.setSetting(std::format("online.mode.{}.available", mode), mode == 0);
  }
  ui.setSetting("online.current.active", false);
  ui.setSetting("online.can_retry", false);
  publishProfile(ui, "Player");
  bumpRevision(ui);
}

// The room the player is in, as its host or as a guest, with two players, the other named `other`.
void publishRoom(Ui& ui, bool host, std::string_view other) {
  publishList(ui, "Room");
  ui.setSetting("online.current.active", true);
  ui.setSetting("online.current.code", std::string{"K7Q2"});
  ui.setSetting("online.current.name", std::string{host ? "" : other});
  ui.setSetting("online.current.name_key", std::string{});
  ui.setSetting("online.current.host", host);
  ui.setSetting("online.current.listed", true);
  ui.setSetting("online.current.phase", std::string{"connected"});
  ui.setSetting("online.current.mode", std::string{"campaign"});
  ui.setSetting("online.current.capacity", 2.0);
  ui.setSetting("online.current.error", std::string{});
  ui.setSetting("online.current.can_start", false);
  ui.setSetting("online.current.can_ready", !host);
  ui.setSetting("online.current.ready", false);
  ui.setSetting("online.current.members", 2.0);
  for (int member = 0; member < 2; ++member) {
    const std::string prefix = std::format("online.member.{}.", member);
    const bool local = (member == 0) == host;
    ui.setSetting(prefix + "name", local ? std::string{} : std::string{other});
    ui.setSetting(prefix + "name_key", std::string{});
    ui.setSetting(prefix + "host", member == 0);
    ui.setSetting(prefix + "ready", false);
    ui.setSetting(prefix + "local", local);
  }
  bumpRevision(ui);
}

// A room the player asked for that is still being set up (`phase` "connecting") or that failed
// ("closed", with a retry on offer).
void publishPending(Ui& ui, std::string_view phase) {
  publishRoom(ui, true, "Friend");
  ui.setSetting("online.current.phase", std::string{phase});
  const bool closed = phase == "closed";
  ui.setSetting("online.current.error", std::string{closed ? "server_unreachable" : ""});
  ui.setSetting("online.can_retry", closed);
  bumpRevision(ui);
}

void checkInside(const Ui::Rect& inner, const Ui::Rect& outer) {
  constexpr float Tolerance = 0.5F;
  CHECK(inner.left >= outer.left - Tolerance);
  CHECK(inner.top >= outer.top - Tolerance);
  CHECK(inner.right <= outer.right + Tolerance);
  CHECK(inner.bottom <= outer.bottom + Tolerance);
}

Ui::Rect windowRect(const Window& window) {
  return {.left = 0.0F,
          .top = 0.0F,
          .right = static_cast<float>(window.width),
          .bottom = static_cast<float>(window.height)};
}

bool squeezed(const Ui& ui, std::string_view document, std::string_view id) {
  return ui.styleProperty(document, id, "transform").contains("scale");
}

// The drawn text ends inside its column (squeezed when it needed to be).
void checkFits(const Ui& ui, std::string_view document, std::string_view id) {
  CAPTURE(id);
  const auto box = ui.elementDrawnRect(document, id);
  const auto text = ui.elementDrawnRect(document, std::format("{}-text", id));
  REQUIRE(box.has_value());
  REQUIRE(text.has_value());
  CHECK(text->right > text->left);
  CHECK(text->right <= box->right + 0.5F);
}

} // namespace

TEST_CASE("the online search stays inside the window at every UI scale under every skin") {
  for (const std::string_view skin : Skins) {
    for (const Window& window : Windows) {
      CAPTURE(skin);
      CAPTURE(window.width);
      CAPTURE(window.scale);
      Screen screen{"online_rooms", "es"};
      screen.skin(skin);
      screen.ui->resize(window.width, window.height);
      screen.ui->setScale(window.scale);
      publishList(*screen.ui, "Room");
      screen.ui->show("online_rooms", true);
      screen.frames(30);
      for (const std::string_view id :
           {"stage", "list-box", "search-box", "code-box", "actions-box", "row-5", "prompts"}) {
        CAPTURE(id);
        REQUIRE(screen.ui->elementShown("online_rooms", id));
        const auto drawn = screen.ui->elementDrawnRect("online_rooms", id);
        REQUIRE(drawn.has_value());
        checkInside(*drawn, windowRect(window));
      }
    }
  }
}

TEST_CASE("the online room stays inside the window at every UI scale, as host and as guest") {
  for (const std::string_view skin : Skins) {
    for (const bool host : {true, false}) {
      for (const Window& window : Windows) {
        CAPTURE(skin);
        CAPTURE(host);
        CAPTURE(window.width);
        CAPTURE(window.scale);
        Screen screen{"online_room", "es"};
        screen.skin(skin);
        screen.ui->resize(window.width, window.height);
        screen.ui->setScale(window.scale);
        publishRoom(*screen.ui, host, "Friend");
        screen.ui->show("online_room", true);
        screen.frames(30);
        REQUIRE(screen.ui->isVisible("online_room"));
        CHECK(screen.ui->elementShown("online_room", host ? "modes-box" : "mode-card"));
        CHECK_FALSE(screen.ui->elementShown("online_room", host ? "mode-card" : "modes-box"));
        for (const std::string_view id :
             {"stage", "info-box", "players-box", "start", "leave", "code-chip", "toast"}) {
          CAPTURE(id);
          const auto drawn = screen.ui->elementDrawnRect("online_room", id);
          REQUIRE(drawn.has_value());
          checkInside(*drawn, windowRect(window));
        }
        // The host has no ready mark; the code to share shows for both.
        CHECK(screen.ui->elementHasClass("online_room", "member-1", "host"));
        CHECK_FALSE(screen.ui->elementHasClass("online_room", "member-1", "ready"));
        CHECK(screen.ui->elementText("online_room", "chip-code") == "K7Q2");
      }
    }
  }
}

TEST_CASE("a long room or player name is squeezed into its column, a short one is not") {
  for (const std::string_view skin : Skins) {
    CAPTURE(skin);
    {
      Screen screen{"online_rooms", "es"};
      screen.skin(skin);
      publishList(*screen.ui, std::format("{} {}", LongName, LongName));
      screen.ui->show("online_rooms", true);
      screen.frames(30);
      checkFits(*screen.ui, "online_rooms", "row-1-name");
      CHECK_FALSE(squeezed(*screen.ui, "online_rooms", "row-2-name-text"));
    }
    {
      Screen screen{"online_room", "es"};
      screen.skin(skin);
      publishRoom(*screen.ui, false, LongName);
      screen.ui->show("online_room", true);
      screen.frames(30);
      // The host is the other player: a long name in the players column and on the card.
      checkFits(*screen.ui, "online_room", "member-1-name");
      checkFits(*screen.ui, "online_room", "info-host");
      CHECK_FALSE(squeezed(*screen.ui, "online_room", "member-2-name-text"));
    }
  }
}

TEST_CASE(
    "the multiplayer hub, the profile and the status screen stay inside the window under every skin") {
  for (const std::string_view skin : Skins) {
    for (const Window& window : Windows) {
      CAPTURE(skin);
      CAPTURE(window.width);
      CAPTURE(window.scale);
      const auto check = [&](std::string_view document, std::string_view phase,
                             std::initializer_list<std::string_view> ids) {
        CAPTURE(document);
        CAPTURE(phase);
        Screen screen{document, "es"};
        screen.skin(skin);
        screen.ui->resize(window.width, window.height);
        screen.ui->setScale(window.scale);
        publishList(*screen.ui, "Room");
        if (!phase.empty()) {
          publishPending(*screen.ui, phase);
        }
        screen.ui->show(document, true);
        screen.frames(30);
        REQUIRE(screen.ui->isVisible(document));
        for (const std::string_view id : ids) {
          CAPTURE(id);
          REQUIRE(screen.ui->elementShown(document, id));
          const auto drawn = screen.ui->elementDrawnRect(document, id);
          REQUIRE(drawn.has_value());
          checkInside(*drawn, windowRect(window));
        }
      };
      check("online_hub", "", {"panel", "player-card", "entry-rooms", "entry-back", "prompts"});
      check("online_profile", "",
            {"panel", "name-field", "icon-6", "record-row", "profile-back", "prompts"});
      check("online_status", "connecting", {"panel", "status-icon", "back", "prompts"});
      check("online_status", "closed", {"panel", "status-text", "retry", "back", "prompts"});
    }
  }
}

TEST_CASE("the connecting screen offers only to cancel, a failed room offers to retry") {
  for (const std::string_view skin : Skins) {
    CAPTURE(skin);
    Screen screen{"online_status", "es"};
    screen.skin(skin);
    publishPending(*screen.ui, "connecting");
    screen.ui->show("online_status", true);
    screen.frames(10);
    CHECK(screen.ui->elementShown("online_status", "back"));
    CHECK_FALSE(screen.ui->elementShown("online_status", "retry"));
    // Only what changes when it fails: a whole new room would show the lobby for a moment.
    screen.ui->setSetting("online.current.phase", std::string{"closed"});
    screen.ui->setSetting("online.current.error", std::string{"server_unreachable"});
    screen.ui->setSetting("online.can_retry", true);
    bumpRevision(*screen.ui);
    screen.frames(10);
    REQUIRE(screen.ui->isVisible("online_status"));
    CHECK(screen.ui->elementShown("online_status", "retry"));
    CHECK_FALSE(screen.ui->elementText("online_status", "status-text").empty());
  }
}

TEST_CASE("the co-op notice stays inside the window under every skin") {
  for (const std::string_view skin : Skins) {
    for (const Window& window : Windows) {
      CAPTURE(skin);
      CAPTURE(window.width);
      CAPTURE(window.scale);
      Screen screen{"coop_hud", "es"};
      screen.skin(skin);
      screen.ui->resize(window.width, window.height);
      screen.ui->setScale(window.scale);
      screen.ui->setSetting("coop_hud.tag.visible", true);
      screen.ui->setSetting("coop_hud.tag.x", 50.0);
      screen.ui->setSetting("coop_hud.tag.y", 50.0);
      screen.ui->setSetting("coop_hud.tag.name", std::string{LongName});
      screen.ui->setSetting("coop_hud.notice.shown", true);
      screen.ui->setSetting("coop_hud.notice.text", std::string{"online.coop.disconnected"});
      screen.ui->setSetting("coop_hud.notice.name", std::string{LongName});
      screen.ui->show("coop_hud", true);
      screen.frames(10);
      for (const std::string_view id : {"tag", "notice"}) {
        CAPTURE(id);
        REQUIRE(screen.ui->elementShown("coop_hud", id));
        const auto drawn = screen.ui->elementDrawnRect("coop_hud", id);
        REQUIRE(drawn.has_value());
        checkInside(*drawn, windowRect(window));
      }
      CHECK(screen.ui->elementText("coop_hud", "notice-text").contains(LongName));
    }
  }
}

TEST_CASE("a long player name is squeezed on the hub and the profile, a long room name is cut") {
  const std::string longRoom = std::format("{} {} {}", LongName, LongName, LongName);
  for (const std::string_view skin : Skins) {
    CAPTURE(skin);
    for (const auto& [document, id] :
         {std::pair{"online_hub", "player-name"}, std::pair{"online_profile", "head-name"}}) {
      CAPTURE(document);
      Screen screen{document, "es"};
      screen.skin(skin);
      publishList(*screen.ui, "Room");
      publishProfile(*screen.ui, LongName);
      bumpRevision(*screen.ui);
      screen.ui->show(document, true);
      screen.frames(30);
      checkFits(*screen.ui, document, id);
    }
    {
      Screen screen{"online_room", "es"};
      screen.skin(skin);
      publishRoom(*screen.ui, false, longRoom);
      screen.ui->show("online_room", true);
      screen.frames(30);
      const std::string shown = screen.ui->elementText("online_room", "info-room-text");
      CAPTURE(shown);
      CHECK(shown.ends_with("..."));
      CHECK(shown.size() < longRoom.size());
      checkFits(*screen.ui, "online_room", "info-room");
    }
    {
      Screen screen{"online_rooms", "es"};
      screen.skin(skin);
      publishList(*screen.ui, longRoom);
      screen.ui->show("online_rooms", true);
      screen.frames(30);
      CHECK(screen.ui->elementText("online_rooms", "row-1-name-text").ends_with("..."));
      CHECK_FALSE(screen.ui->elementText("online_rooms", "row-2-name-text").ends_with("..."));
    }
  }
}

TEST_CASE("an empty or unavailable room list says so instead of showing rows") {
  Screen screen{"online_rooms", "es"};
  publishList(*screen.ui, "Room");
  screen.ui->setSetting("online.rooms", 0.0);
  bumpRevision(*screen.ui);
  screen.ui->show("online_rooms", true);
  screen.frames(10);
  CHECK(screen.ui->elementShown("online_rooms", "list-message"));
  CHECK_FALSE(screen.ui->elementShown("online_rooms", "row-1"));
  // Without the network's list: why, and the way in by code.
  screen.ui->setSetting("online.list.state", std::string{"unavailable"});
  screen.ui->setSetting("online.list.error", std::string{"directory_unavailable"});
  bumpRevision(*screen.ui);
  screen.frames(5);
  CHECK(screen.ui->elementShown("online_rooms", "list-message"));
  CHECK_FALSE(screen.ui->elementText("online_rooms", "list-message-text").empty());
}

} // namespace dwe::ui
