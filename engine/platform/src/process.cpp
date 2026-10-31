#include <dwe/core/message.hpp>
#include <dwe/platform/process.hpp>

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
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

// How a started program's standard error goes: to this program's, or into its own output.
enum class ErrorOutput : std::uint8_t { Inherited, Merged };

core::Result<ProcessHandle> startProcess(std::span<const std::string> arguments,
                                         ErrorOutput errors = ErrorOutput::Inherited) {
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
  if (errors == ErrorOutput::Merged) {
    SDL_SetBooleanProperty(properties.id(), SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
  } else {
    SDL_SetNumberProperty(properties.id(), SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
                          SDL_PROCESS_STDIO_INHERITED);
  }
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

void ChildProcess::Deleter::operator()(SDL_Process* process) const {
  // A program still running is ended first: it never outlives its ChildProcess.
  int ignored = 0;
  if (!SDL_WaitProcess(process, false, &ignored)) {
    SDL_KillProcess(process, true);
    SDL_WaitProcess(process, true, &ignored);
  }
  SDL_DestroyProcess(process);
}

core::Result<ChildProcess> ChildProcess::start(std::span<const std::string> arguments) {
  if (arguments.empty()) {
    return core::fail(std::string{messages::NoProgram});
  }
  auto process = startProcess(arguments, ErrorOutput::Merged);
  if (!process) {
    return std::unexpected{process.error()};
  }
  return ChildProcess{process->release()};
}

ChildProcess::ChildProcess(SDL_Process* process) : process_{process} {}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept = default;
ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept = default;
ChildProcess::~ChildProcess() = default;

bool ChildProcess::drain() {
  if (outputEnded_ || !process_) {
    return true;
  }
  SDL_IOStream* output = SDL_GetProcessOutput(process_.get());
  if (output == nullptr) {
    outputEnded_ = true;
    return true;
  }
  std::array<char, 4096> chunk{};
  for (;;) {
    const std::size_t read = SDL_ReadIO(output, chunk.data(), chunk.size());
    if (read > 0) {
      pending_.append(chunk.data(), read);
      continue;
    }
    // Nothing more for now (not ready), or ever (the end, or an error that ends it as well).
    if (SDL_GetIOStatus(output) != SDL_IO_STATUS_NOT_READY) {
      outputEnded_ = true;
    }
    return outputEnded_;
  }
}

std::vector<std::string> ChildProcess::readLines() {
  const bool ended = drain();
  std::vector<std::string> lines;
  std::size_t start = 0;
  for (std::size_t end = pending_.find('\n'); end != std::string::npos;
       end = pending_.find('\n', start)) {
    std::string line = pending_.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(std::move(line));
    start = end + 1;
  }
  pending_.erase(0, start);
  if (ended && !pending_.empty()) {
    lines.push_back(std::exchange(pending_, {}));
  }
  return lines;
}

std::optional<int> ChildProcess::exitCode() {
  if (!exitCode_ && process_ && outputEnded_ && pending_.empty()) {
    int code = 0;
    if (SDL_WaitProcess(process_.get(), false, &code)) {
      exitCode_ = code;
    }
  }
  return exitCode_;
}

void ChildProcess::kill() {
  if (process_ && !exitCode_) {
    int code = 0;
    SDL_KillProcess(process_.get(), true);
    SDL_WaitProcess(process_.get(), true, &code);
    exitCode_ = code;
    outputEnded_ = true;
    pending_.clear();
  }
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
