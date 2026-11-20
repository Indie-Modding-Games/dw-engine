// The NPCs a story change brings to or takes off a game's map, shown at once (#357): a host game
// and a guest game linked by real CoopLinks over a loopback network, each with its own story and
// its map's NPCs, following field/npc_spawns.

#include <dwe/game/coop/link.hpp>
#include <dwe/game/field/npc_spawns.hpp>
#include <dwe/game/field/npcs.hpp>
#include <dwe/game/story/story_state.hpp>
#include <dwe/net/loopback.hpp>
#include <dwe/scene/world.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "coop_gameplay_bridge.hpp"
#include "coop_npc_presence.hpp"
#include "coop_world.hpp"
#include "coop_world_state.hpp"
#include "packages/game/field/tests/fixture.hpp"

using namespace dwe::app;
namespace coop = dwe::game::coop;
namespace field = dwe::game::field;
using dwe::net::Millis;

namespace {

constexpr std::int32_t Recruited = 232;
constexpr std::size_t RecruitSlot = 1;
constexpr std::size_t HelperSlot = 0;
constexpr std::size_t KeeperSlot = 2;
constexpr std::size_t VisitorSlot = 3;
constexpr std::int32_t VisitorMoved = 240;

// The map head of the town (field/npc_spawns): the recruit stands there until trigger 232 is set,
// then a helper takes its place; the keeper always stands there; a visitor comes while trigger 240
// is clear and the section's own count (variable 38) has not reached three, as File City's do.
constexpr std::string_view Spawns = R"(spawns:
- map: town
  slot: 1
  species: 152
  autotalk: false
  guard:
    terms:
    - if:
      - test: trigger
        clear: true
        trigger: 232
      holds: true
  source:
    script: 0
    section: 1
- map: town
  slot: 0
  species: 77
  autotalk: true
  guard:
    terms:
    - if:
      - test: trigger
        trigger: 232
      holds: true
  source:
    script: 0
    section: 1
- map: town
  slot: 2
  species: 9
  autotalk: false
  source:
    script: 0
    section: 1
- map: town
  slot: 3
  species: 40
  autotalk: false
  guard:
    terms:
    - if:
      - test: pstat
        compare: eq
        pstat: 38
        value: 3
      holds: false
    - if:
      - test: trigger
        clear: true
        trigger: 240
      holds: true
  section_writes:
  - 38
  source:
    script: 0
    section: 1
)";

const std::vector<field::NpcSpawnRule>& rules() {
  static const std::vector<field::NpcSpawnRule> read = [] {
    auto parsed = field::parseNpcSpawns(Spawns);
    REQUIRE(parsed);
    return *parsed;
  }();
  return read;
}

bool shared(const coop::WorldEdit& /*edit*/) {
  return true;
}

// One game: its story and the town's NPCs, as the map head put them there on entering.
struct Game {
  dwe::game::story::StoryState story;
  dwe::scene::World world;
  field::FieldNpcs npcs{world, field::testing::originalRules().npc, 1.0F / 30.0F};
  std::uint32_t serial = 1;
  CoopNpcPresence presence;
  // Whether a script of this game runs (the follow waits for it).
  bool scriptRuns = false;

  Game() {
    std::vector<dwe::assets::MapNpc> records(4);
    records[HelperSlot].typeId = 77;
    records[RecruitSlot].typeId = 152;
    records[KeeperSlot].typeId = 9;
    records[VisitorSlot].typeId = 40;
    for (std::size_t slot = 0; slot < records.size(); ++slot) {
      records[slot].scriptId = static_cast<std::int32_t>(slot) + 5;
      records[slot].position = {static_cast<std::int32_t>(slot) * 400, 0, 0};
    }
    npcs.enterMap(records);
    for (const field::NpcSpawnRule& rule : rules()) {
      if (field::evaluate(rule.guard, facts()) == field::Truth::True) {
        REQUIRE(spawn(rule));
      }
    }
  }

  bool spawn(const field::NpcSpawnRule& rule) {
    return npcs.spawn({.slot = rule.slot,
                       .species = rule.species,
                       .autotalk = rule.autotalk,
                       .model = "npc",
                       .radius = 100.0F});
  }

