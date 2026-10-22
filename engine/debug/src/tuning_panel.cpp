#include "tuning_panel.hpp"

#include <dwe/ruleset/ruleset_values.hpp>

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::debug {

namespace {

// The labels of the window. ImGui takes "visible##id" labels: what follows ## only tells widgets
// apart.
constexpr const char* WindowTitle = "Tuning";
constexpr const char* TabBarId = "tuning tabs";
constexpr const char* RulesetTabLabel = "Ruleset";
constexpr const char* ClockTabLabel = "Clock";
constexpr const char* SystemsTabLabel = "Systems";
constexpr const char* RulesetComboLabel = "ruleset";
constexpr const char* RefreshLabel = "list again";
constexpr const char* PreviewReloadLabel = "what would reloading change?";
constexpr const char* ReloadLabel = "reload from files";
constexpr const char* SaveLabel = "save to file";
constexpr const char* RunningLabel = "running";
constexpr const char* SpeedLabel = "speed";
constexpr const char* HourLabel = "hour";
constexpr const char* MinuteLabel = "minute";
constexpr const char* JumpLabel = "jump (as sleeping)";
constexpr const char* NoClockText = "the game has no clock attached";
constexpr const char* SpeedFormat = "x%.0f";
constexpr const char* PathSeparatorId = "/";
constexpr const char* SectionsTableId = "sections";
constexpr const char* SectionColumn = "section";
constexpr const char* OwnerColumn = "from ruleset";
constexpr const char* FileColumn = "file";
constexpr const char* MissingValueText = "-";
constexpr std::string_view RulesetLineFormat = "{} {} ({}), revision {}";
constexpr std::string_view ExtendsFormat = "extends {}";
constexpr std::string_view ExtendsNothingText = "extends nothing";
constexpr std::string_view SectionHeaderFormat = "{} ({})";
constexpr std::string_view SpeedButtonFormat = "x{:.0f}";
constexpr std::string_view ClockLineFormat = "{}, frame of day {}";
constexpr std::string_view SystemsLineFormat = "ruleset {}, revision {}";
constexpr std::string_view RowFormat = "{}: {}";
constexpr int SectionsColumns = 3;
constexpr std::string_view ReloadChangeFormat = "{}.{}: {} -> {}";

// Where and how big the window opens the first time.
constexpr ImVec2 WindowPosition{700, 10};
constexpr ImVec2 WindowSize{560, 640};
constexpr float ValueWidth = 160.0F;
constexpr float HourWidth = 90.0F;
constexpr float MeterWidth = 160.0F;
constexpr ImVec4 ErrorColor{1.0F, 0.4F, 0.4F, 1.0F};
constexpr ImVec4 StatusColor{0.5F, 0.9F, 0.5F, 1.0F};
constexpr ImVec4 DimColor{0.6F, 0.6F, 0.6F, 1.0F};
constexpr float MaxSpeed = 3600.0F;
constexpr std::int32_t LastHour = 23;
constexpr std::int32_t LastMinute = 59;

void textLine(const std::string& text) {
  ImGui::TextUnformatted(text.c_str());
}

void coloredLine(const ImVec4& color, const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::PushTextWrapPos(0.0F);
  ImGui::TextUnformatted(text.c_str());
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

std::string optionalText(const std::optional<std::string>& value) {
  return value.value_or(std::string{MissingValueText});
}

} // namespace

void TuningPanel::draw(bool& open) {
  ImGui::SetNextWindowPos(WindowPosition, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(WindowSize, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(WindowTitle, &open)) {
    ImGui::End();
    return;
  }
  if (ImGui::BeginTabBar(TabBarId)) {
    if (ImGui::BeginTabItem(RulesetTabLabel)) {
      drawRulesetTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(ClockTabLabel)) {
      drawClockTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(SystemsTabLabel)) {
      drawSystemsTab();
      ImGui::EndTabItem();
    }
    for (const TuningTab& tab : tabs_) {
      if (ImGui::BeginTabItem(tab.name.c_str())) {
        drawCustomTab(tab);
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void TuningPanel::drawRulesetTab() {
  drawRulesetChooser();
  drawOutcome();
  drawReload();
  ImGui::Separator();
  for (const TuningSection& section : model_.sections()) {
    drawSection(section);
  }
}

void TuningPanel::drawRulesetChooser() {
  const ruleset::RulesetInfo& info = model_.active();
  if (ImGui::BeginCombo(RulesetComboLabel, info.id.c_str())) {
    // A copy: selecting changes the active ruleset under the loop.
    const std::vector<std::string> ids = model_.rulesets();
    for (const std::string& id : ids) {
      if (ImGui::Selectable(id.c_str(), id == info.id) && id != info.id) {
        model_.select(id);
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button(RefreshLabel)) {
    model_.refreshRulesets();
  }
  const std::string extends =
      info.chain.size() > 1 ? std::vformat(ExtendsFormat, std::make_format_args(info.chain[1]))
                            : std::string{ExtendsNothingText};
  const std::uint64_t revision = model_.revision();
  textLine(std::vformat(RulesetLineFormat,
                        std::make_format_args(info.name, info.version, extends, revision)));
}

void TuningPanel::drawOutcome() {
  if (!model_.error().empty()) {
    coloredLine(ErrorColor, model_.error());
  } else if (!model_.status().empty()) {
    coloredLine(StatusColor, model_.status());
  }
}

void TuningPanel::drawReload() {
  if (ImGui::Button(PreviewReloadLabel)) {
    model_.previewReload();
  }
  ImGui::SameLine();
  if (ImGui::Button(ReloadLabel)) {
    model_.reload();
  }
  const auto& changes = model_.reloadChanges();
  if (changes.empty()) {
    return;
  }
  for (const ruleset::ValueDifference& change : changes) {
    const std::string before = optionalText(change.before);
    const std::string after = optionalText(change.after);
    ImGui::Bullet();
    textLine(std::vformat(ReloadChangeFormat,
                          std::make_format_args(change.section, change.path, before, after)));
  }
}

void TuningPanel::drawSection(const TuningSection& section) {
  ImGui::PushID(section.name.c_str());
  const std::string header =
      std::vformat(SectionHeaderFormat, std::make_format_args(section.name, section.owner));
  if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
    coloredLine(DimColor, section.file);
    if (ImGui::Button(SaveLabel)) {
      model_.save(section.name);
    }
    for (const TuningNode& child : section.root.children) {
      drawNode(section, child);
    }
  }
  ImGui::PopID();
}

void TuningPanel::drawNode(const TuningSection& section, const TuningNode& node) {
  if (node.kind != TuningValueKind::Group) {
    drawValue(section, node);
    return;
  }
  ImGui::PushID(node.path.c_str());
  if (ImGui::TreeNodeEx(node.key.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
    for (const TuningNode& child : node.children) {
      drawNode(section, child);
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}

void TuningPanel::drawValue(const TuningSection& section, const TuningNode& node) {
  ImGui::PushID(node.path.c_str());
  if (node.kind == TuningValueKind::Flag) {
    bool on = node.text == FlagOnText;
    if (ImGui::Checkbox(node.key.c_str(), &on)) {
      model_.edit(section.name, node, on ? FlagOnText : FlagOffText);
    }
    ImGui::PopID();
    return;
  }
  EditBuffer& buffer = bufferOf(section, node);
  ImGui::SetNextItemWidth(ValueWidth);
  if (ImGui::InputText(node.key.c_str(), buffer.data(), buffer.size(),
                       ImGuiInputTextFlags_EnterReturnsTrue)) {
    if (!model_.edit(section.name, node, buffer.data())) {
      // Refused: the field shows the value that stays.
      std::ranges::fill(buffer, '\0');
      std::ranges::copy_n(
          node.text.begin(),
          static_cast<std::ptrdiff_t>(std::min(node.text.size(), buffer.size() - 1)),
          buffer.begin());
    }
  }
  ImGui::PopID();
}

TuningPanel::EditBuffer& TuningPanel::bufferOf(const TuningSection& section,
                                               const TuningNode& node) {
  if (buffersRevision_ != model_.revision()) {
    buffers_.clear();
    buffersRevision_ = model_.revision();
  }
  const std::string key = section.name + PathSeparatorId + node.path;
  auto [found, added] = buffers_.try_emplace(key);
  if (added) {
    found->second.fill('\0');
    std::ranges::copy_n(
        node.text.begin(),
        static_cast<std::ptrdiff_t>(std::min(node.text.size(), found->second.size() - 1)),
        found->second.begin());
  }
  return found->second;
}

void TuningPanel::drawClockTab() {
  ruleset::GameClock* clock = model_.target().clock;
  if (clock == nullptr) {
    coloredLine(DimColor, NoClockText);
    return;
  }
  const std::string time = describeTime(clock->time());
  const std::int64_t frame = clock->frameOfDay();
  textLine(std::vformat(ClockLineFormat, std::make_format_args(time, frame)));
  bool running = clock->running();
  if (ImGui::Checkbox(RunningLabel, &running)) {
    clock->setRunning(running);
  }
  drawClockSpeed(*clock);
  ImGui::Separator();
  drawClockJump(*clock);
}

void TuningPanel::drawClockSpeed(ruleset::GameClock& clock) {
  auto speed = static_cast<float>(clock.speed());
  if (ImGui::SliderFloat(SpeedLabel, &speed, 0.0F, MaxSpeed, SpeedFormat,
                         ImGuiSliderFlags_Logarithmic)) {
    clock.setSpeed(speed);
  }
  for (const double preset : ClockSpeedPresets) {
    const std::string label = std::vformat(SpeedButtonFormat, std::make_format_args(preset));
    if (ImGui::Button(label.c_str())) {
      clock.setSpeed(preset);
    }
    ImGui::SameLine();
  }
  ImGui::NewLine();
}

void TuningPanel::drawClockJump(ruleset::GameClock& clock) {
  ImGui::SetNextItemWidth(HourWidth);
  ImGui::InputInt(HourLabel, &jumpHour_);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(HourWidth);
  ImGui::InputInt(MinuteLabel, &jumpMinute_);
  jumpHour_ = std::clamp(jumpHour_, 0, LastHour);
  jumpMinute_ = std::clamp(jumpMinute_, 0, LastMinute);
  if (ImGui::Button(JumpLabel)) {
    clock.jumpTo(jumpHour_, jumpMinute_);
  }
}

void TuningPanel::drawSystemsTab() {
  const std::uint64_t revision = model_.revision();
  textLine(std::vformat(SystemsLineFormat, std::make_format_args(model_.active().id, revision)));
  if (!ImGui::BeginTable(SectionsTableId, SectionsColumns,
                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    return;
  }
  ImGui::TableSetupColumn(SectionColumn);
  ImGui::TableSetupColumn(OwnerColumn);
  ImGui::TableSetupColumn(FileColumn);
  ImGui::TableHeadersRow();
  for (const TuningSection& section : model_.sections()) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    textLine(section.name);
    ImGui::TableNextColumn();
    textLine(section.owner);
    ImGui::TableNextColumn();
    textLine(section.file);
  }
  ImGui::EndTable();
}

void TuningPanel::drawCustomTab(const TuningTab& tab) {
  for (const TuningRow& row : tab.rows()) {
    if (row.meter) {
      ImGui::ProgressBar(std::clamp(*row.meter, 0.0F, 1.0F), {MeterWidth, 0}, row.value.c_str());
      ImGui::SameLine();
      textLine(row.label);
    } else {
      textLine(std::vformat(RowFormat, std::make_format_args(row.label, row.value)));
    }
  }
}

} // namespace dwe::debug
