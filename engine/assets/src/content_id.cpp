#include <dwe/assets/content_id.hpp>
#include <dwe/assets/messages.hpp>
#include <dwe/assets/pack_manifest.hpp>

#include <algorithm>
#include <format>

namespace dwe::assets {

namespace {

constexpr char AssetPathSeparator = '/';

bool isNameCharacter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

core::Result<void> checkName(std::string_view id, std::string_view name) {
    if (name.empty() || !std::ranges::all_of(name, isNameCharacter)) {
        return core::fail(messages::formatMessage(messages::ContentIdCharacters, id));
    }
    if (name.size() > MaxContentNameLength) {
        return core::fail(messages::formatMessage(messages::ContentIdTooLong, id, MaxContentNameLength));
    }
    return {};
}

} // namespace

core::Result<ContentId> ContentId::parse(std::string_view text) {
    if (text.empty()) {
        return core::fail(std::string{messages::ContentIdEmpty});
    }
    const std::size_t separator = text.find(ContentNamespaceSeparator);
    if (separator == std::string_view::npos) {
        if (auto checked = checkName(text, text); !checked) {
            return std::unexpected{checked.error()};
        }
        return ContentId{std::string{text}, std::string::npos};
    }
    if (auto checked = checkPackId(text.substr(0, separator)); !checked) {
        return core::fail(messages::formatMessage(messages::ContentIdNamespace, text, checked.error().message));
    }
    if (auto checked = checkName(text, text.substr(separator + 1)); !checked) {
        return std::unexpected{checked.error()};
    }
    return ContentId{std::string{text}, separator};
}

std::string_view ContentId::packNamespace() const {
    return isNamespaced() ? std::string_view{text_}.substr(0, separator_) : std::string_view{};
}

std::string_view ContentId::name() const {
    return isNamespaced() ? std::string_view{text_}.substr(separator_ + 1) : std::string_view{text_};
}

std::string ContentId::assetPath() const {
    if (!isNamespaced()) {
        return text_;
    }
    return std::format("{}{}{}", packNamespace(), AssetPathSeparator, name());
}

std::string contentAssetPath(std::string_view id) {
    auto parsed = ContentId::parse(id);
    return parsed ? parsed->assetPath() : std::string{id};
}

} // namespace dwe::assets
