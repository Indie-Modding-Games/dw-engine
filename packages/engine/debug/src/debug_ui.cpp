#include <dwe/core/log.hpp>
#include <dwe/debug/debug_ui.hpp>
#include <dwe/debug/messages.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <deque>
#include <format>
#include <mutex>
#include <string>

#include "asset_viewer.hpp"
#include "tuning_panel.hpp"

namespace dwe::debug {
namespace {

constexpr std::size_t ConsoleLines = 500;
// Descriptors the backend may create for textures: the font atlas, the asset viewer's preview and
// the previews it let go that are waiting for their last frames to finish.
constexpr std::uint32_t TextureDescriptors = 16;

// A console line that starts with this is a command of the overlay, not Lua.
constexpr char CommandPrefix = '/';
constexpr std::string_view TuningCommand = "/tuning";
constexpr const char* TuningCheckboxLabel = "tuning panel";

struct ConsoleLine {
  core::LogLevel level;
  std::string text;
};

// The last lines of the log, shared with the log sink, which may write from any thread.
struct Console {
  std::mutex mutex;
  std::deque<ConsoleLine> lines;
};

ImVec4 colorOf(core::LogLevel level) {
  switch (level) {
  case core::LogLevel::Debug:
    return {0.6F, 0.6F, 0.6F, 1.0F};
  case core::LogLevel::Warning:
    return {1.0F, 0.8F, 0.3F, 1.0F};
  case core::LogLevel::Error:
    return {1.0F, 0.4F, 0.4F, 1.0F};
  case core::LogLevel::Info:
    break;
  }
  return {0.9F, 0.9F, 0.9F, 1.0F};
}

const char* familyLabel(platform::GamepadFamily family) {
  switch (family) {
  case platform::GamepadFamily::Xbox:
    return "Xbox";
  case platform::GamepadFamily::PlayStation:
    return "PlayStation";
  case platform::GamepadFamily::Nintendo:
    return "Nintendo";
  case platform::GamepadFamily::Steam:
    return "Steam";
  case platform::GamepadFamily::Generic:
    break;
  }
  return "Generic";
}

void checkVulkan(VkResult result) {
  if (result != VK_SUCCESS) {
    core::logError("debug UI: Vulkan error {}", static_cast<int>(result));
  }
}

} // namespace

struct DebugUi::Impl {
  rhi::Device* device = nullptr;
  std::shared_ptr<Console> console = std::make_shared<Console>();
  std::array<char, 256> command{};
  bool visible = false;
  bool scrollToBottom = true;
  std::unique_ptr<AssetViewer> assets;
  bool assetsOpen = false;
  std::unique_ptr<TuningPanel> tuning;
  bool tuningOpen = false;
  OverlayShapes shapes;

