#include <doctest/doctest.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dwe::platform {

#ifdef _WIN32
// Every program of the project carries the manifest of cmake/windows/utf8.manifest, the tests too: without it char
// strings (argv, paths made from std::string) use the system's ANSI code page and a path outside it breaks.
TEST_CASE("on Windows a program's char strings are UTF-8") {
    CHECK(GetACP() == CP_UTF8);
}
#endif

} // namespace dwe::platform
