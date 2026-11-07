#include "first_run_screen.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/text_table.hpp>
#include <dwe/content/dev_disc.hpp>
#include <dwe/content/disc_install.hpp>
#include <dwe/content/user_data.hpp>
#include <dwe/core/log.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/platform/locale.hpp>
#include <dwe/platform/process.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "language.hpp"
#include "user_settings.hpp"

namespace dwe::app {
namespace {

// The languages the screen has texts for, and the table they are in.
constexpr std::array<std::string_view, 2> Languages{"en", "es"};
constexpr std::string_view TextTable = "first_run";
// The font of the screen, from the game pack, and its sizes at a 720-pixel-high window.
// The team's game pack and the files of it the screen reads (content::readTeamPackFile).
constexpr std::string_view GamePack = "game";
constexpr std::string_view FontFile = "ui/fonts/noto_sans_regular.ttf";
constexpr float TextSize = 20.0F;
constexpr float TitleSize = 32.0F;
constexpr float ReferenceHeight = 720.0F;
// The panel's share of the window.
constexpr float PanelWidth = 0.78F;
constexpr float PanelHeight = 0.82F;
// The descriptors ImGui's Vulkan backend may use: its font atlas and nothing else.
constexpr std::uint32_t TextureDescriptors = 8;
constexpr std::size_t PathCapacity = 4096;

void checkVulkan(VkResult result) {
  if (result != VK_SUCCESS) {
    core::logError("first run: Vulkan error {}", static_cast<int>(result));
  }
}

// The texts of one language, read from the team's game pack; a missing one shows as its key.
class Texts {
public:
  void load(std::string_view language) {
    language_ = language;
    texts_ = read(language);
    if (language != assets::ReferenceLanguage) {
      fallback_ = read(assets::ReferenceLanguage);
    } else {
      fallback_ = texts_;
    }
  }

  [[nodiscard]] const std::string& language() const { return language_; }

  [[nodiscard]] std::string
  operator()(std::string_view key,
             std::span<const localization::TextArgument> arguments = {}) const {
    const std::string full = std::format("{}.{}", TextTable, key);
    for (const assets::TextTable* table : {&texts_, &fallback_}) {
      if (const auto found = table->find(full); found != table->end()) {
        return localization::formatText(found->second, arguments);
      }
    }
    return full;
  }

private:
  [[nodiscard]] static assets::TextTable read(std::string_view language) {
    const std::string file =
        std::format("{}/{}/{}.yaml", assets::EngineTextFolder, language, TextTable);
    const auto bytes = content::readTeamPackFile(content::teamPacksFolder(), GamePack, file);
    if (!bytes) {
      core::logWarning("first run: {}: {}", file, bytes.error().message);
      return {};
    }
    auto table = assets::parseTextTable(
        {.kind = assets::TextTableKind::Engine,
         .language = std::string{language},
         .name = std::string{TextTable}},
        std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
    if (!table) {
      core::logWarning("first run: {}: {}", file, table.error().message);
      return {};
    }
    return std::move(*table);
  }

  std::string language_;
  assets::TextTable texts_;
  assets::TextTable fallback_;
};

// The file the system's file dialog picked. The dialog may answer on another thread, and after
// the screen is gone, so it lives as long as the program.
struct PickedFile {
  std::mutex mutex;
  std::optional<std::string> path;
  bool open = false;
};

PickedFile& pickedFile() {
  static PickedFile picked;
  return picked;
}

void SDLCALL onFilePicked(void* /*userdata*/, const char* const* files, int /*filter*/) {
  PickedFile& picked = pickedFile();
  const std::scoped_lock lock{picked.mutex};
  picked.open = false;
  if (files != nullptr && files[0] != nullptr) {
    picked.path = files[0];
  }
}

// `imageFilter` names the disc images in the dialog's list of file kinds, in the player's language.
void openFileDialog(platform::Window& window, const std::string& imageFilter) {
  // The dialog reads the filters until it calls back: they live as long as the program, and change
  // only while no dialog is open.
  static std::string filterName;
  static std::array<SDL_DialogFileFilter, 2> filters{
      SDL_DialogFileFilter{.name = "", .pattern = "cue;bin;iso"},
      SDL_DialogFileFilter{.name = "*", .pattern = "*"}};
  {
    PickedFile& picked = pickedFile();
    const std::scoped_lock lock{picked.mutex};
    if (picked.open) {
      return;
    }
    picked.open = true;
  }
  filterName = imageFilter;
  filters.front().name = filterName.c_str();
  const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME);
  SDL_ShowOpenFileDialog(onFilePicked, nullptr, window.sdlWindow(), filters.data(),
                         static_cast<int>(filters.size()), home, false);
}

std::optional<std::string> takePickedFile() {
  PickedFile& picked = pickedFile();
  const std::scoped_lock lock{picked.mutex};
  return std::exchange(picked.path, std::nullopt);
}

// Sets ImGui up on the game's window and device, and takes it down again.
class ImGuiSession {
public:
  static core::Result<std::unique_ptr<ImGuiSession>> create(platform::Window& window,
                                                            rhi::Device& device) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    // ImGui keeps the font's bytes and frees them with the atlas.
    if (const auto font = content::readTeamPackFile(content::teamPacksFolder(), GamePack, FontFile);
        font) {
      void* bytes = IM_ALLOC(font->size());
      std::memcpy(bytes, font->data(), font->size());
      io.Fonts->AddFontFromMemoryTTF(bytes, static_cast<int>(font->size()), TextSize);
    } else {
      core::logWarning("first run: no font ({}), using ImGui's own", font.error().message);
    }
    if (!ImGui_ImplSDL3_InitForVulkan(window.sdlWindow())) {
      ImGui::DestroyContext();
      return core::fail("could not start the first start screen's SDL backend");
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
      return core::fail("could not start the first start screen's Vulkan backend");
    }
    return std::unique_ptr<ImGuiSession>{new ImGuiSession{device}};
  }

