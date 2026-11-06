// A program for the process tests: writes each argument after the first on its own line to its
// standard output, a line to its standard error, and ends with the code its first argument gives (0
// without one).

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
  // In text mode the CRT writes each \n as \r\n; the tests compare the output byte for byte.
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  const std::span arguments{argv, static_cast<std::size_t>(argc)};
  int exitCode = 0;
  if (arguments.size() > 1) {
    const std::string_view code{arguments[1]};
    std::from_chars(code.data(), code.data() + code.size(), exitCode);
  }
  for (const char* argument : arguments.subspan(std::min<std::size_t>(arguments.size(), 2))) {
    std::puts(argument);
  }
  std::fputs("process probe: done\n", stderr);
  return exitCode;
}