  void drawStats(const FrameStats& stats);
  void drawShapes() const;
  static void drawInput(const input::InputState& input);
  void drawConsole(script::ScriptVm& scripts);
  void runConsoleLine(const std::string& line, script::ScriptVm& scripts);
  void runCommand(std::string_view name);
};

core::Result<std::unique_ptr<DebugUi>> DebugUi::create(const platform::Window& window,
                                                       rhi::Device& device) {
  auto impl = std::make_unique<Impl>();
  impl->device = &device;
  impl->assets = std::make_unique<AssetViewer>(device);

  std::weak_ptr<Console> weakConsole = impl->console;
  core::addLogSink([weakConsole](core::LogLevel level, std::string_view message) {
    if (auto console = weakConsole.lock()) {
      std::scoped_lock lock{console->mutex};
      console->lines.push_back(ConsoleLine{.level = level, .text = std::string{message}});
      if (console->lines.size() > ConsoleLines) {
        console->lines.pop_front();
      }
    }
  });

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();

  if (!ImGui_ImplSDL3_InitForVulkan(window.sdlWindow())) {
    ImGui::DestroyContext();
    return core::fail("could not start the debug UI's SDL backend");
  }

  const rhi::NativeHandles handles = device.nativeHandles();
  ImGui_ImplVulkan_InitInfo info{};
  info.ApiVersion = handles.apiVersion;
  info.Instance = handles.instance;
  info.PhysicalDevice = handles.physicalDevice;
  info.Device = handles.device;
  info.QueueFamily = handles.queueFamily;
  info.Queue = handles.queue;
  info.DescriptorPoolSize = TextureDescriptors;
  info.MinImageCount = 2;
  info.ImageCount = std::max(handles.imageCount, info.MinImageCount);
  info.UseDynamicRendering = true;
  info.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
  info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
  info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats =
      &handles.swapchainFormat;
  info.CheckVkResultFn = checkVulkan;
  if (!ImGui_ImplVulkan_Init(&info)) {
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    return core::fail("could not start the debug UI's Vulkan backend");
  }
  return std::unique_ptr<DebugUi>{new DebugUi{std::move(impl)}};
}

DebugUi::DebugUi(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}

DebugUi::~DebugUi() {
  // The textures of the asset viewer go back to the backend before it shuts down.
  impl_->assets.reset();
  impl_->device->waitIdle();
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the event goes to the ImGui context it owns.
void DebugUi::handleRawEvent(const SDL_Event& event) {
  ImGui_ImplSDL3_ProcessEvent(&event);
}

bool DebugUi::capturesKeyboard() const {
  return impl_->visible && ImGui::GetIO().WantCaptureKeyboard;
}

bool DebugUi::capturesMouse() const {
  return impl_->visible && ImGui::GetIO().WantCaptureMouse;
}

void DebugUi::mountContent(const std::filesystem::path& folder) {
  impl_->assets->mount(folder);
}

void DebugUi::toggle() {
  impl_->visible = !impl_->visible;
  impl_->scrollToBottom = true;
}

void DebugUi::attachTuning(const TuningTarget& target) {
  impl_->tuning = std::make_unique<TuningPanel>(target);
}

void DebugUi::addTuningTab(TuningTab tab) {
  if (impl_->tuning) {
    impl_->tuning->addTab(std::move(tab));
  }
}

void DebugUi::detachTuning() {
  impl_->tuning.reset();
}

void DebugUi::openTuning() {
  impl_->visible = true;
  impl_->tuningOpen = true;
}

void DebugUi::draw(const rhi::Frame& frame, const FrameStats& stats, const input::InputState& input,
                   script::ScriptVm& scripts, const Marker& marker) {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  if (marker.radius > 0.0F) {
    ImGui::GetBackgroundDrawList()->AddCircleFilled({marker.x, marker.y}, marker.radius,
                                                    IM_COL32(255, 160, 40, 255));
  }
  impl_->drawShapes();
  if (impl_->visible) {
    impl_->drawStats(stats);
    Impl::drawInput(input);
    impl_->drawConsole(scripts);
    if (impl_->assetsOpen) {
      impl_->assets->draw(impl_->assetsOpen);
    }
    if (impl_->tuningOpen && impl_->tuning) {
      impl_->tuning->draw(impl_->tuningOpen);
    }
  }
  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.commands);
}

void DebugUi::setShapes(const OverlayShapes& shapes) {
  // Copied into the vectors the last shapes left, so a shape set each frame reuses their memory.
  impl_->shapes.quads.assign(shapes.quads.begin(), shapes.quads.end());
  impl_->shapes.lines.assign(shapes.lines.begin(), shapes.lines.end());
  impl_->shapes.dots.assign(shapes.dots.begin(), shapes.dots.end());
  impl_->shapes.labels.assign(shapes.labels.begin(), shapes.labels.end());
}

void DebugUi::Impl::drawShapes() const {
  if (shapes.empty()) {
    return;
  }
  ImDrawList& list = *ImGui::GetBackgroundDrawList();
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  const auto at = [size](glm::vec2 fraction) {
    return ImVec2{fraction.x * size.x, fraction.y * size.y};
  };
  // The alpha byte; 0 draws nothing.
  const auto shows = [](OverlayColor color) { return (color >> 24U) != 0; };
  for (const OverlayQuad& quad : shapes.quads) {
    const ImVec2 a = at(quad.corners[0]);
    const ImVec2 b = at(quad.corners[1]);
    const ImVec2 c = at(quad.corners[2]);
    const ImVec2 d = at(quad.corners[3]);
    if (shows(quad.fill)) {
      list.AddQuadFilled(a, b, c, d, quad.fill);
    }
    if (shows(quad.outline)) {
      list.AddQuad(a, b, c, d, quad.outline);
    }
  }
  for (const OverlayLine& line : shapes.lines) {
    list.AddLine(at(line.from), at(line.to), line.color, line.thickness);
  }
  for (const OverlayDot& dot : shapes.dots) {
    list.AddCircleFilled(at(dot.at), dot.radius, dot.color);
  }
  constexpr float Pad = 2.0F;
  for (const OverlayLabel& label : shapes.labels) {
    const ImVec2 corner = at(label.at);
    const ImVec2 text = ImGui::CalcTextSize(label.text.c_str());
    list.AddRectFilled({corner.x - Pad, corner.y - Pad},
                       {corner.x + text.x + Pad, corner.y + text.y + Pad}, IM_COL32(0, 0, 0, 170));
    list.AddText(corner, label.color, label.text.c_str());
  }
}

void DebugUi::Impl::drawStats(const FrameStats& stats) {
  ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
  ImGui::Begin("Frame", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::TextUnformatted(
      std::format("{:.0f} fps ({:.2f} ms)", stats.framesPerSecond, stats.frameMilliseconds)
          .c_str());
  ImGui::TextUnformatted(std::format("logic ticks this frame: {}", stats.ticksThisFrame).c_str());
  ImGui::Checkbox("asset viewer", &assetsOpen);
  if (tuning) {
    ImGui::Checkbox(TuningCheckboxLabel, &tuningOpen);
  }
  ImGui::End();
}

void DebugUi::Impl::drawInput(const input::InputState& input) {
  ImGui::SetNextWindowPos({10, 110}, ImGuiCond_FirstUseEver);
  ImGui::Begin("Input", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
  const bool gamepad = input.lastDevice() == input::DeviceKind::Gamepad;
  ImGui::TextUnformatted(std::format("last device: {}", gamepad
                                                            ? familyLabel(input.lastGamepadFamily())
                                                            : "keyboard and mouse")
                             .c_str());
  ImGui::Separator();
  if (input.gamepads().empty()) {
    ImGui::BeginDisabled();
    ImGui::TextUnformatted("no gamepad connected");
    ImGui::EndDisabled();
  }
  for (const input::ConnectedGamepad& pad : input.gamepads()) {
    ImGui::Bullet();
    ImGui::TextUnformatted(std::format("{} ({})", pad.name, familyLabel(pad.family)).c_str());
  }
  ImGui::Separator();
  for (const auto& entry : input.actionMap().entries()) {
    ImGui::ProgressBar(input.value(entry.action), {120, 0}, "");
    ImGui::SameLine();
    ImGui::TextUnformatted(entry.action.c_str());
  }
  ImGui::End();
}

void DebugUi::Impl::drawConsole(script::ScriptVm& scripts) {
  ImGui::SetNextWindowPos({300, 10}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({640, 360}, ImGuiCond_FirstUseEver);
  ImGui::Begin("Console");
  const float footer = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
  ImGui::BeginChild("lines", {0, -footer}, ImGuiChildFlags_None,
                    ImGuiWindowFlags_HorizontalScrollbar);
  {
    std::scoped_lock lock{console->mutex};
    for (const ConsoleLine& line : console->lines) {
      ImGui::PushStyleColor(ImGuiCol_Text, colorOf(line.level));
      ImGui::TextUnformatted(line.text.c_str());
      ImGui::PopStyleColor();
    }
  }
  if (scrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
    ImGui::SetScrollHereY(1.0F);
  }
  scrollToBottom = false;
  ImGui::EndChild();

  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputTextWithHint("##command", "Lua", command.data(), command.size(),
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
    runConsoleLine(std::string{command.data()}, scripts);
    command.fill('\0');
    scrollToBottom = true;
    ImGui::SetKeyboardFocusHere(-1);
  }
  ImGui::End();
}

void DebugUi::Impl::runConsoleLine(const std::string& line, script::ScriptVm& scripts) {
  if (line.empty()) {
    return;
  }
  core::logInfo("> {}", line);
  if (line.front() == CommandPrefix) {
    runCommand(line);
  } else if (auto result = scripts.runString(line, "console"); !result) {
    core::logError("{}", result.error().message);
  }
}

void DebugUi::Impl::runCommand(std::string_view name) {
  if (name == TuningCommand && tuning) {
    tuningOpen = true;
    return;
  }
  core::logError("{}", messages::formatMessage(messages::UnknownCommand, name, TuningCommand));
}

} // namespace dwe::debug
