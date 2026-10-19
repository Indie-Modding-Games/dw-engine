#include <dwe/core/log.hpp>
#include <dwe/debug/debug_ui.hpp>

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

namespace dwe::debug {
namespace {

constexpr std::size_t ConsoleLines = 500;
// Descriptors the backend may create for textures: the font atlas, the asset viewer's preview and the previews it
// let go that are waiting for their last frames to finish.
constexpr std::uint32_t TextureDescriptors = 16;

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

    void drawStats(const FrameStats& stats);
    static void drawInput(const input::InputState& input);
    void drawConsole(script::ScriptVm& scripts);
};

core::Result<std::unique_ptr<DebugUi>> DebugUi::create(const platform::Window& window, rhi::Device& device) {
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
    info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &handles.swapchainFormat;
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

void DebugUi::draw(const rhi::Frame& frame, const FrameStats& stats, const input::InputState& input,
                   script::ScriptVm& scripts, const Marker& marker) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (marker.radius > 0.0F) {
        ImGui::GetBackgroundDrawList()->AddCircleFilled({marker.x, marker.y}, marker.radius,
                                                        IM_COL32(255, 160, 40, 255));
    }
    if (impl_->visible) {
        impl_->drawStats(stats);
        Impl::drawInput(input);
        impl_->drawConsole(scripts);
        if (impl_->assetsOpen) {
            impl_->assets->draw(impl_->assetsOpen);
        }
    }
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.commands);
}

void DebugUi::Impl::drawStats(const FrameStats& stats) {
    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Frame", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted(
        std::format("{:.0f} fps ({:.2f} ms)", stats.framesPerSecond, stats.frameMilliseconds).c_str());
    ImGui::TextUnformatted(std::format("logic ticks this frame: {}", stats.ticksThisFrame).c_str());
    ImGui::Checkbox("asset viewer", &assetsOpen);
    ImGui::End();
}

void DebugUi::Impl::drawInput(const input::InputState& input) {
    ImGui::SetNextWindowPos({10, 110}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Input", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    const bool gamepad = input.lastDevice() == input::DeviceKind::Gamepad;
    ImGui::TextUnformatted(
        std::format("last device: {}", gamepad ? familyLabel(input.lastGamepadFamily()) : "keyboard and mouse")
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
    ImGui::BeginChild("lines", {0, -footer}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
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
        const std::string source{command.data()};
        if (!source.empty()) {
            core::logInfo("> {}", source);
            if (auto result = scripts.runString(source, "console"); !result) {
                core::logError("{}", result.error().message);
            }
        }
        command.fill('\0');
        scrollToBottom = true;
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::End();
}

} // namespace dwe::debug
