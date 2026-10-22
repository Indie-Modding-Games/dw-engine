#include <dwe/core/message.hpp>
#include <dwe/platform/process.hpp>

#include <SDL3/SDL.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::platform {
namespace {

#ifdef _WIN32
constexpr std::string_view ExecutableExtension = ".exe";
#else
constexpr std::string_view ExecutableExtension;
#endif

struct ProcessDeleter {
  void operator()(SDL_Process* process) const { SDL_DestroyProcess(process); }
};
using ProcessHandle = std::unique_ptr<SDL_Process, ProcessDeleter>;

struct OutputDeleter {
  void operator()(void* output) const { SDL_free(output); }
};
using OutputBuffer = std::unique_ptr<void, OutputDeleter>;

// The program's own properties, destroyed when they are no longer needed.
class ProcessProperties {
public:
  ProcessProperties() : id_(SDL_CreateProperties()) {}
  ProcessProperties(const ProcessProperties&) = delete;
  ProcessProperties& operator=(const ProcessProperties&) = delete;
  ProcessProperties(ProcessProperties&&) = delete;
  ProcessProperties& operator=(ProcessProperties&&) = delete;
  ~ProcessProperties() {
    if (id_ != 0) {
      SDL_DestroyProperties(id_);
    }
  }

  [[nodiscard]] SDL_PropertiesID id() const { return id_; }

private:
  SDL_PropertiesID id_;
};

// SDL takes the arguments as a null-terminated array of C strings.
std::vector<const char*> argumentPointers(std::span<const std::string> arguments) {
  std::vector<const char*> pointers;
  pointers.reserve(arguments.size() + 1);
  for (const std::string& argument : arguments) {
    pointers.push_back(argument.c_str());
  }
  pointers.push_back(nullptr);
  return pointers;
}

core::Result<ProcessHandle> startProcess(std::span<const std::string> arguments) {
  const ProcessProperties properties;
  if (properties.id() == 0) {
    return core::fail(
        core::formatMessage(messages::CannotStartProcess, arguments.front(), SDL_GetError()));
  }
  std::vector<const char*> pointers = argumentPointers(arguments);
  SDL_SetPointerProperty(properties.id(), SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                         static_cast<void*>(pointers.data()));
  SDL_SetNumberProperty(properties.id(), SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                        SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(properties.id(), SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                        SDL_PROCESS_STDIO_APP);
  SDL_SetNumberProperty(properties.id(), SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
                        SDL_PROCESS_STDIO_INHERITED);
  ProcessHandle process{SDL_CreateProcessWithProperties(properties.id())};
  if (!process) {
    return core::fail(
        core::formatMessage(messages::CannotStartProcess, arguments.front(), SDL_GetError()));
  }
  return process;
}

} // namespace

core::Result<ProcessResult> runProcess(std::span<const std::string> arguments) {
  if (arguments.empty()) {
    return core::fail(std::string{messages::NoProgram});
  }
  auto process = startProcess(arguments);
  if (!process) {
    return std::unexpected{process.error()};
  }
  std::size_t size = 0;
  int exitCode = 0;
  // Reads everything the program writes and waits for it to end.
  const OutputBuffer output{SDL_ReadProcess(process->get(), &size, &exitCode)};
  if (!output) {
    std::string error =
        core::formatMessage(messages::CannotReadProcess, arguments.front(), SDL_GetError());
    // The program may still be running: end it and wait for it, so it never outlives the call.
    SDL_KillProcess(process->get(), true);
    SDL_WaitProcess(process->get(), true, nullptr);
    return core::fail(std::move(error));
  }
  return ProcessResult{.exitCode = exitCode,
                       .output = std::string{static_cast<const char*>(output.get()), size}};
}

std::string executableFileName(std::string_view program) {
  return std::string{program} + std::string{ExecutableExtension};
}

core::Result<std::filesystem::path> executableFolder() {
  const char* folder = SDL_GetBasePath();
  if (folder == nullptr) {
    return core::fail(core::formatMessage(messages::NoExecutableFolder, SDL_GetError()));
  }
  // SDL gives UTF-8, which a path only reads as such from char8_t (on Windows a char path is the
  // ANSI code page), and ends it with a separator, which would leave the path without a file name.
  const std::filesystem::path path{std::u8string{reinterpret_cast<const char8_t*>(folder)}};
  return path.has_filename() ? path : path.parent_path();
}

} // namespace dwe::platform
