#pragma once

#include <string>

namespace dwe::platform {

// The language of the system's preferred locale as an ISO 639 code in lower case ("es" for es_ES),
// or empty when the system names none. It does not need a Platform.
std::string systemLanguage();

} // namespace dwe::platform
