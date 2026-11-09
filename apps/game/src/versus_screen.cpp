#include "versus_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/game/save/settings.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/game/versus/network.hpp>
#include <dwe/net/rtc_peer_link.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/slot_names.hpp>
#include <dwe/session/tournament_adapter.hpp>
#include <dwe/session/versus_match.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <optional>
#include <random>
#include <utility>
#include <vector>

#include "arena_view.hpp"
#include "common.hpp"
#include "session_start.hpp"

#ifndef DWE_GAME_VERSION
#define DWE_GAME_VERSION "0.0.0"
#endif

namespace dwe::app {

namespace {

constexpr std::string_view Revision = "versus.revision";
// Off every arena: where the battle keeps the trainer figure nobody plays.
constexpr std::int32_t BystanderDistance = 4000;

// The saves of the saves folder with their registered partners, in the slots' order; a save that
// cannot be read is left out.
std::vector<VersusSave> savesIn(const assets::VirtualFileSystem& files, const Options& options,
                                bool keepsSaves) {
  std::vector<VersusSave> saves;
  const std::unique_ptr<game::save::DiskSaveStorage> storage = saveStorageFor(options, keepsSaves);
  const auto settings = game::save::loadSaveSettings(files);
  if (!storage || !settings) {
    return saves;
  }
  game::save::SaveSlots slots{*storage, *settings};
  slots.setSummaryReader(session::summaryFromSections);
  for (const game::save::SlotInfo& info : slots.list()) {
    if (info.state != game::save::SlotInfo::State::Saved) {
      continue;
    }
    auto file = slots.load(info.slot);
    if (!file) {
      core::logWarning("versus: cannot read a save: {}", file.error().message);
      continue;
    }
    auto partners = session::registeredPartnersIn(*file);
    if (!partners) {
      core::logWarning("versus: cannot read a save's partners: {}", partners.error().message);
      continue;
    }
    saves.push_back({.player = file->header.summary.tamer,
                     .nickname = file->header.summary.nickname,
                     .partners = std::move(*partners)});
  }
  return saves;
}

// The other player's flow of --versus-loopback: its battles are readied as this machine's are, so
// both play the same battle, and shown nowhere.
class ReadiedOnly final : public VersusBattleView {
public:
  explicit ReadiedOnly(VersusArena& arena) : arena_{&arena} {}
  void battleStarted(game::battle::Battle& battle) override { arena_->prepare(battle); }
  void battleFrame(const game::battle::Battle& /*battle*/,
                   const game::battle::BattleEvents& /*events*/) override {}
  void battlesOver() override {}

private:
  VersusArena* arena_;
};

// The arenas a versus series may be on (battle.yaml: versus.arenas, numbers of the tournament's
// arenas: vs_scene.c: VS_loadArenaAssets(VS_BATTLE_SETUP.stage)), by their number; none, logged,
// when the rules give none.
std::vector<session::TournamentAdapter::ArenaStage>
arenasFor(const assets::VirtualFileSystem& files, const ruleset::Ruleset& rules,
          const session::BattleAdapter& battles) {
  std::vector<session::TournamentAdapter::ArenaStage> stages;
  const auto& versus = battles.rules().versus;
  if (!versus || versus->arenas.empty()) {
    return stages;
  }
  session::TournamentAdapterDesc arenasDesc;
  arenasDesc.files = &files;
  session::TournamentAdapter arenas{arenasDesc};
  if (auto read = arenas.readRules(rules); !read) {
    core::logWarning("versus: no arena: {}", read.error().message);
    return stages;
  }
  const std::int32_t last = std::ranges::max(versus->arenas);
  for (std::int32_t index = 0; index <= last; ++index) {
    auto stage = arenas.arenaStageAt(static_cast<std::size_t>(index));
    if (!stage) {
      core::logWarning("versus: arenas from {} on are missing: {}", index, stage.error().message);
      break;
    }
    stages.push_back(std::move(*stage));
  }
  return stages;
}

// The arena of the versus' battles; none, logged, when the renderer or the arenas give none.
std::unique_ptr<VersusArena> arenaFor(const assets::VirtualFileSystem& files,
                                      std::vector<session::TournamentAdapter::ArenaStage> stages,
                                      const session::BattleAdapter& battles,
                                      render::Renderer* renderer, audio::Audio* audio, ui::Ui& ui) {
  if (renderer == nullptr || stages.empty()) {
    return nullptr;
  }
  auto arena = VersusArena::create(VersusArenaDesc{.renderer = renderer,
                                                   .files = &files,
                                                   .species = battles.digimonTable(),
                                                   .audio = audio,
                                                   .arenas = std::move(stages),
                                                   .clock = {},
                                                   .ui = &ui});
  if (!arena) {
    core::logWarning("versus: no arena: {}", arena.error().message);
    return nullptr;
  }
  return std::move(*arena);
}

// vs_intro.c: VS__placePlayer1 and VS__placePlayer2: each battle starts with the fighters on the
// arena's start (the same on both machines: their content is checked alike); no map, the rules'
// arena radius holds them, and nobody stands aside.
std::vector<session::VersusPlacement>
placementsOf(const std::vector<session::TournamentAdapter::ArenaStage>& stages) {
  std::vector<session::VersusPlacement> placements;
  for (const session::TournamentAdapter::ArenaStage& stage : stages) {
    placements.push_back({.map = "",
                          .host = battlePointOf(stage.start.partner),
                          .guest = battlePointOf(stage.start.rival),
                          .bystander = {.x = 0, .z = -BystanderDistance}});
  }
  return placements;
}

// Whether the players wait on the network in this view: the arena is built and loaded then.
bool waitsOnLink(VersusFlow::View view) {
  using View = VersusFlow::View;
  return view == View::Hosting || view == View::Joining || view == View::WaitingLineup;
}

// Whether the players are linked or linking in this view: the battles need the arena from then on.
bool linked(VersusFlow::View view) {
  using View = VersusFlow::View;
  return view != View::Menu && view != View::HostSetup && view != View::JoinEntry &&
         view != View::Result && view != View::Error;
}

} // namespace

VersusScreen::VersusScreen(const assets::VirtualFileSystem& files, const Options& options,
                           bool keepsSaves, ui::Ui& ui, render::Renderer* renderer,
                           audio::Audio* audio)
    : files_{&files}, options_{&options}, keepsSaves_{keepsSaves}, ui_{&ui}, renderer_{renderer},
      audio_{audio} {}

VersusScreen::~VersusScreen() = default;

bool VersusScreen::open() {
  if (!flow_) {
    auto rules = ruleset::loadRuleset(*files_, options_->ruleset);
    if (!rules) {
      core::logError("versus: {}", rules.error().message);
      return false;
    }
    auto battles = std::make_unique<session::BattleAdapter>(
        session::BattleAdapterDesc{files_, {}, {}, nullptr});
    if (auto read = battles->readRules(*rules); !read) {
      core::logError("versus: {}", read.error().message);
      return false;
    }
    auto identity = session::versusIdentity(DWE_GAME_VERSION, *rules, *files_);
    if (!identity) {
      core::logError("versus: {}", identity.error().message);
      return false;
    }
    battles_ = std::move(battles);
    rules_ = std::make_unique<ruleset::Ruleset>(std::move(*rules));
    // The servers: the game pack's, and the command line's signaling server over it. Without a
    // signaling server the links fail at once and the screen says that none is set up.
    net::RtcSettings settings;
    if (auto network = game::versus::loadNetworkConfig(*files_); network) {
      settings.signalServer = std::move(network->signalServer);
      settings.iceServers = std::move(network->iceServers);
    } else {
      core::logWarning("versus: {}", network.error().message);
    }
    if (!options_->versusServer.empty()) {
      settings.signalServer = options_->versusServer;
    }
    const assets::VirtualFileSystem* files = files_;
    const Options* options = options_;
    const bool keepsSaves = keepsSaves_;
    VersusFlowDesc desc{
        .battles = battles_.get(),
        .identity = std::move(*identity),
        .saves = [files, options, keepsSaves] { return savesIn(*files, *options, keepsSaves); },
        .host = [settings] { return net::RtcPeerLink::host(settings, net::openWebSocket); },
        .join =
            [settings](std::string_view code) {
              return net::RtcPeerLink::join(settings, code, net::openWebSocket);
            },
        .seed =
            [] {
              std::random_device device;
              return (static_cast<std::uint64_t>(device()) << 32U) | device();
            },
        .view = nullptr,
        .placement = [this](std::int32_t arena) -> std::optional<session::VersusPlacement> {
          if (arena < 0 || static_cast<std::size_t>(arena) >= placements_.size()) {
            return std::nullopt;
          }
          return placements_.at(static_cast<std::size_t>(arena));
        }};
    if (options_->versusLoopback) {
      rivalDesc_ = desc;
      desc.host = [this] { return loopback_->host(); };
      desc.join = &VersusLoopback::join;
      core::logInfo("versus: --versus-loopback: the other player is this process' own");
    }
    flow_.emplace(std::move(desc));
  }
  flow_->open();
  // A new opening, a new other player (once the flow let go of the last one's link): the last one
  // may have left its series.
  if (rivalDesc_) {
    loopback_ = std::make_unique<VersusLoopback>(*rivalDesc_);
  }
  lastSeconds_.reset();
  active_ = true;
  closed_ = false;
  screens::publish(*ui_, flow_->settings(), Revision);
  ui_->show(VersusDocument, true);
  return true;
}

void VersusScreen::close() {
  active_ = false;
  closed_ = true;
  ui_->hide(VersusDocument);
}

bool VersusScreen::handle(const ui::UiCommand& command) {
  if (!active_) {
    return false;
  }
  if (command.name != VersusActionCommand) {
    return command.name.starts_with("versus_");
  }
  if (const auto* action = std::get_if<std::string>(&command.value)) {
    flow_->act(*action);
  }
  return true;
}

void VersusScreen::type(std::string_view text, bool enter) {
  if (active_) {
    flow_->type(text, enter);
  }
}

void VersusScreen::tick(double nowSeconds) {
  if (!active_) {
    return;
  }
  constexpr double MillisPerSecond = 1000.0;
  readyArena();
  if (loopback_) {
    // A tick's time at most a quarter second: a paused or stalled game (a debugger, a slow load)
    // does not make the two flows of this one process miss each other for longer.
    constexpr double LongestTick = 0.25;
    const double elapsed =
        lastSeconds_ ? std::clamp(nowSeconds - *lastSeconds_, 0.0, LongestTick) : 0.0;
    flow_->tick(loopback_->now());
    loopback_->tick(net::Millis{static_cast<std::int64_t>(elapsed * MillisPerSecond)});
  } else {
    flow_->tick(net::Millis{static_cast<std::int64_t>(nowSeconds * MillisPerSecond)});
  }
  lastSeconds_ = nowSeconds;
  if (arena_) {
    arena_->tick(TickSeconds);
  }
  if (flow_->closed()) {
    close();
    return;
  }
  screens::publish(*ui_, flow_->settings(), Revision);
}

void VersusScreen::readyArena() {
  const VersusFlow::View view = flow_->view();
  if (!arenaBuilt_ && linked(view) && (linking_ || !waitsOnLink(view))) {
    buildArena();
  } else if (arenaBuilt_ && arena_ && preloading_ && linking_ && waitsOnLink(view)) {
    preloading_ = arena_->preloadArena();
  }
  linking_ = waitsOnLink(view);
}

void VersusScreen::buildArena() {
  arenaBuilt_ = true;
  const auto started = std::chrono::steady_clock::now();
  std::vector<session::TournamentAdapter::ArenaStage> stages =
      arenasFor(*files_, *rules_, *battles_);
  placements_ = placementsOf(stages);
  arena_ = arenaFor(*files_, std::move(stages), *battles_, renderer_, audio_, *ui_);
  if (!arena_) {
    return;
  }
  flow_->attachView(arena_.get());
  if (rivalDesc_) {
    rivalView_ = std::make_unique<ReadiedOnly>(*arena_);
    rivalDesc_->view = rivalView_.get();
    if (loopback_) {
      loopback_->attachView(rivalView_.get());
    }
  }
  core::logInfo("versus: the arena is built in {} ms",
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count());
}

bool VersusScreen::takeClosed() {
  return std::exchange(closed_, false);
}

void VersusScreen::pressFinisher() {
  if (active_) {
    flow_->act("finisher");
  }
}

FieldScene* VersusScreen::arenaScene() {
  return active_ && arena_ && arena_->showing() ? &arena_->scene() : nullptr;
}

void VersusScreen::update(double frameSeconds, float alpha, const GraphicsOptions& graphics) {
  if (arenaScene() != nullptr) {
    arena_->update(frameSeconds, alpha, graphics);
  }
}

} // namespace dwe::app