  ImGuiSession(const ImGuiSession&) = delete;
  ImGuiSession& operator=(const ImGuiSession&) = delete;
  ImGuiSession(ImGuiSession&&) = delete;
  ImGuiSession& operator=(ImGuiSession&&) = delete;
  ~ImGuiSession() {
    device_->waitIdle();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
  }

private:
  explicit ImGuiSession(rhi::Device& device) : device_{&device} {}

  rhi::Device* device_;
};

// The message of a failed install, in the player's language.
std::string problemText(const Texts& tr, const content::DiscInstall& install) {
  const std::string& detail = install.detail();
  switch (install.problem()) {
  case content::InstallProblem::MissingFile: {
    const std::array arguments{localization::TextArgument{.name = "file", .value = detail}};
    return tr("error.missing_file", arguments);
  }
  case content::InstallProblem::CannotRead: {
    const std::array arguments{localization::TextArgument{.name = "detail", .value = detail}};
    return tr("error.cannot_read", arguments);
  }
  case content::InstallProblem::NotTheGame:
    return tr("error.not_the_game");
  case content::InstallProblem::OtherRegion: {
    // dwx identify names the release (eu, jp, jp_rev1); the player reads which one it is.
    const std::string key = std::format("region.{}", detail);
    std::string region = tr(key);
    if (region == std::format("{}.{}", TextTable, key)) {
      region = tr("region.other");
    }
    const std::array arguments{localization::TextArgument{.name = "region", .value = region}};
    return tr("error.other_region", arguments);
  }
  case content::InstallProblem::ExtractFailed: {
    const std::array arguments{localization::TextArgument{.name = "detail", .value = detail}};
    return tr("error.extract_failed", arguments);
  }
  case content::InstallProblem::NoTool: {
    const std::array arguments{localization::TextArgument{.name = "detail", .value = detail}};
    return tr("error.no_tool", arguments);
  }
  case content::InstallProblem::None:
    break;
  }
  return {};
}

// The path as typed or dropped: without surrounding spaces or the quotes a file manager adds when a
// path is pasted, and with a leading ~ as the home folder.
std::filesystem::path typedPath(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\'' || text.front() == '"')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\'' || text.back() == '"' ||
                           text.back() == '\n' || text.back() == '\r')) {
    text.remove_suffix(1);
  }
  if (text.starts_with("file://")) {
    text.remove_prefix(std::string_view{"file://"}.size());
  }
  if (text.starts_with("~/")) {
    if (const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME); home != nullptr) {
      return std::filesystem::path{std::u8string{reinterpret_cast<const char8_t*>(home)}} /
             std::filesystem::path{std::u8string{text.begin() + 2, text.end()}};
    }
  }
  return std::filesystem::path{std::u8string{text.begin(), text.end()}};
}

} // namespace

