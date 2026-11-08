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
#include <random>
#include <utility>
#include <vector>

#include "common.hpp"
#include "session_start.hpp"

#ifndef DWE_GAME_VERSION
#define DWE_GAME_VERSION "0.0.0"
#endif

namespace dwe::app {

namespace {

constexpr std::string_view Revision = "versus.revision";

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

// vs_scene.c: VS_loadArenaAssets(VS_BATTLE_SETUP.stage): the versus' setup always leaves stage 0,
// the tournaments' first arena (measured: VS_BATTLE_SETUP.stage = 0 in a versus battle).
constexpr std::size_t VersusArenaIndex = 0;

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

// The arena of the versus' battles; none, logged, when the rules or the renderer give none.
std::unique_ptr<VersusArena> arenaFor(const assets::VirtualFileSystem& files,
                                      const ruleset::Ruleset& rules,
                                      const session::BattleAdapter& battles,
                                      render::Renderer* renderer, audio::Audio* audio) {
  if (renderer == nullptr) {
    return nullptr;
  }
  session::TournamentAdapterDesc arenasDesc;
  arenasDesc.files = &files;
  session::TournamentAdapter arenas{arenasDesc};
  if (auto read = arenas.readRules(rules); !read) {
    core::logWarning("versus: no arena: {}", read.error().message);
    return nullptr;
  }
  auto stage = arenas.arenaStageAt(VersusArenaIndex);
  if (!stage) {
    core::logWarning("versus: no arena: {}", stage.error().message);
    return nullptr;
  }
  auto arena = VersusArena::create(VersusArenaDesc{.renderer = renderer,
                                                   .files = &files,
                                                   .species = battles.digimonTable(),
                                                   .audio = audio,
                                                   .arena = std::move(*stage),
                                                   .clock = {}});
  if (!arena) {
    core::logWarning("versus: no arena: {}", arena.error().message);
    return nullptr;
  }
  return std::move(*arena);
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
    arena_ = arenaFor(*files_, *rules, *battles_, renderer_, audio_);
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
        .view = arena_.get()};
    if (options_->versusLoopback) {
      rivalDesc_ = desc;
      if (arena_) {
        rivalView_ = std::make_unique<ReadiedOnly>(*arena_);
      }
      rivalDesc_->view = rivalView_.get();
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
  if (loopback_) {
    const double elapsed = lastSeconds_ ? std::max(nowSeconds - *lastSeconds_, 0.0) : 0.0;
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

bool VersusScreen::takeClosed() {
  return std::exchange(closed_, false);
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
