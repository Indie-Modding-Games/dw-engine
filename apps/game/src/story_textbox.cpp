#include "story_textbox.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/partner_view.hpp>

#include <array>
#include <charconv>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

using game::story::DialogueView;
using game::story::Speaker;
using game::story::SpeakerKind;

// The placeholders of the script's text that name a table's entry by a story variable
// (script_textbox.c: showTextbox, codes 7, 8, 9, 0x11, 0x12; docs/formats/textos.md).
struct VariableName {
  std::string_view prefix;
  std::string_view block;
};
constexpr std::array VariableNames{
    VariableName{.prefix = "digimon:pstat=", .block = screens::SpeciesNamesBlock},
    VariableName{.prefix = "move:pstat=", .block = screens::MoveNamesBlock},
    VariableName{.prefix = "item:pstat=", .block = screens::ItemNamesBlock},
    VariableName{.prefix = "bgm:pstat=", .block = "bgm_track_names"},
    VariableName{.prefix = "tournament:pstat=", .block = "tournament_names"},
};
constexpr std::string_view NumberPrefix = "num:pstat=";
constexpr std::string_view TamerTag = "tamer";
constexpr std::string_view PartnerTag = "partner";
// The player's name is the species table's first name until the game names the player
// (script_value.c: getSpeakerName, speaker 0xFD: DIGIMON_NAME(0)).
constexpr std::size_t PlayerNameSlot = 0;
// The special speakers' names (script_value.c: SPECIAL_SPEAKERS).
constexpr std::string_view SpecialSpeakersBlock = "special_speakers";

std::optional<std::int32_t> numberAfter(std::string_view tag, std::string_view prefix) {
  if (!tag.starts_with(prefix)) {
    return std::nullopt;
  }
  const std::string_view digits = tag.substr(prefix.size());
  std::int32_t value = 0;
  const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
  if (error != std::errc{} || end != digits.data() + digits.size()) {
    return std::nullopt;
  }
  return value;
}

const assets::MapNpc* characterOf(const FieldScene& scene, std::int64_t script) {
  for (const assets::MapNpc& npc : scene.map().npcs) {
    if (npc.scriptId == script) {
      return &npc;
    }
  }
  return nullptr;
}

} // namespace

StoryTextbox::StoryTextbox(ui::Ui& ui, session::StoryAdapter& story,
                           const session::GameSession& session)
    : ui_{&ui}, story_{&story}, session_{&session} {}

StoryTextbox::~StoryTextbox() {
  ui_->hide(StoryTextboxDocument);
}

void StoryTextbox::press(const input::InputState& input) {
  story_->dialogue().press({.confirm = input.wasPressed("confirm"),
                            .cancel = input.wasPressed("cancel"),
                            .up = input.wasPressed("move_up"),
                            .down = input.wasPressed("move_down")},
                           input.isDown("confirm"));
}

void StoryTextbox::update(const FieldScene& scene, const assets::DigimonTable* species) {
  const DialogueView& view = story_->dialogue().view();
  if (published_ == view.revision) {
    return;
  }
  published_ = view.revision;
  const std::vector<TextRow> rows = rowsOf(view, scene, species);
  const auto& rules = story_->dialogue().rules();
  const double opening = rules.openFrames > 0 ? static_cast<double>(view.openingFrame) /
                                                    static_cast<double>(rules.openFrames)
                                              : 1.0;
  const bool named = view.speaker.kind != SpeakerKind::None;
  const TextboxLook look{
      .open = view.open,
      .speech = view.kind == game::story::BoxKind::Speech,
      .columns = view.columns,
      .rows = view.rows,
      .opening = opening,
      .origin = originOf(view, scene),
      .prompt = view.waitingForConfirm,
      .cursorRow = view.cursor ? std::optional<std::size_t>{static_cast<std::size_t>(*view.cursor) +
                                                            (named ? 1U : 0U)}
                               : std::nullopt,
      .cursorColumns = view.cursor ? std::optional<std::size_t>{longestRow(rows, named ? 1U : 0U)}
                                   : std::nullopt,
      .top = false,
  };
  screens::publish(*ui_, textboxSettings(look, rows), TextboxRevisionSetting);
  if (staleRevision_ && *staleRevision_ != view.revision) {
    staleRevision_.reset();
  }
  const bool shown = view.open && !covered_ && !staleRevision_;
  if (shown != shown_) {
    shown_ = shown;
    if (shown_) {
      ui_->show(StoryTextboxDocument);
    } else {
      ui_->hide(StoryTextboxDocument);
    }
  }
}

void StoryTextbox::cover(bool covered) {
  if (covered == covered_) {
    return;
  }
  covered_ = covered;
  if (covered) {
    staleRevision_.reset();
  } else {
    staleRevision_ = story_->dialogue().view().revision;
  }
  // The next update shows or hides the box again.
  published_.reset();
}

std::vector<TextRow> StoryTextbox::rowsOf(const DialogueView& view, const FieldScene& scene,
                                          const assets::DigimonTable* species) const {
  std::vector<TextRow> rows;
  if (!view.open || view.text.empty()) {
    return rows;
  }
  // script_textbox.c: showTextbox writes the speaker's name on the first row, in its colour.
  if (view.speaker.kind != SpeakerKind::None) {
    rows.push_back({TextRun{.text = speakerName(view.speaker, scene, species),
                            .color = nameColor(view.speaker)}});
  }
  appendPageRows(rows, pageText(view.text, species));
  return rows;
}

