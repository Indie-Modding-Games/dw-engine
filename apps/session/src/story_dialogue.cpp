#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/story_dialogue.hpp>
#include <dwe/session/story_lua.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "dialogue_messages.hpp"

namespace dwe::session {

namespace {

namespace msg = dialogue_messages;
namespace api = story_dialogue_api;
using game::story::Dialogue;
using game::story::DialogueRules;
using game::story::DialogueSound;
using script::ApiCall;
using script::ApiValue;

constexpr std::string_view OpenFramesKey = "open_frames";
constexpr std::string_view NoCancelTriggerKey = "no_cancel_trigger";
constexpr std::string_view SpeechBoxKey = "speech_box";
constexpr std::string_view ColumnsKey = "columns";
constexpr std::string_view RowsKey = "rows";
constexpr std::string_view SoundsKey = "sounds";
constexpr std::string_view OpenKey = "open";
constexpr std::string_view AdvanceKey = "advance";
constexpr std::string_view CancelKey = "cancel";
constexpr std::string_view CursorKey = "cursor";
constexpr std::string_view RefusedKey = "refused";
constexpr std::string_view SlotKey = "slot";
constexpr std::string_view SoundKey = "sound";
constexpr std::string_view NameColorsKey = "name_colors";
constexpr std::string_view PlayerKey = "player";
constexpr std::string_view PartnerKey = "partner";
constexpr std::string_view CharacterKey = "character";
constexpr std::string_view SpecialKey = "special";

std::int64_t atLeast(assets::YamlReader& reader, const core::YamlNode& mapping,
                     std::string_view key, std::int64_t least) {
  const core::YamlNode& node = reader.field(mapping, key);
  const auto value = reader.integer<std::int64_t>(node);
  if (reader.ok() && value < least) {
    reader.fail(node, messages::formatMessage(msg::RuleTooLow, key, least));
  }
  return value;
}

DialogueSound readSound(assets::YamlReader& reader, const core::YamlNode& sounds,
                        std::string_view key) {
  const core::YamlNode& node = reader.field(sounds, key);
  if (!reader.expectKeys(node, key, {SlotKey, SoundKey})) {
    return {};
  }
  return {.slot = atLeast(reader, node, SlotKey, 0), .sound = atLeast(reader, node, SoundKey, 0)};
}

// The integer operands of a call, in order.
template <std::size_t Count>
core::Result<std::array<std::int64_t, Count>> integers(const ApiCall& call) {
  std::array<std::int64_t, Count> values{};
  for (std::size_t index = 0; index < Count; ++index) {
    const auto value = call.integer(index);
    if (!value) {
      return std::unexpected{value.error()};
    }
    values.at(index) = *value;
  }
  return values;
}

core::Result<ApiValue> nothingOr(const core::Result<void>& done) {
  if (!done) {
    return std::unexpected{done.error()};
  }
  return ApiValue{};
}

} // namespace

core::Result<DialogueRules> readDialogueRules(const ruleset::Section& section) {
  assets::YamlReader reader;
  const core::YamlNode& root = section.node;
  if (!reader.expectKeys(
          root, msg::SectionText,
          {OpenFramesKey, NoCancelTriggerKey, SpeechBoxKey, SoundsKey, NameColorsKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  DialogueRules rules;
  rules.openFrames = atLeast(reader, root, OpenFramesKey, 0);
  rules.noCancelTrigger = atLeast(reader, root, NoCancelTriggerKey, 0);
  const core::YamlNode& box = reader.field(root, SpeechBoxKey);
  if (reader.expectKeys(box, SpeechBoxKey, {ColumnsKey, RowsKey})) {
    rules.speechColumns = atLeast(reader, box, ColumnsKey, 1);
    rules.speechRows = atLeast(reader, box, RowsKey, 1);
  }
  const core::YamlNode& sounds = reader.field(root, SoundsKey);
  if (reader.expectKeys(sounds, SoundsKey,
                        {OpenKey, AdvanceKey, CancelKey, CursorKey, RefusedKey})) {
    rules.openSound = readSound(reader, sounds, OpenKey);
    rules.advanceSound = readSound(reader, sounds, AdvanceKey);
    rules.cancelSound = readSound(reader, sounds, CancelKey);
    rules.cursorSound = readSound(reader, sounds, CursorKey);
    rules.refusedSound = readSound(reader, sounds, RefusedKey);
  }
  const core::YamlNode& colors = reader.field(root, NameColorsKey);
  if (reader.expectKeys(colors, NameColorsKey, {PlayerKey, PartnerKey, CharacterKey, SpecialKey})) {
    rules.playerNameColor = atLeast(reader, colors, PlayerKey, 0);
    rules.partnerNameColor = atLeast(reader, colors, PartnerKey, 0);
    rules.characterNameColor = atLeast(reader, colors, CharacterKey, 0);
    rules.specialNameColor = atLeast(reader, colors, SpecialKey, 0);
  }
  return reader.finish(rules, section.file);
}

StoryDialogue::StoryDialogue(game::story::StoryStatePort& state, DialogueRules rules)
    : dialogue_{state, *this, rules} {}

StoryDialogue::~StoryDialogue() = default;

void StoryDialogue::attach(game::story::StoryRunner& runner) {
  runner_ = &runner;
  runner.addFrameListener(dialogue_);
}

void StoryDialogue::freeze(std::int64_t speaker) {
  core::logDebug("{}", messages::formatMessage(msg::PortFreeze, speaker, runner_ != nullptr));
  // Stopping the speaker's own moves (setMovementEnabled) belongs to the characters on the map.
  if (runner_ != nullptr) {
    runner_->freezeWorld();
  }
}

bool StoryDialogue::onMap(std::int64_t speaker) const {
  // The player and the partner are always on the map; a character the scripts talk through stands
  // on it, since its section runs from it.
  return speaker != game::story::NoSpeaker;
}

core::Result<void> StoryLuaHost::publishDialogue(Dialogue& dialogue) {
  script::ApiRegistry registry = vm_->api();
  std::vector<core::Result<void>> published;
  // The emitter writes the player and the partner by name (lua_emitter.hpp); another order family
  // may have published them already.
  for (const auto& [name, value] : {std::pair{api::Player, game::story::PlayerSpeaker},
                                    std::pair{api::Partner, game::story::PartnerSpeaker}}) {
    if (!registry.isPublished(StoryNamespace, name)) {
      published.push_back(registry.publishValue(StoryNamespace, name, ApiValue{value}));
    }
  }
  published.push_back(
      registry.publishWaiting(StoryNamespace, api::ShowTextbox,
                              [this, &dialogue](const ApiCall& call) -> core::Result<ApiValue> {
                                const auto text = call.text(0);
                                if (!text) {
                                  return std::unexpected{text.error()};
                                }
                                pendingWait_ = dialogue.showText(std::string{*text});
                                return ApiValue{};
                              }));
  published.push_back(
      registry.publishWaiting(StoryNamespace, api::SetSelection,
                              [this, &dialogue](const ApiCall& call) -> core::Result<ApiValue> {
                                const auto text = call.text(0);
                                if (!text) {
                                  return std::unexpected{text.error()};
                                }
                                const auto count = call.integer(1);
                                if (!count) {
                                  return std::unexpected{count.error()};
                                }
                                auto wait = dialogue.showSelection(std::string{*text}, *count);
                                if (!wait) {
                                  return std::unexpected{wait.error()};
                                }
                                pendingWait_ = *wait;
                                return ApiValue{};
                              }));
  published.push_back(registry.publish(StoryNamespace, api::SetDialogOwner,
                                       [&dialogue](const ApiCall& call) -> core::Result<ApiValue> {
                                         const auto owner = call.integer(0);
                                         if (!owner) {
                                           return std::unexpected{owner.error()};
                                         }
                                         dialogue.setOwner(*owner);
                                         return ApiValue{};
                                       }));
  published.push_back(registry.publish(StoryNamespace, api::SetTextboxMode,
                                       [&dialogue](const ApiCall& call) -> core::Result<ApiValue> {
                                         const auto operands = integers<2>(call);
                                         if (!operands) {
                                           return std::unexpected{operands.error()};
                                         }
                                         return nothingOr(
                                             dialogue.setMode((*operands)[0], (*operands)[1]));
                                       }));
  published.push_back(registry.publish(StoryNamespace, api::SetTextboxSize,
                                       [&dialogue](const ApiCall& call) -> core::Result<ApiValue> {
                                         const auto operands = integers<3>(call);
                                         if (!operands) {
                                           return std::unexpected{operands.error()};
                                         }
                                         return nothingOr(dialogue.setSizedBox(
                                             (*operands)[0], (*operands)[1], (*operands)[2]));
                                       }));
  published.push_back(registry.publish(
      StoryNamespace, api::SetTextboxOrigin,
      [&dialogue](const ApiCall& call) -> core::Result<ApiValue> {
        const auto operands = integers<3>(call);
        if (!operands) {
          return std::unexpected{operands.error()};
        }
        dialogue.setOrigin({.x = (*operands)[0], .y = (*operands)[1], .z = (*operands)[2]});
        return ApiValue{};
      }));
  published.push_back(
      registry.publishWaiting(StoryNamespace, api::FadeoutHud,
                              [this, &dialogue](const ApiCall& call) -> core::Result<ApiValue> {
                                const auto box = call.integer(0);
                                if (!box) {
                                  return std::unexpected{box.error()};
                                }
                                pendingWait_ = dialogue.closeBox(*box);
                                return ApiValue{};
                              }));
  const auto failed = std::ranges::find_if(published, [](const auto& result) { return !result; });
  if (failed != published.end()) {
    return core::fail(messages::formatMessage(msg::PublishFailed, failed->error().message));
  }
  return {};
}

} // namespace dwe::session
