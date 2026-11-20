
#include <dwe/game/partner/state_yaml.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "coop_guest_profile.hpp"
#include "coop_guest_return.hpp"

using namespace dwe;
using game::save::SavedSection;
using game::save::SaveFile;

namespace {

constexpr game::save::SlotId GuestSlot{.kind = game::save::SlotKind::Manual, .index = 3};
const game::save::SaveSettings Settings{};
constexpr std::chrono::sys_seconds Started{std::chrono::seconds{1'700'000'000}};
constexpr std::chrono::sys_seconds Later{std::chrono::seconds{1'700'009'000}};

core::YamlNode scalar(std::string_view text) {
  return core::YamlNode::scalar(std::string{text});
}

SavedSection partnerSection(std::string_view species, std::int32_t hp) {
  game::partner::PartnerState partner;
  partner.species = std::string{species};
  partner.stats.base[game::partner::Stat::Hp] = hp;
  partner.stats.base[game::partner::Stat::Mp] = 300;
  partner.stats.base[game::partner::Stat::Offense] = 80;
  partner.stats.currentHp = hp;
  partner.stats.currentMp = 300;
  partner.life.lives = 3;
  partner.life.remainingLifetime = 200;
  return {.version = game::partner::PartnerStateVersion, .state = game::partner::toYaml(partner)};
}

SaveFile guestHomeSave() {
  SaveFile file;
  file.header.created = Started;
  file.header.updated = Started;
  file.header.rulesetId = "original";
  file.header.playSeconds = 3600;
  file.header.summary = {.slot = "slot_03",
                         .tamer = "Mia",
                         .species = "agumon",
                         .nickname = "Sparky",
                         .map = "guest_town",
                         .mode = {}};
  file.header.label = "agumon guest_town";
  file.sections.emplace("partner", partnerSection("agumon", 900));
  file.sections.emplace("moves", SavedSection{.state = scalar("guest_moves")});
  file.sections.emplace("evolution", SavedSection{.state = scalar("raised_agumon")});
  file.sections.emplace("inventory", SavedSection{.version = 2, .state = scalar("bag_3_meat")});
  file.sections.emplace("story", SavedSection{.version = 4, .state = scalar("guest_story")});
  file.sections.emplace("field", SavedSection{.state = scalar("guest_town_spot")});
  file.sections.emplace("clock", SavedSection{.state = scalar("guest_day_5")});
  return file;
}

std::string textOf(const SaveFile& file) {
  auto text = game::save::writeSaveFile(file);
  REQUIRE(text.has_value());
  return *text;
}

SaveFile playedSave(std::string_view species = "greymon",
                    std::string_view bag = "bag_3_meat_1_disk", std::int32_t hp = 1400) {
  SaveFile file;
  file.header.created = Started - std::chrono::hours{240};
  file.header.rulesetId = "original";
  file.header.summary = {.slot = {},
                         .tamer = "Host",
                         .species = std::string{species},
                         .nickname = "Sparky",
                         .map = "host_jungle",
                         .mode = {}};
  file.sections.emplace("partner", partnerSection(species, hp));
  file.sections.emplace("moves", SavedSection{.state = scalar("guest_moves_and_new_one")});
  file.sections.emplace("evolution", SavedSection{.state = scalar("raised_agumon_greymon")});
  file.sections.emplace("inventory", SavedSection{.version = 2, .state = scalar(bag)});
  file.sections.emplace("story", SavedSection{.version = 4, .state = scalar("host_story")});
  file.sections.emplace("field", SavedSection{.state = scalar("host_jungle_spot")});
  file.sections.emplace("clock", SavedSection{.state = scalar("host_day_40")});
  return file;
}

SaveFile slotOn(const game::save::MemorySaveStorage& storage, std::string_view name) {
  auto text = storage.read(name);
  REQUIRE(text.has_value());
  REQUIRE(text->has_value());
  auto file = game::save::parseSaveFile(**text);
  REQUIRE(file.has_value());
  return *file;
}

} // namespace

TEST_CASE("a co-op return takes home the guest's partner, moves, raised species and bag only") {
  const SaveFile home = guestHomeSave();
  const auto returned = app::coopGuestReturn(home, home, playedSave(), "Leo", Later);
  REQUIRE_MESSAGE(returned.has_value(), returned.error().message);

  CHECK(returned->sections.at("partner").state == playedSave().sections.at("partner").state);
  CHECK(returned->sections.at("moves").state == scalar("guest_moves_and_new_one"));
  CHECK(returned->sections.at("evolution").state == scalar("raised_agumon_greymon"));
  CHECK(returned->sections.at("inventory").state == scalar("bag_3_meat_1_disk"));
  CHECK(returned->sections.at("story").state == scalar("guest_story"));
  CHECK(returned->sections.at("field").state == scalar("guest_town_spot"));
  CHECK(returned->sections.at("clock").state == scalar("guest_day_5"));
  CHECK(returned->header.summary.map == "guest_town");
  CHECK(returned->header.summary.tamer == "Mia");
  CHECK(returned->header.summary.slot == "slot_03");
  CHECK(returned->header.created == Started);
  CHECK(returned->header.playSeconds == 3600);
  CHECK(returned->header.summary.species == "greymon");
  CHECK(returned->header.label == "greymon guest_town");
  CHECK(returned->header.updated == Later);
  REQUIRE(returned->sections.contains(std::string{app::CoopReturnSection}));
  const auto note =
      app::coopReturnNoteOf(returned->sections.at(std::string{app::CoopReturnSection}));
  REQUIRE(note.has_value());
  CHECK(*note == app::CoopReturnNote{.host = "Leo", .species = "greymon"});
}

TEST_CASE("a co-op return writes nothing over another playthrough or a partner out of range") {
  const SaveFile brought = guestHomeSave();
  SaveFile other = brought;
  other.header.created = Later;
  CHECK_FALSE(app::coopGuestReturn(other, brought, playedSave(), "Leo", Later));
  other = brought;
  other.header.summary.tamer = "Someone";
  CHECK_FALSE(app::coopGuestReturn(other, brought, playedSave(), "Leo", Later));

  CHECK_FALSE(
      app::coopGuestReturn(brought, brought, playedSave("greymon", "bag", 50'000), "Leo", Later));
  SaveFile noPartner = playedSave();
  noPartner.sections.erase("partner");
  CHECK_FALSE(app::coopGuestReturn(brought, brought, noPartner, "Leo", Later));

  game::partner::PartnerState partner;
  partner.species = "greymon";
  partner.stats.base[game::partner::Stat::Hp] = 1000;
  CHECK(app::checkReturningPartner(partner));
  partner.stats.currentHp = 1001;
  CHECK_FALSE(app::checkReturningPartner(partner));
  partner.stats.currentHp = 10;
  partner.stats.base[game::partner::Stat::Speed] = 1000;
  CHECK_FALSE(app::checkReturningPartner(partner));
  partner.stats.base[game::partner::Stat::Speed] = 999;
  partner.life.lives = -1;
  CHECK_FALSE(app::checkReturningPartner(partner));
  partner.life.lives = 0;
  CHECK(app::checkReturningPartner(partner));
  partner.species.clear();
  CHECK_FALSE(app::checkReturningPartner(partner));
}

TEST_CASE("a co-op guest writes its bag home at once and its partner's growth every while") {
  const SaveFile home = guestHomeSave();
  app::CoopGuestHome guest;
  REQUIRE(guest.begin({.slot = GuestSlot, .backup = false, .text = textOf(home)}, "Leo", 100.0));

  SaveFile played = home;
  CHECK_FALSE(guest.due(played, 105.0));
  CHECK_FALSE(guest.changed(played));
  CHECK(app::coopReturnChange(home, played) == app::CoopReturnChange::None);
  played.sections.insert_or_assign("partner", partnerSection("agumon", 950));
  CHECK(app::coopReturnChange(home, played) == app::CoopReturnChange::Partner);
  CHECK(guest.changed(played));
  CHECK_FALSE(guest.due(played, 110.0));
  CHECK(guest.due(played, 100.0 + app::CoopGuestHome::PartnerEverySeconds));
  played.sections.insert_or_assign("inventory",
                                   SavedSection{.version = 2, .state = scalar("bag_traded")});
  CHECK(app::coopReturnChange(home, played) == app::CoopReturnChange::Bag);
  CHECK(guest.due(played, 101.0));
  SaveFile world = home;
  world.sections.insert_or_assign("story", SavedSection{.version = 4, .state = scalar("x")});
  CHECK(app::coopReturnChange(home, world) == app::CoopReturnChange::None);
}

TEST_CASE("a co-op guest's progress goes home into its slot, keeping the save before it") {
  const SaveFile home = guestHomeSave();
  game::save::MemorySaveStorage storage;
  game::save::SaveSlots slots{storage, Settings};
  REQUIRE(slots.save(GuestSlot, home));
  const std::string slotName = game::save::slotFileName(GuestSlot);

  app::CoopGuestHome guest;
  REQUIRE(guest.begin(
      {.slot = GuestSlot, .backup = false, .text = textOf(slotOn(storage, slotName))}, "Leo", 0.0));
  const auto written = guest.bringHome(storage, Settings, playedSave(), Later, 10.0);
  REQUIRE_MESSAGE(written.has_value(), written.error().message);

  const SaveFile slot = slotOn(storage, slotName);
  CHECK(slot.sections.at("inventory").state == scalar("bag_3_meat_1_disk"));
  CHECK(slot.sections.at("story").state == scalar("guest_story"));
  CHECK(slot.header.summary.species == "greymon");
  CHECK(textOf(slot) == *written);
  const SaveFile before = slotOn(storage, game::save::slotBeforeCoopName(GuestSlot));
  CHECK(before.sections.at("inventory").state == scalar("bag_3_meat"));
  CHECK(slotOn(storage, game::save::slotBackupName(GuestSlot)).header.summary.species == "agumon");
  REQUIRE(guest.home().has_value());
  CHECK(guest.home()->text == *written);
  CHECK_FALSE(guest.changed(playedSave()));

  REQUIRE(guest.bringHome(storage, Settings, playedSave("greymon", "bag_3_meat"), Later, 50.0));
  CHECK(slotOn(storage, slotName).sections.at("inventory").state == scalar("bag_3_meat"));
  CHECK(slotOn(storage, game::save::slotBeforeCoopName(GuestSlot)).sections.at("inventory").state ==
        scalar("bag_3_meat"));
  CHECK(slotOn(storage, game::save::slotBeforeCoopName(GuestSlot)).header.summary.species ==
        "agumon");

  SaveFile stranger = guestHomeSave();
  stranger.header.created = Later;
  stranger.sections.insert_or_assign("story", SavedSection{.version = 4, .state = scalar("new")});
  REQUIRE(slots.save(GuestSlot, stranger));
  CHECK_FALSE(guest.bringHome(storage, Settings, playedSave(), Later, 90.0));
  CHECK(slotOn(storage, slotName).sections.at("story").state == scalar("new"));
  guest.failed(90.0);
  CHECK_FALSE(guest.due(playedSave("greymon", "bag_again"), 100.0));
  CHECK(guest.due(playedSave("greymon", "bag_again"),
                  90.0 + app::CoopGuestHome::PartnerEverySeconds));
}

TEST_CASE("a guest back in the room brings the save it took home, so nothing comes in twice") {
  const SaveFile home = guestHomeSave();
  game::save::MemorySaveStorage storage;
  game::save::SaveSlots slots{storage, Settings};
  REQUIRE(slots.save(GuestSlot, home));
  app::CoopGuestHome first;
  REQUIRE(first.begin({.slot = GuestSlot, .backup = false, .text = textOf(home)}, "Leo", 0.0));
  const auto written = first.bringHome(storage, Settings, playedSave(), Later, 1.0);
  REQUIRE(written.has_value());
  first.end();
  CHECK_FALSE(first.active());

  const auto rejoined = app::hostWorldWithGuestPartner(textOf(playedSave()), *written);
  REQUIRE_MESSAGE(rejoined.has_value(), rejoined.error().message);
  const auto again = game::save::parseSaveFile(*rejoined);
  REQUIRE(again.has_value());
  CHECK(again->sections.at("inventory").state == scalar("bag_3_meat_1_disk"));
  CHECK_FALSE(again->sections.contains(std::string{app::CoopReturnSection}));

  app::CoopGuestHome second;
  REQUIRE(second.begin({.slot = GuestSlot, .backup = false, .text = *written}, "Leo", 0.0));
  CHECK_FALSE(second.changed(*again));
  REQUIRE(second.bringHome(storage, Settings, *again, Later, 2.0));
  CHECK(slotOn(storage, game::save::slotFileName(GuestSlot)).sections.at("inventory").state ==
        scalar("bag_3_meat_1_disk"));
}

TEST_CASE("a guest plays the host's world with its own bag, moves and raised species") {
  SaveFile host = playedSave();
  host.sections.insert_or_assign("inventory",
                                 SavedSection{.version = 2, .state = scalar("host_bag_99_bits")});
  host.sections.insert_or_assign("evolution", SavedSection{.state = scalar("host_raised")});
  host.sections.insert_or_assign(std::string{app::CoopReturnSection},
                                 app::coopReturnSection({.host = "Someone", .species = "x"}));
  const auto merged = app::hostWorldWithGuestPartner(textOf(host), textOf(guestHomeSave()));
  REQUIRE_MESSAGE(merged.has_value(), merged.error().message);
  const auto game = game::save::parseSaveFile(*merged);
  REQUIRE(game.has_value());
  CHECK(game->sections.at("inventory").state == scalar("bag_3_meat"));
  CHECK(game->sections.at("evolution").state == scalar("raised_agumon"));
  CHECK(game->sections.at("moves").state == scalar("guest_moves"));
  CHECK(game->sections.at("story").state == scalar("host_story"));
  CHECK(game->sections.at("clock").state == scalar("host_day_40"));
  CHECK_FALSE(game->sections.contains(std::string{app::CoopReturnSection}));
}

TEST_CASE("a co-op return's note reads back, and a foreign one is not taken for it") {
  const app::CoopReturnNote note{.host = "Leo \"the host\"", .species = "greymon"};
  CHECK(app::coopReturnNoteOf(app::coopReturnSection(note)) == note);
  SavedSection newer = app::coopReturnSection(note);
  newer.version = app::CoopReturnVersion + 1;
  CHECK_FALSE(app::coopReturnNoteOf(newer).has_value());
  CHECK_FALSE(app::coopReturnNoteOf(SavedSection{.state = scalar("x")}).has_value());
}
