#pragma once

#include <string>
#include <string_view>

namespace dwe::script::detail {

// Whether a name can name an API namespace, function or event part: 1 to 64 characters of a-z, 0-9
// and _, starting with a letter.
bool isApiName(std::string_view name);

// Whether a name can name a published value: an API name, or the same in upper case (ENDED).
bool isApiValueName(std::string_view name);

// Whether an event is a namespace and a name joined by a dot, such as field.map_entered.
bool isEventName(std::string_view event);

// apiNamespace.name, as scripts call a function and errors name it.
std::string qualifiedApiName(std::string_view apiNamespace, std::string_view name);

} // namespace dwe::script::detail
