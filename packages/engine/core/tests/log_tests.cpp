#include <dwe/core/log.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdio>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

int descriptorOf(std::FILE* stream) {
#ifdef _WIN32
  return _fileno(stream);
#else
  return fileno(stream);
#endif
}

int duplicate(int descriptor) {
#ifdef _WIN32
  return _dup(descriptor);
#else
  return dup(descriptor);
#endif
}

void redirect(int from, int to) {
#ifdef _WIN32
  _dup2(from, to);
#else
  dup2(from, to);
#endif
}

void closeDescriptor(int descriptor) {
#ifdef _WIN32
  _close(descriptor);
#else
  close(descriptor);
#endif
}

} // namespace

TEST_CASE("a log line reaches a redirected stderr at once, though the stream buffers it fully") {
  std::FILE* file = std::tmpfile();
  REQUIRE(file != nullptr);
  std::fflush(stderr);
  const int saved = duplicate(descriptorOf(stderr));
  REQUIRE(saved >= 0);
  redirect(descriptorOf(file), descriptorOf(stderr));
  std::array<char, 4096> buffer{};
  std::setvbuf(stderr, buffer.data(), _IOFBF, buffer.size());

  dwe::core::logInfo("written before a hang");
  std::string written(64, '\0');
  std::fseek(file, 0, SEEK_SET);
  written.resize(std::fread(written.data(), 1, written.size(), file));

  std::fflush(stderr);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  redirect(saved, descriptorOf(stderr));
  closeDescriptor(saved);
  std::fclose(file);
  CHECK(written.starts_with("[info] written before a hang"));
}
