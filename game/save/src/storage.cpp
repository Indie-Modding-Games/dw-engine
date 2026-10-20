#include <dwe/assets/file_system.hpp>
#include <dwe/game/save/storage.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <system_error>
#include <utility>

#include "messages.hpp"

namespace dwe::game::save {

namespace {

core::Result<void> checkName(std::string_view name) {
    if (!isValidFileName(name)) {
        return core::fail(std::format(messages::BadFileName, name));
    }
    return {};
}

core::Result<void> writeWhole(const std::filesystem::path& file, std::string_view text) {
    std::ofstream stream{file, std::ios::binary | std::ios::trunc};
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    if (!stream) {
        return core::fail(std::format(messages::CannotWrite, assets::pathText(file)));
    }
    return {};
}

// Copies a file to its backup, if it exists. A copy and not a rename, so the file is never missing.
core::Result<void> backUp(const std::filesystem::path& file, const std::filesystem::path& backup) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        return {};
    }
    std::filesystem::copy_file(file, backup, std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        return core::fail(std::format(messages::CannotBackUp, assets::pathText(file), error.message()));
    }
    return {};
}

core::Result<void> replace(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code error;
    std::filesystem::rename(from, to, error);
    if (error) {
        return core::fail(std::format(messages::CannotReplace, assets::pathText(to), error.message()));
    }
    return {};
}

} // namespace

bool isValidFileName(std::string_view name) {
    return !name.empty() && name.size() <= MaxFileNameLength && !name.starts_with('.') &&
           std::ranges::all_of(
               name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.'; });
}

DiskSaveStorage::DiskSaveStorage(std::filesystem::path folder) : folder_{std::move(folder)} {}

core::Result<std::optional<std::string>> DiskSaveStorage::read(std::string_view name) const {
    if (auto valid = checkName(name); !valid) {
        return std::unexpected{valid.error()};
    }
    const std::filesystem::path file = folder_ / name;
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        if (error) {
            return core::fail(std::format(messages::CannotOpen, assets::pathText(file), error.message()));
        }
        return std::optional<std::string>{};
    }
    auto bytes = assets::DiskFileSystem{}.readFile(file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    return std::optional<std::string>{std::in_place, reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

core::Result<void> DiskSaveStorage::write(std::string_view name, std::string_view text, std::string_view backup) {
    if (auto valid = checkName(name).and_then([&] { return checkName(backup); }); !valid) {
        return valid;
    }
    std::error_code error;
    std::filesystem::create_directories(folder_, error);
    if (error) {
        return core::fail(std::format(messages::CannotCreateFolder, assets::pathText(folder_), error.message()));
    }
    const std::filesystem::path file = folder_ / name;
    const std::filesystem::path temporary = folder_ / std::format("{}{}", name, TemporarySuffix);
    auto written = writeWhole(temporary, text).and_then([&] { return backUp(file, folder_ / backup); }).and_then([&] {
        return replace(temporary, file);
    });
    if (!written) {
        std::filesystem::remove(temporary, error);
    }
    return written;
}

core::Result<std::optional<std::string>> MemorySaveStorage::read(std::string_view name) const {
    if (auto valid = checkName(name); !valid) {
        return std::unexpected{valid.error()};
    }
    const auto found = files_.find(name);
    if (found == files_.end()) {
        return std::optional<std::string>{};
    }
    return std::optional<std::string>{found->second};
}

core::Result<void> MemorySaveStorage::write(std::string_view name, std::string_view text, std::string_view backup) {
    if (auto valid = checkName(name).and_then([&] { return checkName(backup); }); !valid) {
        return valid;
    }
    if (failNextWrite_) {
        failNextWrite_ = false;
        return core::fail(std::format(messages::StorageFull, name));
    }
    const auto found = files_.find(name);
    if (found != files_.end()) {
        files_.insert_or_assign(std::string{backup}, found->second);
    }
    files_.insert_or_assign(std::string{name}, std::string{text});
    return {};
}

void MemorySaveStorage::put(std::string name, std::string text) {
    files_.insert_or_assign(std::move(name), std::move(text));
}

} // namespace dwe::game::save
