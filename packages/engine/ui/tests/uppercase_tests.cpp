#include <dwe/ui/text_font.hpp>

#include <doctest/doctest.h>

namespace dwe::ui {

TEST_CASE("RmlUi's capitals get their accented letters in capitals too") {
  // RmlUi's text-transform: uppercase writes "Regañar" as "REGAñAR".
  CHECK(finishUppercase("REGAñAR") == "REGAÑAR");
  CHECK(finishUppercase("TéCNICAS") == "TÉCNICAS");
  CHECK(finishUppercase("CANCIóN Y MúSICA") == "CANCIÓN Y MÚSICA");
}

TEST_CASE("a text not written in capitals keeps its small letters") {
  CHECK(finishUppercase("Regañar") == "Regañar");
  CHECK(finishUppercase("Año 1") == "Año 1");
  CHECK(finishUppercase("día") == "día");
  CHECK(finishUppercase("¡ñ!") == "¡ñ!");
  CHECK(finishUppercase("HP 12") == "HP 12");
  CHECK(finishUppercase("") == "");
  // The division sign is no letter.
  CHECK(finishUppercase("A÷B") == "A÷B");
}

} // namespace dwe::ui
