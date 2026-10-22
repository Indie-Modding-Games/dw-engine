#include <dwe/platform/locale.hpp>

#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <span>

namespace dwe::platform {

std::string systemLanguage() {
  int count = 0;
  SDL_Locale** locales = SDL_GetPreferredLocales(&count);
  if (locales == nullptr) {
    return {};
  }
  const std::span<SDL_Locale* const> preferred{locales,
                                               static_cast<std::size_t>(std::max(count, 0))};
  std::string language = !preferred.empty() && preferred.front()->language != nullptr
                             ? std::string{preferred.front()->language}
                             : "";
  SDL_free(static_cast<void*>(locales));
  std::ranges::transform(language, language.begin(), [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return language;
}

} // namespace dwe::platform
