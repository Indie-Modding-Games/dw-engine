#pragma once

#include <dwe/core/error.hpp>

#include <filesystem>

#include "options.hpp"

namespace dwe::app {

// The folder of the save files: --saves, or `saves` in the user's data folder.
core::Result<std::filesystem::path> savesFolder(const Options& options);

// Logs every save slot (empty, its header, or why it cannot be read) with the save settings of the mounted packs,
// and returns the exit code.
int listSaves(const Options& options);

} // namespace dwe::app