std::string StoryTextbox::speakerName(const Speaker& speaker, const FieldScene& scene,
                                      const assets::DigimonTable* species) const {
  const screens::NameKeys names{species, nullptr, nullptr};
  switch (speaker.kind) {
  case SpeakerKind::None:
    return {};
  case SpeakerKind::Player:
    return tamerName();
  case SpeakerKind::Partner:
    return partnerName(names);
  case SpeakerKind::Special:
    return localization::tr(
        screens::mainLabelKey(SpecialSpeakersBlock, static_cast<std::size_t>(speaker.number)));
  case SpeakerKind::Character:
    break;
  }
  // script_value.c: getSpeakerName names a character by its species.
  const assets::MapNpc* character = characterOf(scene, speaker.number);
  if (character == nullptr) {
    core::logDebug("[port:dialogue] getSpeakerName (script_value.c:60): character {} is not on "
                   "the map; no name",
                   speaker.number);
    return {};
  }
  return localization::tr(screens::mainLabelKey(screens::SpeciesNamesBlock,
                                                static_cast<std::size_t>(character->typeId)));
}

std::string StoryTextbox::tamerName() const {
  // script_value.c: getSpeakerName, speaker 0xFD: DIGIMON_NAME(0), the name the opening asked for.
  if (const std::string& name = session_->names().tamer; !name.empty()) {
    return name;
  }
  return localization::tr(screens::mainLabelKey(screens::SpeciesNamesBlock, PlayerNameSlot));
}

std::string StoryTextbox::partnerName(const screens::NameKeys& names) const {
  // PARTNER_ENTITY.name: the name the opening asked for, else the species'.
  if (std::string name = session_->partnerView().nickname; !name.empty()) {
    return name;
  }
  return localization::tr(names.species(session_->partnerView().species));
}

std::int64_t StoryTextbox::nameColor(const Speaker& speaker) const {
  const auto& rules = story_->dialogue().rules();
  switch (speaker.kind) {
  case SpeakerKind::Player:
    return rules.playerNameColor;
  case SpeakerKind::Partner:
    return rules.partnerNameColor;
  case SpeakerKind::Special:
    return rules.specialNameColor;
  case SpeakerKind::Character:
    return rules.characterNameColor;
  case SpeakerKind::None:
    break;
  }
  return PageColor;
}

std::string StoryTextbox::pageText(const std::string& key,
                                   const assets::DigimonTable* species) const {
  const std::string text = localization::tr(key, colorArguments());
  const screens::NameKeys names{species, nullptr, nullptr};
  // script_textbox.c: showTextbox writes the names and numbers the page's codes ask for.
  auto fill = [&](std::string_view tag) -> std::optional<std::string> {
    if (tag == TamerTag) {
      return tamerName();
    }
    if (tag == PartnerTag) {
      return partnerName(names);
    }
    const auto readVariable = [&](std::int32_t variable) -> std::optional<std::size_t> {
      const auto value = story_->state().variable(variable);
      return value ? std::optional<std::size_t>{*value} : std::nullopt;
    };
    if (const auto variable = numberAfter(tag, NumberPrefix)) {
      const auto value = readVariable(*variable);
      return value ? std::optional<std::string>{std::to_string(*value)} : std::nullopt;
    }
    for (const VariableName& named : VariableNames) {
      if (const auto variable = numberAfter(tag, named.prefix)) {
        const auto value = readVariable(*variable);
        if (!value) {
          return std::nullopt;
        }
        return localization::tr(screens::mainLabelKey(named.block, *value));
      }
    }
    // Money, merit, the shop's and the tournaments' numbers come with their systems.
    core::logDebug("[port:dialogue] showTextbox (script_textbox.c:1054): {{{}}} is not filled yet",
                   tag);
    return std::nullopt;
  };
  std::string filled;
  filled.reserve(text.size());
  for (std::size_t at = 0; at < text.size(); ++at) {
    const std::size_t close = text[at] == '{' ? text.find('}', at + 1) : std::string::npos;
    if (close == std::string::npos) {
      filled += text[at];
      continue;
    }
    const std::string_view tag{text.data() + at + 1, close - at - 1};
    filled += fill(tag).value_or(std::string{text.substr(at, close - at + 1)});
    at = close;
  }
  return filled;
}

std::optional<glm::vec2> StoryTextbox::originOf(const DialogueView& view, const FieldScene& scene) {
  if (view.originPoint) {
    return scene.worldToWindowFraction(assets::toWorldPosition(
        glm::ivec3{static_cast<int>(view.originPoint->x), static_cast<int>(view.originPoint->y),
                   static_cast<int>(view.originPoint->z)}));
  }
  if (!view.originCharacter) {
    return std::nullopt;
  }
  const std::int64_t character = *view.originCharacter;
  if (character == game::story::PlayerSpeaker) {
    return scene.worldToWindowFraction(scene.field().player().position);
  }
  if (character == game::story::PartnerSpeaker) {
    return scene.worldToWindowFraction(scene.field().partner().position);
  }
  if (const assets::MapNpc* npc = characterOf(scene, character)) {
    return scene.worldToWindowFraction(assets::toWorldPosition(npc->position));
  }
  return std::nullopt;
}

} // namespace dwe::app
