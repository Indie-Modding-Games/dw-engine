#include <dwe/localization/text_case.hpp>

#include <doctest/doctest.h>

#include <string>

namespace {

using dwe::localization::lowerCase;
using dwe::localization::upperCase;

TEST_CASE("capitals of the Latin, Greek and Cyrillic alphabets") {
  CHECK(upperCase("press start") == "PRESS START");
  CHECK(upperCase("español, português, français") == "ESPAÑOL, PORTUGUÊS, FRANÇAIS");
  CHECK(upperCase("straße") == "STRASSE");
  CHECK(upperCase("łódź, čeština, ğ, ő") == "ŁÓDŹ, ČEŠTINA, Ğ, Ő");
  CHECK(upperCase("ÿ") == "Ÿ");
  CHECK(upperCase("русский, ёж, їжак") == "РУССКИЙ, ЁЖ, ЇЖАК");
  CHECK(upperCase("ελληνικά, ς") == "ΕΛΛΗΝΙΚΆ, Σ");
}

TEST_CASE("small letters are the capitals' mirror") {
  CHECK(lowerCase("PRESS START") == "press start");
  CHECK(lowerCase("ESPAÑOL, PORTUGUÊS") == "español, português");
  CHECK(lowerCase("ŁÓDŹ, ČEŠTINA") == "łódź, čeština");
  CHECK(lowerCase("РУССКИЙ, ЁЖ") == "русский, ёж");
  CHECK(lowerCase("Ÿ") == "ÿ");
}

TEST_CASE("characters without case and broken bytes stay as they are") {
  CHECK(upperCase("简体中文 日本語 한국어 ①") == "简体中文 日本語 한국어 ①");
  const std::string broken = std::string{"a\xff"} + "b\xc3";
  CHECK(upperCase(broken) == std::string{"A\xff"} + "B\xc3");
}

} // namespace
