#pragma once

#include <dwe/core/error.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Process;

namespace dwe::platform {

// How a program that ran to its end finished, and what it wrote to its standard output.
struct ProcessResult {
  int exitCode = 0;
  std::string output;
};

// Runs a program and waits for it to end. arguments[0] is the program and the rest are its
// arguments, given to it as they are, never through a shell, all in UTF-8. Its standard output is
// captured; what it writes to its standard error goes to this program's, and it reads nothing. It
// does not need a Platform.
core::Result<ProcessResult> runProcess(std::span<const std::string> arguments);

// A program running next to this one, for a screen that shows what it says while it works: its
// standard output and standard error are read together, a line at a time, without ever waiting.
// Arguments are given as runProcess gives them. It reads nothing. The program is ended, and waited
// for, when the ChildProcess goes.
class ChildProcess {
public:
  static core::Result<ChildProcess> start(std::span<const std::string> arguments);

  ChildProcess(ChildProcess&& other) noexcept;
  ChildProcess& operator=(ChildProcess&& other) noexcept;
  ChildProcess(const ChildProcess&) = delete;
  ChildProcess& operator=(const ChildProcess&) = delete;
  ~ChildProcess();

  // The whole lines it wrote since the last call, without their line ends, and once it has ended,
  // its last line even without one. Never waits.
  std::vector<std::string> readLines();

  // Its exit code once it has ended and everything it wrote has been read; nothing before.
  [[nodiscard]] std::optional<int> exitCode();

  // Ends it now and waits for it; its exit code is then whatever the system gives a killed
  // program.
  void kill();

private:
  struct Deleter {
    void operator()(SDL_Process* process) const;
  };

  explicit ChildProcess(SDL_Process* process);
  // Reads what is there; true when the output has reached its end.
  bool drain();

  std::unique_ptr<SDL_Process, Deleter> process_;
  std::string pending_;
  bool outputEnded_ = false;
  std::optional<int> exitCode_;
};

// The file name of a program on this system: its name, with the extension the system gives programs
// (.exe on Windows).
[[nodiscard]] std::string executableFileName(std::string_view program);

// The folder that holds the running executable, with no separator at its end. It does not need a
// Platform.
core::Result<std::filesystem::path> executableFolder();

} // namespace dwe::platform