  [[nodiscard]] field::GuardFacts facts() const {
    field::GuardFacts facts{[this](std::int32_t trigger) -> std::optional<bool> {
      const auto set = story.trigger(trigger);
      return set ? std::optional<bool>{*set} : std::nullopt;
    }};
    facts.variables = [this](std::int32_t variable) -> std::optional<std::int64_t> {
      const auto value = story.variable(variable);
      return value ? std::optional<std::int64_t>{*value} : std::nullopt;
    };
    return facts;
  }

  CoopWorldView view() {
    return {.story = &story,
            .items = nullptr,
            .itemTable = nullptr,
            .mapId = "town",
            .mapSerial = serial,
            .settled = true,
            .mapEdits = {},
            .shared = shared,
            .claim = nullptr};
  }

  // After the shared world's tick, as PlaySession::followCoopFacts does.
  std::size_t follow(const CoopWorldShare& share) {
    presence.note(serial, share.facts);
    presence.note(serial, share.ownFacts, CoopNpcPresence::Origin::Own);
    return presence.follow(
        rules(), "town", serial, !scriptRuns, facts(),
        {.npcs = &npcs, .spawn = [this](const field::NpcSpawnRule& rule) { return spawn(rule); }});
  }

  [[nodiscard]] bool standing(std::size_t slot) const { return npcs.entityAt(slot).has_value(); }
};

coop::Identity identity() {
  return {.protocol = coop::ProtocolVersion, .gameVersion = "test", .contentHash = "content"};
}

// A host and a guest whose games talk through real links.
struct Linked {
  dwe::net::LoopbackNetwork network{{}};
  coop::CoopLink hostLink = coop::CoopLink::host(network.link(0), identity());
  coop::CoopLink guestLink = coop::CoopLink::guest(network.link(1), identity(), "Guest");
  NetworkFieldController hostController;
  NetworkFieldController guestController;
  CoopGameplayBridge hostBridge{hostLink, hostController};
  CoopGameplayBridge guestBridge{guestLink, guestController};
  CoopHostWorld host{"Host", "agumon"};
  std::optional<CoopGuestWorld> guest;
  Game hostGame;
  Game guestGame;
  CoopWorldShare lastHost;
  CoopWorldShare lastGuest;

  Linked() {
    for (int i = 0; i < 8; ++i) {
      pump();
    }
    REQUIRE(hostLink.phase() == coop::LinkPhase::Ready);
    REQUIRE(guestLink.phase() == coop::LinkPhase::Ready);
    const auto id = host.join("Guest", "gabumon", Millis{0});
    REQUIRE(id);
    guest.emplace(*id);
    tick();
    tick();
  }

  // Long enough for what one side sent to reach the other.
  void pump() {
    for (int i = 0; i < 3; ++i) {
      network.advance(Millis{1});
      hostBridge.poll(network.now());
      guestBridge.poll(network.now());
    }
  }

  // One tick of both games: each side's messages cross the links, then each game follows.
  void tick() {
    for (const coop::Message& message : hostBridge.takeWorldMessages()) {
      host.receive(guest->self(), message, network.now());
    }
    auto hostView = hostGame.view();
    lastHost = host.share(hostView);
    for (const CoopOutgoing& out : lastHost.out) {
      REQUIRE(hostBridge.send(out.message));
    }
    pump();
    for (const coop::Message& message : guestBridge.takeWorldMessages()) {
      guest->receive(message);
    }
    auto guestView = guestGame.view();
    lastGuest = guest->share(guestView);
    for (const CoopOutgoing& out : lastGuest.out) {
      REQUIRE(guestBridge.send(out.message));
    }
    pump();
    (void)hostGame.follow(lastHost);
    (void)guestGame.follow(lastGuest);
  }
};

} // namespace

