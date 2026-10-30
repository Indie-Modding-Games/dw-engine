// PlaySession's side of a bug report (bug_report.hpp): what the game is doing now, and a save of
// it.

#include <dwe/assets/map_document.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/field/npcs.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/story/story_runner.hpp>
#include <dwe/scene/components.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "bug_report.hpp"
#include "common.hpp"
#include "partner_menu.hpp"
#include "play_session.hpp"

namespace dwe::app {
namespace {

// How often a free field's snapshot is taken again, in real seconds.
constexpr double SnapshotSeconds = 5.0;

std::string valueText(const ui::UiValue& value) {
  if (const auto* flag = std::get_if<bool>(&value)) {
    return *flag ? "true" : "false";
  }
  if (const auto* number = std::get_if<double>(&value)) {
    return std::format("{}", *number);
  }
  if (const auto* text = std::get_if<std::string>(&value)) {
    return *text;
  }
  return {};
}

ReportPose poseOf(const game::field::CharacterState& state) {
  return {.position = state.position, .yaw = state.yaw};
}

} // namespace

std::string_view PlaySession::busyWith() const {
  const game::field::Field& field = scene_->field();
  if (battleScreen_.active()) {
    return "battle";
  }
  if (evolutionScene_.active()) {
    return "evolution";
  }
  if (deathScene_.active()) {
    return "death";
  }
  if (sleepScreen_.active()) {
    return "sleep";
  }
  if (gymScreen_.active()) {
    return "gym";
  }
  if (fishingScreen_.active()) {
    return "fishing";
  }
  if (shopScreen_.active()) {
    return "shop";
  }
  if (saveMachine_.active()) {
    return "save_machine";
  }
  if (feedScene_.active() || fieldToilet_.active() || fieldPoop_.active() || field.heldForCare()) {
    return "care_scene";
  }
  if (fieldItemsScreen_.active()) {
    return "field_item";
  }
  if (field.mapChangePending() || field.fadeLevel() > 0.0F) {
    return "map_change";
  }
  const session::StoryAdapter* story = game_.systems.story;
  if (field.heldByScript() || (story != nullptr && story->scriptRunning())) {
    return "script";
  }
  return {};
}

void PlaySession::keepFieldSnapshot(double frameSeconds) {
  snapshotSeconds_ += frameSeconds;
  if (fieldSnapshot_ && snapshotSeconds_ < SnapshotSeconds) {
    return;
  }
  if (!busyWith().empty()) {
    return;
  }
  if (auto file = game_.session->saveFile()) {
    fieldSnapshot_ = std::move(*file);
    snapshotSeconds_ = 0.0;
  }
}

BugReportState PlaySession::reportState() const {
  const session::GameSession& session = *game_.session;
  const game::field::Field& field = scene_->field();
  const ui::Ui& ui = *context_.ui;
  BugReportState state;
  state.map = field.mapId();
  const ruleset::GameTime time = session.clock().time();
  state.time = {.year = time.year, .day = time.day, .hour = time.hour, .minute = time.minute};
  const std::string_view busy = busyWith();
  state.screen = busy.empty() || busy == "script" || busy == "map_change" ? "field" : busy;
  if (battleScreen_.active()) {
    state.battlePhase = valueText(ui.setting(BattlePhaseSetting));
  }
  session::StoryAdapter* story = game_.systems.story;
  state.scriptRunning = story != nullptr && story->scriptRunning();
  if (story != nullptr && story->runner() != nullptr) {
    for (const game::story::SectionRef& section : story->runner()->runningSections()) {
      state.scriptSections.push_back(
          std::format("script {} section {}", section.script, section.section));
    }
  }
  state.heldByScript = field.heldByScript();
  state.heldForCare = field.heldForCare();
  state.dialogOpen = storyTextbox_.has_value() && story != nullptr && story->dialogue().view().open;
  state.menuOpen = ui.isVisible(MenuDocument) || ui.isVisible(PartnerMenuDocument);
  state.mapChanging = field.mapChangePending();
  state.player = poseOf(field.player());
  state.partner = poseOf(field.partner());

  const auto& registry = scene_->world().registry();
  const game::field::FieldNpcs& npcs = field.npcs();
  for (const auto [entity, tag, transform] :
       registry.view<const game::field::NpcTag, const scene::Transform>().each()) {
    ReportNpc npc;
    npc.scriptId = tag.scriptId;
    npc.pose = {.position = transform.position, .yaw = transform.yaw};
    npc.held = npcs.heldOf(tag.scriptId).value_or(false);
    npc.scriptedAnimation = npcs.scriptedAnimationOf(tag.scriptId).value_or(0);
    npc.scripted = registry.all_of<game::field::ScriptedActor>(entity);
    if (const auto instance = npcs.instanceOf(tag.scriptId);
        instance && *instance >= game::field::FirstNpcInstance) {
      npc.slot = *instance - game::field::FirstNpcInstance;
      if (const assets::MapNpc* record = npcs.record(npc.slot)) {
        npc.species = std::to_string(record->typeId);
      }
    }
    if (const auto* model = registry.try_get<scene::ModelRef>(entity)) {
      npc.model = model->asset;
    }
    state.npcs.push_back(std::move(npc));
  }

  if (const game::battle::Battle* battle = battleScreen_.battle()) {
    const auto& fighters = battle->state().fighters;
    const auto enemyNpcs = battleScreen_.enemyNpcs();
    for (std::size_t index = 0; index < fighters.size(); ++index) {
      const game::battle::Fighter& fighter = fighters[index];
      ReportFighter reported;
      reported.species = fighter.combatant.species;
      reported.hp = fighter.hp;
      reported.maxHp = fighter.stats.maxHp;
      reported.mp = fighter.mp;
      reported.maxMp = fighter.stats.maxMp;
      reported.x = fighter.position.x;
      reported.z = fighter.position.z;
      if (index > 0 && index - 1 < enemyNpcs.size()) {
        reported.npc = enemyNpcs[index - 1];
      }
      state.fighters.push_back(std::move(reported));
    }
  }

  state.partnerSpecies = session.partner().state().species;
  for (const screens::ScreenSetting& setting : partnerStatus()) {
    state.partnerStatus.push_back({.name = setting.name, .value = valueText(setting.value)});
  }
  state.overlaps = field.overlapEpisodes();
  return state;
}

std::optional<std::string> PlaySession::reportSave(ReportSave& save) const {
  const std::string_view busy = busyWith();
  std::optional<game::save::SaveFile> file;
  if (busy.empty()) {
    if (auto now = game_.session->saveFile()) {
      file = std::move(*now);
      save = {.kind = ReportSaveKind::Now, .secondsAgo = 0.0, .reason = {}};
    } else {
      save.reason = now.error().message;
    }
  } else {
    save.reason = std::format("cannot save during: {}", busy);
  }
  if (!file && fieldSnapshot_) {
    file = *fieldSnapshot_;
    save.kind = ReportSaveKind::FieldSnapshot;
    save.secondsAgo = snapshotSeconds_;
  }
  if (!file) {
    save.kind = ReportSaveKind::None;
    if (save.reason.empty()) {
      save.reason = "no save of this game yet";
    }
    return std::nullopt;
  }
  auto text = game::save::writeSaveFile(*file);
  if (!text) {
    save = {.kind = ReportSaveKind::None, .secondsAgo = 0.0, .reason = text.error().message};
    return std::nullopt;
  }
  return std::move(*text);
}

} // namespace dwe::app
