#include <dwe/screens/lives_box.hpp>

#include <doctest/doctest.h>

#include <string_view>
#include <variant>

namespace {

using dwe::screens::LivesBox;

template <typename T>
T value(const LivesBox& box, std::string_view name) {
  for (const dwe::screens::ScreenSetting& setting : box.settings()) {
    if (setting.name == name) {
      return std::get<T>(setting.value);
    }
  }
  FAIL("no setting ", name);
  return {};
}

void steps(LivesBox& box, int count) {
  for (int i = 0; i < count; ++i) {
    box.step();
  }
}

} // namespace

TEST_CASE("lives box: the outline grows from the partner for 5 frames, then the box shows") {
  LivesBox box;
  box.open({.x = 40.0, .y = 60.0}, 1, 3);
  CHECK(box.takeSound() == dwe::screens::BoxOpenSound);
  CHECK(box.shown());
  CHECK(value<bool>(box, dwe::screens::LivesBoxOutlineSetting));
  CHECK(value<double>(box, dwe::screens::LivesBoxGrowSetting) == doctest::Approx(0.0));
  CHECK(value<double>(box, dwe::screens::LivesBoxAnchorXSetting) == doctest::Approx(40.0));
  steps(box, 4);
  CHECK(value<bool>(box, dwe::screens::LivesBoxOutlineSetting));
  CHECK(value<double>(box, dwe::screens::LivesBoxGrowSetting) == doctest::Approx(1.0));
  box.step();
  CHECK_FALSE(value<bool>(box, dwe::screens::LivesBoxOutlineSetting));
  CHECK(value<double>(box, dwe::screens::LivesBoxLivesSetting) == doctest::Approx(1.0));
  CHECK(value<double>(box, dwe::screens::LivesBoxSlotsSetting) == doctest::Approx(3.0));
}

TEST_CASE("lives box: the life just lost shows full, blinks, then shows empty") {
  LivesBox box;
  box.open({}, 2, 3);
  steps(box, 5);
  CHECK(value<bool>(box, dwe::screens::LivesBoxLostFullSetting));
  steps(box, 9);
  CHECK(value<bool>(box, dwe::screens::LivesBoxLostFullSetting));
  // MURD_renderLivesBox: from frame 0xa it blinks in pairs of frames, empty on 0xa and 0xb, full on
  // 0xc and 0xd...
  box.step();
  CHECK_FALSE(value<bool>(box, dwe::screens::LivesBoxLostFullSetting));
  steps(box, 2);
  CHECK(value<bool>(box, dwe::screens::LivesBoxLostFullSetting));
  steps(box, 2);
  CHECK_FALSE(value<bool>(box, dwe::screens::LivesBoxLostFullSetting));
  // From 0x1e it stays empty.
  steps(box, 0x1e - 0xe);
  CHECK_FALSE(value<bool>(box, dwe::screens::LivesBoxLostFullSetting));
  steps(box, 10);
  CHECK_FALSE(value<bool>(box, dwe::screens::LivesBoxLostFullSetting));
}

TEST_CASE("lives box: closing shrinks the outline back for 5 frames with the close sound") {
  LivesBox box;
  box.open({}, 0, 3);
  steps(box, 10);
  static_cast<void>(box.takeSound());
  box.close({.x = 10.0, .y = 20.0});
  CHECK(box.closing());
  CHECK(box.takeSound() == dwe::screens::BoxCloseSound);
  CHECK(value<bool>(box, dwe::screens::LivesBoxOutlineSetting));
  CHECK(value<double>(box, dwe::screens::LivesBoxGrowSetting) == doctest::Approx(1.0));
  steps(box, 4);
  CHECK(box.shown());
  CHECK(value<double>(box, dwe::screens::LivesBoxGrowSetting) == doctest::Approx(0.0));
  box.step();
  CHECK_FALSE(box.shown());
  CHECK_FALSE(box.takeSound().has_value());
}

TEST_CASE("lives box: removing takes it away at once") {
  LivesBox box;
  box.open({}, 1, 3);
  steps(box, 7);
  box.remove();
  CHECK_FALSE(box.shown());
  CHECK_FALSE(value<bool>(box, dwe::screens::LivesBoxShownSetting));
}