TEST_CASE("the host's recruit leaves the guest's map at once and the map head's helper arrives") {
  Linked linked;
  REQUIRE(linked.guestGame.standing(RecruitSlot));
  REQUIRE_FALSE(linked.guestGame.standing(HelperSlot));

  // The host's recruit scene sets the trigger and takes its own NPC off (unload_entity).
  REQUIRE(linked.hostGame.story.setTrigger(Recruited, true));
  REQUIRE(linked.hostGame.npcs.unloadAt(RecruitSlot));
  linked.hostGame.scriptRuns = true;
  // A script of the guest's game runs: its NPCs wait for it.
  linked.guestGame.scriptRuns = true;
  linked.tick();
  REQUIRE(linked.lastHost.ownFacts.size() == 1);
  CHECK(linked.lastHost.ownFacts[0].previous == 0);
  CHECK_FALSE(linked.hostGame.standing(HelperSlot));
  CHECK(linked.guestGame.story.trigger(Recruited).value_or(false));
  REQUIRE(linked.lastGuest.facts.size() == 1);
  CHECK(linked.lastGuest.facts[0].previous == 0);
  CHECK(linked.guestGame.standing(RecruitSlot));

  linked.guestGame.scriptRuns = false;
  linked.tick();
  CHECK_FALSE(linked.guestGame.standing(RecruitSlot));
  CHECK(linked.guestGame.standing(HelperSlot));
  CHECK(linked.guestGame.standing(KeeperSlot));
  // The host's own scene is still on: its helper waits for it, and then comes as the guest's did.
  CHECK_FALSE(linked.hostGame.standing(HelperSlot));
  CHECK(linked.lastHost.facts.empty());
  linked.hostGame.scriptRuns = false;
  linked.tick();
  CHECK_FALSE(linked.hostGame.standing(RecruitSlot));
  CHECK(linked.hostGame.standing(HelperSlot));

  // Once is enough: later ticks change nothing.
  linked.tick();
  CHECK(linked.guestGame.standing(HelperSlot));
  CHECK_FALSE(linked.guestGame.presence.pending());
}

TEST_CASE("the guest's recruit, asked of the host, leaves the host's map at once") {
  Linked linked;
  REQUIRE(linked.guestGame.story.setTrigger(Recruited, true));
  REQUIRE(linked.guestGame.npcs.unloadAt(RecruitSlot));
  linked.tick(); // the guest's ask crosses its link
  linked.tick(); // the host takes it
  CHECK(linked.hostGame.story.trigger(Recruited).value_or(false));
  CHECK_FALSE(linked.hostGame.standing(RecruitSlot));
  CHECK(linked.hostGame.standing(HelperSlot));
  // The guest's own game shows the helper too once its scene is over.
  CHECK(linked.guestGame.standing(HelperSlot));
}

TEST_CASE("a game's own story change brings newcomers but leaves what its scene left standing") {
  Linked linked;
  // A scene of the host sets the trigger without taking the recruit off (the original keeps it
  // there until the map loads again).
  REQUIRE(linked.hostGame.story.setTrigger(Recruited, true));
  linked.tick();
  CHECK(linked.hostGame.standing(RecruitSlot));
  CHECK(linked.hostGame.standing(HelperSlot));
  // The guest's game follows the host's change both ways.
  linked.tick();
  CHECK_FALSE(linked.guestGame.standing(RecruitSlot));
  CHECK(linked.guestGame.standing(HelperSlot));
}

TEST_CASE("a visitor the map head's count leaves undecided goes when another player moves it") {
  Linked linked;
  REQUIRE(linked.hostGame.standing(VisitorSlot));
  REQUIRE(linked.guestGame.standing(VisitorSlot));
  // The guest's story moves the visitor elsewhere (its shop opened): no count lets it come again.
  REQUIRE(linked.guestGame.story.setTrigger(VisitorMoved, true));
  linked.tick();
  linked.tick();
  CHECK(linked.hostGame.story.trigger(VisitorMoved).value_or(false));
  CHECK_FALSE(linked.hostGame.standing(VisitorSlot));
  // The guest's own game keeps it until its next load, as the original does.
  CHECK(linked.guestGame.standing(VisitorSlot));
}

TEST_CASE("a story change taken just before a map load is left to that map's head") {
  Game game;
  const CoopFactChange recruited{
      .edit = {.kind = coop::WorldEditKind::Trigger, .key = Recruited, .value = 1}, .previous = 0};
  REQUIRE(game.story.setTrigger(Recruited, true));
  game.presence.note(game.serial, std::span{&recruited, 1});
  ++game.serial; // the map loads again: its head decided with the trigger set
  CHECK(game.follow({}) == 0);
  CHECK_FALSE(game.presence.pending());
  CHECK(game.standing(RecruitSlot));

  // A change undone before the game could follow it flips nothing.
  const CoopFactChange undone{
      .edit = {.kind = coop::WorldEditKind::Trigger, .key = Recruited, .value = 0}, .previous = 1};
  game.presence.note(game.serial, std::span{&recruited, 1});
  game.presence.note(game.serial, std::span{&undone, 1});
  REQUIRE(game.story.setTrigger(Recruited, false));
  CHECK(game.follow({}) == 0);
  CHECK(game.standing(RecruitSlot));
}
