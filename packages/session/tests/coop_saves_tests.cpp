// Co-op and the single-player saves (docs/architecture/multiplayer.md, "Saves in co-op"): the host
// keeps saving its own game, and nothing of the guest (its body, its partner) ever reaches that
// save, so the save loads in a single-player game as if the guest had never been there.

#include <dwe/assets/map_document.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/scene/world.hpp>
#include <dwe/session/game_session.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "fixture.hpp"
#include "session_fixture.hpp"

using dwe::game::field::Field;
using dwe::game::field::GuestPlayerId;
using dwe::game::field::testing::originalRules;
using dwe::game::field::testing::roomMap;
using dwe::game::save::MemorySaveStorage;
using dwe::game::save::SlotId;
using dwe::game::save::SlotKind;
using dwe::session::GameSession;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGame;
using dwe::session::testing::TestPacks;
using dwe::session::testing::Tick;

namespace {

constexpr SlotId HostSlot{.kind = SlotKind::Manual, .index = 2};
constexpr std::int32_t Ticks = 90;
constexpr std::string_view GuestPartnerModel = "partners/guest_partner/model";

// A host's game on the field: its session, saving to its own storage, and the field it plays.
class HostGame {
public:
  explicit HostGame(const TestPacks& packs)
      : session_(newGame(packs, &storage_)), field_(world_, originalRules()) {
    field_.enterMapAt(roomMap("town"), {.position = {0.0F, 0.0F, 0.0F}, .yaw = 0.0F});
    session_->attachField(field_);
  }
  HostGame(const HostGame&) = delete;
  HostGame& operator=(const HostGame&) = delete;
  HostGame(HostGame&&) = delete;
  HostGame& operator=(HostGame&&) = delete;
  ~HostGame() { session_->detachField(); }

  // The guest joins: its body beside the host and its partner behind it, as the app adds them
  // (FieldScene::addCoopGuest).
  void addGuest() {
    REQUIRE(field_.addPlayer(GuestPlayerId, {.position = {600.0F, 0.0F, 0.0F}, .yaw = 0.0F}));
    REQUIRE(field_.addPartner(GuestPlayerId, std::string{GuestPartnerModel}, 120.0F));
  }

  // The game loop's ticks; the guest, if any, runs about meanwhile.
  void run() {
    for (std::int32_t step = 0; step < Ticks; ++step) {
      (void)field_.setInput(GuestPlayerId, {.stick = {1.0F, 0.0F}, .run = true});
      field_.tick(static_cast<float>(Tick));
      session_->tick(Tick);
    }
  }

  // The host's save, as the slot holds it.
  std::string saved() {
    auto written = session_->save(HostSlot);
    REQUIRE_MESSAGE(written.has_value(), errorOf(written));
    auto text = storage_.read(dwe::game::save::slotFileName(HostSlot));
    REQUIRE(text.has_value());
    REQUIRE(text->has_value());
    return **text;
  }

  [[nodiscard]] Field& field() { return field_; }
  [[nodiscard]] MemorySaveStorage& storage() { return storage_; }

private:
  MemorySaveStorage storage_;
  std::unique_ptr<GameSession> session_;
  dwe::scene::World world_;
  Field field_;
};

} // namespace

TEST_CASE("a host's save made with a co-op guest in the field is its single-player save") {
  const TestPacks packs;
  HostGame alone{packs};
  HostGame hosting{packs};
  hosting.addGuest();

  alone.run();
  hosting.run();
  // The guest did run about, so the save had something to leave out.
  REQUIRE(hosting.field().player(GuestPlayerId).has_value());
  CHECK(hosting.field().player(GuestPlayerId)->position.x > 600.0F);

  const std::string single = alone.saved();
  const std::string coop = hosting.saved();
  CHECK(coop == single);
  CHECK(coop.find(GuestPartnerModel) == std::string::npos);
}

TEST_CASE("a save the host wrote in co-op loads in a single-player game at the host's place") {
  const TestPacks packs;
  HostGame hosting{packs};
  hosting.addGuest();
  hosting.run();
  const dwe::game::field::CharacterState host = hosting.field().player();
  (void)hosting.saved();

  // The same save, played alone afterwards: no field of the guest's, the host where it stood.
  auto later = newGame(packs, &hosting.storage());
  auto loaded = later->load(HostSlot);
  REQUIRE_MESSAGE(loaded.has_value(), errorOf(loaded));
  REQUIRE(later->savedPlace().has_value());
  CHECK(later->savedPlace()->map == "town");
  CHECK(later->savedPlace()->spawn.position == host.position);
  CHECK(later->homeSlot() == std::optional<SlotId>{HostSlot});
}

TEST_CASE("a note a co-op guest's return left in its save is taken once and gone from the next") {
  const TestPacks packs;
  MemorySaveStorage storage;
  auto game = newGame(packs, &storage);
  REQUIRE(game->save(HostSlot));
  auto file = game->saveFile();
  REQUIRE(file.has_value());
  file->sections.insert_or_assign(
      "coop_return", dwe::game::save::SavedSection{
                         .version = 1, .state = dwe::core::YamlNode::scalar("from a friend")});

  auto later = newGame(packs, &storage);
  auto loaded = later->loadFile(*file);
  REQUIRE_MESSAGE(loaded.has_value(), errorOf(loaded));
  // A section a system reads is never taken.
  CHECK_FALSE(later->takeUnreadSection("partner").has_value());
  const auto note = later->takeUnreadSection("coop_return");
  REQUIRE(note.has_value());
  CHECK(note->state == dwe::core::YamlNode::scalar("from a friend"));
  CHECK_FALSE(later->takeUnreadSection("coop_return").has_value());
  auto next = later->saveFile();
  REQUIRE(next.has_value());
  CHECK_FALSE(next->sections.contains("coop_return"));
  CHECK(next->sections.contains("partner"));
}
