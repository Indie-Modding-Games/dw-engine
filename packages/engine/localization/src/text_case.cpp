#include <dwe/localization/text_case.hpp>

#include <cstddef>
#include <cstdint>

namespace dwe::localization {
namespace {

struct Decoded {
  char32_t codepoint = 0;
  std::size_t length = 1;
  bool valid = false;
};

Decoded decode(std::string_view text, std::size_t at) {
  const auto lead = static_cast<std::uint8_t>(text[at]);
  std::size_t length = 0;
  char32_t codepoint = 0;
  if (lead < 0x80U) {
    return {.codepoint = lead, .length = 1, .valid = true};
  }
  if ((lead & 0xE0U) == 0xC0U) {
    length = 2;
    codepoint = lead & 0x1FU;
  } else if ((lead & 0xF0U) == 0xE0U) {
    length = 3;
    codepoint = lead & 0x0FU;
  } else if ((lead & 0xF8U) == 0xF0U) {
    length = 4;
    codepoint = lead & 0x07U;
  } else {
    return {};
  }
  if (at + length > text.size()) {
    return {};
  }
  for (std::size_t i = 1; i < length; ++i) {
    const auto next = static_cast<std::uint8_t>(text[at + i]);
    if ((next & 0xC0U) != 0x80U) {
      return {};
    }
    codepoint = (codepoint << 6U) | (next & 0x3FU);
  }
  return {.codepoint = codepoint, .length = length, .valid = true};
}

void encode(std::string& out, char32_t codepoint) {
  if (codepoint < 0x80U) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800U) {
    out.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
    out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
  } else if (codepoint < 0x10000U) {
    out.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
  }
}

bool latinExtendedPaired(char32_t c) {
  return (c >= 0x100U && c <= 0x137U && c != 0x131U) || (c >= 0x139U && c <= 0x148U) ||
         (c >= 0x14AU && c <= 0x177U) || (c >= 0x179U && c <= 0x17EU);
}

// From Ĺ to ň and from Ź to ž the capital sits on the odd code point.
bool latinExtendedCapital(char32_t c) {
  const bool oddCapitals = (c >= 0x139U && c <= 0x148U) || (c >= 0x179U && c <= 0x17EU);
  return ((c & 1U) == 1U) == oddCapitals;
}

char32_t toUpper(char32_t c) {
  if ((c >= U'a' && c <= U'z') || (c >= 0xE0U && c <= 0xFEU && c != 0xF7U)) {
    return c - 0x20U;
  }
  if (c == 0xFFU) {
    return 0x178U; // ÿ, whose capital is in Latin Extended-A
  }
  if (c == 0x131U) {
    return U'I'; // dotless ı
  }
  if (c == 0x17FU) {
    return U'S'; // long ſ
  }
  if (latinExtendedPaired(c)) {
    return latinExtendedCapital(c) ? c : c - 1U;
  }
  if (c == 0x3C2U) {
    return 0x3A3U; // final sigma
  }
  if (c >= 0x3B1U && c <= 0x3CBU) {
    return c - 0x20U;
  }
  if (c == 0x3ACU) {
    return 0x386U;
  }
  if (c >= 0x3ADU && c <= 0x3AFU) {
    return c - 0x25U;
  }
  if (c == 0x3CCU) {
    return 0x38CU;
  }
  if (c == 0x3CDU || c == 0x3CEU) {
    return c - 0x3FU;
  }
  if (c >= 0x430U && c <= 0x44FU) {
    return c - 0x20U;
  }
  if (c >= 0x450U && c <= 0x45FU) {
    return c - 0x50U;
  }
  if (((c >= 0x460U && c <= 0x481U) || (c >= 0x48AU && c <= 0x4BFU) ||
       (c >= 0x4D0U && c <= 0x52FU)) &&
      (c & 1U) == 1U) {
    return c - 1U;
  }
  if (c >= 0x4C1U && c <= 0x4CEU && (c & 1U) == 0U) {
    return c - 1U;
  }
  if (c == 0x4CFU) {
    return 0x4C0U;
  }
  return c;
}

char32_t toLower(char32_t c) {
  if ((c >= U'A' && c <= U'Z') || (c >= 0xC0U && c <= 0xDEU && c != 0xD7U)) {
    return c + 0x20U;
  }
  if (c == 0x178U) {
    return 0xFFU;
  }
  if (c == 0x130U) {
    return U'i'; // İ
  }
  if (latinExtendedPaired(c)) {
    return latinExtendedCapital(c) ? c + 1U : c;
  }
  if ((c >= 0x391U && c <= 0x3ABU && c != 0x3A2U)) {
    return c + 0x20U;
  }
  if (c == 0x386U) {
    return 0x3ACU;
  }
  if (c >= 0x388U && c <= 0x38AU) {
    return c + 0x25U;
  }
  if (c == 0x38CU) {
    return 0x3CCU;
  }
  if (c == 0x38EU || c == 0x38FU) {
    return c + 0x3FU;
  }
  if (c >= 0x410U && c <= 0x42FU) {
    return c + 0x20U;
  }
  if (c >= 0x400U && c <= 0x40FU) {
    return c + 0x50U;
  }
  if (((c >= 0x460U && c <= 0x481U) || (c >= 0x48AU && c <= 0x4BFU) ||
       (c >= 0x4D0U && c <= 0x52FU)) &&
      (c & 1U) == 0U) {
    return c + 1U;
  }
  if (c >= 0x4C1U && c <= 0x4CEU && (c & 1U) == 1U) {
    return c + 1U;
  }
  if (c == 0x4C0U) {
    return 0x4CFU;
  }
  return c;
}

template <typename Map>
std::string mapCase(std::string_view text, Map map, bool sharpS) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t at = 0; at < text.size();) {
    const Decoded decoded = decode(text, at);
    if (!decoded.valid) {
      out.push_back(text[at]);
    } else if (sharpS && decoded.codepoint == 0xDFU) {
      out += "SS";
    } else {
      encode(out, map(decoded.codepoint));
    }
    at += decoded.length;
  }
  return out;
}

} // namespace

std::string upperCase(std::string_view text) {
  return mapCase(text, toUpper, true);
}

std::string lowerCase(std::string_view text) {
  return mapCase(text, toLower, false);
}

} // namespace dwe::localization
