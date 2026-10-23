#include <dwe/core/log.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_map_edits.hpp>

#include <array>
#include <cstdint>

#include "story_game_context.hpp"

// The story's orders that change the map the field shows (script_interp.c: scriptInstruction46to58
// SET_OBJ_VISIBILITY, scriptInstruction64to7E SET_RECT_IMPASSABLE). They wait in the context until
// the field takes them (StoryGameOrders::takeMapEdits).
namespace dwe::session {

namespace {

using script::ApiValue;

// setMapObjectsFlag and setImpassableRect take their operands as signed bytes (int8_t).
std::int32_t signedByte(std::int64_t value) {
  return static_cast<std::int8_t>(static_cast<std::uint8_t>(value));
}

void keep(StoryGameContext& context, StoryMapEdit edit) {
  // Without a field there is no map to change; the next map loads its own objects and collision.
  if (!context.game().currentPlace()) {
    core::logDebug("[port:story] a map edit with no field shown is dropped");
    return;
  }
  context.mapEdits.push_back(edit);
}

core::Result<ApiValue> setObjectFlag(StoryGameContext& context, std::int64_t object,
                                     std::int64_t flag) {
  const StoryObjectFlag edit{.object = static_cast<std::int32_t>(object), .flag = signedByte(flag)};
  core::logDebug("[port:story] set_obj_visibility (script_interp.c:1530, overworld.c:930): "
                 "object {} flag {}",
                 edit.object, edit.flag);
  keep(context, edit);
  return ApiValue{};
}

core::Result<ApiValue> blockRect(StoryGameContext& context, std::int64_t column, std::int64_t row,
                                 std::int64_t width, std::int64_t height) {
  const StoryBlockedRect edit{.column = signedByte(column),
                              .row = signedByte(row),
                              .width = signedByte(width),
                              .height = signedByte(height)};
  core::logDebug("[port:story] set_rect_impassable (script_interp.c:2158, map_collision.c:190): "
                 "{} x {} cells from ({}, {})",
                 edit.width, edit.height, edit.column, edit.row);
  keep(context, edit);
  return ApiValue{};
}

} // namespace

core::Result<void> publishMapOrders(script::ApiRegistry& api, StoryGameContext& context) {
  namespace names = story_game_api;
  StoryGameContext* game = &context;
  using Operands2 = std::array<std::int64_t, 2>;
  using Operands5 = std::array<std::int64_t, 5>;
  return publishOrders(
      api, {{names::SetObjVisibility, integerOrder<2>([game](const Operands2& operands) {
               return setObjectFlag(*game, operands[0], operands[1]);
             })},
            // The first operand is read and not used (script_interp.c:2159).
            {names::SetRectImpassable, integerOrder<5>([game](const Operands5& operands) {
               return blockRect(*game, operands[1], operands[2], operands[3], operands[4]);
             })}});
}

} // namespace dwe::session