bool needsFirstRun(const Options& options) {
  if (options.contentGiven || options.devAssets || options.autotest || options.control) {
    return false;
  }
  if (!content::hasBasePack(options.content)) {
    return true;
  }
  // Only the content the first start installed has a record: a ./content of dwx extract does not.
  const auto userData =
      platform::userDataFolder(content::UserDataOrganization, content::UserDataApplication);
  if (!userData) {
    return false;
  }
  std::error_code error;
  const bool installed = std::filesystem::equivalent(
      options.content, *userData / content::InstalledContentFolderName, error);
  if (installed && content::installIsOutdated(options.content)) {
    core::logInfo("first run: the installed content is of another extractor version; reading the "
                  "disc again");
    return true;
  }
  return false;
}

core::Result<std::optional<std::filesystem::path>>
runFirstRun(platform::Platform& platform, platform::Window& window, rhi::Device& device,
            const std::optional<std::string>& language) {
  auto userData =
      platform::userDataFolder(content::UserDataOrganization, content::UserDataApplication);
  auto session = ImGuiSession::create(window, device);
  if (!session) {
    return std::unexpected{session.error()};
  }

  // The player's language: --lang, the saved one, or the system's, as the game picks it.
  const std::optional<std::filesystem::path> settingsFile = userSettingsFile();
  UserSettings settings = settingsFile ? loadUserSettings(*settingsFile) : UserSettings{};
  const std::vector<std::string> available{Languages.begin(), Languages.end()};
  Texts tr;
  tr.load(startLanguage(available, language, settings.language, platform::systemLanguage())
              .value_or(std::string{assets::ReferenceLanguage}));

  std::optional<std::filesystem::path> dwx;
  std::string noTool;
  if (const auto folder = platform::executableFolder(); folder) {
    if (auto found = content::findDwx(content::dwxCandidates(*folder)); found) {
      dwx = std::move(*found);
    } else {
      noTool = found.error().message;
    }
  } else {
    noTool = folder.error().message;
  }

  std::array<char, PathCapacity> path{};
  // A game of another extractor version than the installed content's reads the same disc again: the
  // screen says why and fills in the image the record names.
  const bool update =
      userData && content::installIsOutdated(*userData / content::InstalledContentFolderName);
  const std::optional<content::InstallRecord> record =
      update ? content::readInstallRecord(*userData / content::InstalledContentFolderName)
             : std::nullopt;
  std::optional<content::DiscInstall> install;
  bool focusPath = true;
  std::vector<platform::Event> events;
  const auto begin = [&] {
    const std::filesystem::path image = typedPath(path.data());
    if (!userData) {
      install =
          content::DiscInstall::failed(content::InstallProblem::NoTool, userData.error().message);
    } else if (!dwx) {
      install = content::DiscInstall::failed(content::InstallProblem::NoTool, noTool);
    } else {
      install = content::DiscInstall::start(image, *userData, *dwx);
    }
  };
  const auto setPath = [&](std::string_view text) {
    const std::size_t size = std::min(text.size(), path.size() - 1);
    std::ranges::copy(text.substr(0, size), path.begin());
    path.at(size) = '\0';
  };
  if (record && !record->image.empty()) {
    setPath(assets::pathText(record->image));
  }

  for (;;) {
    events.clear();
    bool quit = false;
    platform.pollEvents(events, [&](const SDL_Event& raw) {
      ImGui_ImplSDL3_ProcessEvent(&raw);
      if (raw.type == SDL_EVENT_DROP_FILE && raw.drop.data != nullptr) {
        setPath(raw.drop.data);
      }
    });
    quit = std::ranges::any_of(events, [](const platform::Event& event) {
      return std::holds_alternative<platform::QuitRequested>(event);
    });
    if (const auto picked = takePickedFile()) {
      setPath(*picked);
    }
    const bool working = install && (install->stage() == content::DiscInstall::Stage::Identifying ||
                                     install->stage() == content::DiscInstall::Stage::Extracting);
    if (quit) {
      // Leaving stops a running install and removes what it wrote (DiscInstall's destructor).
      return std::optional<std::filesystem::path>{};
    }
    if (install) {
      install->update();
      if (install->stage() == content::DiscInstall::Stage::Finished) {
        return std::optional{install->contentFolder()};
      }
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float scale = std::max(0.75F, viewport->Size.y / ReferenceHeight);
    ImGui::GetStyle().FontScaleMain = scale;
    ImGui::SetNextWindowPos(
        {viewport->Pos.x + viewport->Size.x * 0.5F, viewport->Pos.y + viewport->Size.y * 0.5F},
        ImGuiCond_Always, {0.5F, 0.5F});
    ImGui::SetNextWindowSize({viewport->Size.x * PanelWidth, viewport->Size.y * PanelHeight},
                             ImGuiCond_Always);
    ImGui::Begin("first-run", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(nullptr, TitleSize);
    ImGui::TextUnformatted(tr(update ? "update_title" : "title").c_str());
    ImGui::PopFont();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextWrapped("%s", tr(update ? "update_intro" : "intro").c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("%s", tr("space").c_str());
    if (userData) {
      const std::array arguments{localization::TextArgument{
          .name = "folder",
          .value = assets::pathText(*userData / content::InstalledContentFolderName)}};
      ImGui::TextWrapped("%s", tr("destination", arguments).c_str());
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginDisabled(working);
    ImGui::TextUnformatted(tr("image_label").c_str());
    const float browseWidth =
        ImGui::CalcTextSize(tr("browse").c_str()).x + ImGui::GetStyle().FramePadding.x * 4.0F;
    ImGui::SetNextItemWidth(-browseWidth - ImGui::GetStyle().ItemSpacing.x);
    if (std::exchange(focusPath, false)) {
      ImGui::SetKeyboardFocusHere();
    }
    const bool entered =
        ImGui::InputText("##image", path.data(), path.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button(tr("browse").c_str())) {
      openFileDialog(window, tr("image_filter"));
    }
    ImGui::TextDisabled("%s", tr("drop_hint").c_str());
    ImGui::Spacing();
    const bool start = ImGui::Button(tr("install").c_str()) || entered;
    ImGui::SameLine();
    const bool leave = ImGui::Button(tr("quit").c_str());
    ImGui::EndDisabled();
    if (working) {
      ImGui::SameLine();
      if (ImGui::Button(tr("cancel").c_str())) {
        install->cancel();
        install.reset();
        focusPath = true;
      }
    }

    ImGui::Spacing();
    if (install && install->stage() == content::DiscInstall::Stage::Identifying) {
      ImGui::TextUnformatted(tr("step.identify").c_str());
      ImGui::ProgressBar(-1.0F * static_cast<float>(ImGui::GetTime()), {-1.0F, 0.0F});
    } else if (install && install->stage() == content::DiscInstall::Stage::Extracting) {
      ImGui::TextUnformatted(tr(std::format("step.{}", install->step())).c_str());
      ImGui::ProgressBar(static_cast<float>(install->percent()) / 100.0F, {-1.0F, 0.0F});
    } else if (install && install->stage() == content::DiscInstall::Stage::Failed &&
               install->problem() != content::InstallProblem::None) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0F, 0.45F, 0.4F, 1.0F});
      ImGui::TextWrapped("%s", problemText(tr, *install).c_str());
      ImGui::PopStyleColor();
    }

    // The language, at the bottom: the screen and the game that follows switch to it.
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeightWithSpacing() * 2.0F);
    ImGui::Separator();
    ImGui::TextDisabled("%s", tr("keys").c_str());
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::CalcTextSize("English  Español").x -
                         ImGui::GetStyle().FramePadding.x * 6.0F);
    for (const auto& [code, name] : std::array<std::pair<std::string_view, const char*>, 2>{
             {{"en", "English"}, {"es", "Español"}}}) {
      if (code != Languages.front()) {
        ImGui::SameLine();
      }
      const bool chosen = tr.language() == code;
      ImGui::BeginDisabled(chosen);
      if (ImGui::Button(name)) {
        tr.load(code);
        settings.language = std::string{code};
        if (settingsFile) {
          saveUserSettings(*settingsFile, settings);
        }
      }
      ImGui::EndDisabled();
    }
    ImGui::End();
    ImGui::Render();

    if (start && !working) {
      begin();
    }
    if (leave || (!working && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
      return std::optional<std::filesystem::path>{};
    }

    const auto frame = device.beginFrame();
    if (!frame) {
      platform.waitForEvents(std::chrono::milliseconds{50});
      continue;
    }
    device.beginRendering(*frame, {.red = 0.05F, .green = 0.06F, .blue = 0.09F, .alpha = 1.0F});
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame->commands);
    device.endRendering(*frame);
    device.endFrame(*frame);
  }
}

} // namespace dwe::app
