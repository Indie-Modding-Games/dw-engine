#pragma once

#include <filesystem>

#include "options.hpp"

namespace dwe::app {

// Whether the options ask for a development view instead of the field: the test scene, the model
// viewer or the map preview.
bool wantsDevView(const Options& options);

// Runs a development view with the debug overlay until the window closes.
int runDevView(const Options& options);

// Mounts the packs of a content folder and opens every asset the stack provides with the engine's
// readers. Prints one line per asset that fails; exits with success only when there is none.
int checkContentFolder(const std::filesystem::path& folder);

} // namespace dwe::app
