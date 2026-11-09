#pragma once

#include <string>

namespace dwe::net {

// The certificate authorities a wss:// signaling server's certificate must chain up to, as PEM
// certificates. First the roots the game ships: those of every authority the deployed server's
// host issues its certificates from, so a system that lacks them, or Windows, whose store fills
// lazily, still connects. Then the system's own roots, so a server of one's own (--versus-server)
// with any public certificate works too: on Linux and macOS the system's bundle file, on Windows
// its "ROOT" store.
std::string trustedSignalRoots();

// Only the roots the game ships.
std::string shippedSignalRoots();

} // namespace dwe::net
