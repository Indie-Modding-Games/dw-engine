#pragma once

#include <string_view>

// The messages of dwx. Templates take their arguments in the order of their placeholders.
namespace dwe::dwx::messages {

// dwx validate: a translation that differs from the reference language (assets::checkTranslations).
inline constexpr std::string_view TranslationProblem = "the translation {} {}";

} // namespace dwe::dwx::messages
