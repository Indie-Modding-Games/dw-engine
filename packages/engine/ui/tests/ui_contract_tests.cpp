#include <dwe/ui/ui_contract.hpp>

#include <doctest/doctest.h>

#include <array>
#include <string>

namespace dwe::ui {
namespace {

constexpr auto Contract = std::to_array<UiContractEntry>({
    {.screen = "hud",
     .kind = UiContractKind::Setting,
     .name = "hud.meter.<n>.value",
     .value = UiValueKind::Number,
     .summary = "A meter's \"value\"."},
    {.screen = "hud",
     .kind = UiContractKind::Command,
     .name = "open_menu",
     .value = UiValueKind::None,
     .summary = "Opens the menu."},
    {.screen = "pause_menu",
     .kind = UiContractKind::Setting,
     .name = "evoreq.req.<id>",
     .value = UiValueKind::Text,
     .summary = "A requirement."},
});

} // namespace

TEST_CASE("a contract name's placeholders stand for one part each") {
  CHECK(matchesContractName("hud.meter.<n>.value", "hud.meter.12.value"));
  CHECK_FALSE(matchesContractName("hud.meter.<n>.value", "hud.meter.x.value"));
  CHECK_FALSE(matchesContractName("hud.meter.<n>.value", "hud.meter..value"));
  CHECK_FALSE(matchesContractName("hud.meter.<n>.value", "hud.meter.1.2.value"));
  CHECK(matchesContractName("evoreq.req.<id>", "evoreq.req.agumon_2"));
  CHECK_FALSE(matchesContractName("evoreq.req.<id>", "evoreq.req.Agumon"));
  CHECK_FALSE(matchesContractName("evoreq.req.<id>", "evoreq.req"));
  CHECK(matchesContractName("open_menu", "open_menu"));
  CHECK_FALSE(matchesContractName("open_menu", "open_menu.now"));
}

TEST_CASE("an entry is found by its screen, its kind and a name it covers") {
  CHECK(findContractEntry(Contract, "hud", UiContractKind::Setting, "hud.meter.0.value") ==
        Contract.data());
  CHECK(findContractEntry(Contract, "hud", UiContractKind::Command, "hud.meter.0.value") ==
        nullptr);
  CHECK(findContractEntry(Contract, "pause_menu", UiContractKind::Command, "open_menu") == nullptr);
}

TEST_CASE("the reference lists each screen's entries under the API's version") {
  const std::string markdown = contractMarkdown(Contract);
  CHECK(markdown.contains("**0.1.0**"));
  CHECK(markdown.contains("### `hud`"));
  CHECK(markdown.contains("| `open_menu` | comando | — | Opens the menu. |"));
  CHECK(markdown.find("### `hud`") < markdown.find("### `pause_menu`"));

  const std::string json = contractJson(Contract);
  CHECK(json.contains(R"("version": "0.1.0")"));
  CHECK(json.contains(R"("summary": "A meter's \"value\".")"));
  CHECK(json.contains(R"("kind": "command")"));
}

} // namespace dwe::ui
