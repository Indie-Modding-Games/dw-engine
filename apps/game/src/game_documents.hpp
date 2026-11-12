#pragma once

#include <array>
#include <string_view>

namespace dwe::app {

// A document of the game pack the game loads at start: the name the screens show it by and its
// asset.
struct GameDocument {
  std::string_view name;
  std::string_view asset;
};

// Every document under the game pack's ui/ (a test checks none is missing), except
// ui/dev_assets, which DevAssetsBanner loads itself.
inline constexpr std::array GameDocuments{
    GameDocument{.name = "hud", .asset = "ui/hud/document"},
    GameDocument{.name = "story_textbox", .asset = "ui/story_textbox/document"},
    GameDocument{.name = "pause_menu", .asset = "ui/pause_menu/document"},
    GameDocument{.name = "pause_box", .asset = "ui/pause_box/document"},
    GameDocument{.name = "main_menu", .asset = "ui/main_menu/document"},
    GameDocument{.name = "battle", .asset = "ui/battle/document"},
    GameDocument{.name = "partner_menu", .asset = "ui/partner_menu/document"},
    GameDocument{.name = "destination_choice", .asset = "ui/destination_choice/document"},
    GameDocument{.name = "save_slots", .asset = "ui/save_slots/document"},
    GameDocument{.name = "dialog", .asset = "ui/dialog/document"},
    GameDocument{.name = "new_game_rules", .asset = "ui/new_game_rules/document"},
    GameDocument{.name = "new_game_mode", .asset = "ui/new_game_mode/document"},
    GameDocument{.name = "skin_picker", .asset = "ui/skin_picker/document"},
    GameDocument{.name = "sleep_menu", .asset = "ui/sleep_menu/document"},
    GameDocument{.name = "save_machine", .asset = "ui/save_machine/document"},
    GameDocument{.name = "gym_stats", .asset = "ui/gym_stats/document"},
    GameDocument{.name = "shop_menu", .asset = "ui/shop_menu/document"},
    GameDocument{.name = "dev_menu", .asset = "ui/dev_menu/document"},
    GameDocument{.name = "item_keeper", .asset = "ui/item_keeper/document"},
    GameDocument{.name = "discard_menu", .asset = "ui/discard_menu/document"},
    GameDocument{.name = "jukebox", .asset = "ui/jukebox/document"},
    GameDocument{.name = "versus", .asset = "ui/versus/document"},
    GameDocument{.name = "tournament_schedule", .asset = "ui/tournament_schedule/document"},
    GameDocument{.name = "tournament_bracket", .asset = "ui/tournament_bracket/document"},
    GameDocument{.name = "tournament_match", .asset = "ui/tournament_match/document"},
    GameDocument{.name = "trade_menu", .asset = "ui/trade_menu/document"},
    GameDocument{.name = "bird_transport", .asset = "ui/bird_transport/document"},
    GameDocument{.name = "money_box", .asset = "ui/money_box/document"},
    GameDocument{.name = "card_shop", .asset = "ui/card_shop/document"},
    GameDocument{.name = "merit_shop", .asset = "ui/merit_shop/document"},
    GameDocument{.name = "field_item_notice", .asset = "ui/field_item_notice/document"},
    GameDocument{.name = "field_numbers", .asset = "ui/field_numbers/document"},
    GameDocument{.name = "award_notice", .asset = "ui/award_notice/document"},
    GameDocument{.name = "gym_slots", .asset = "ui/gym_slots/document"},
    GameDocument{.name = "naming_box", .asset = "ui/naming_box/document"},
    GameDocument{.name = "egg_box", .asset = "ui/egg_box/document"},
    GameDocument{.name = "lives_box", .asset = "ui/lives_box/document"},
    GameDocument{.name = "fishing_bait", .asset = "ui/fishing_bait/document"},
    GameDocument{.name = "fishing_notice", .asset = "ui/fishing_notice/document"},
    GameDocument{.name = "fishing_hud", .asset = "ui/fishing_hud/document"},
    GameDocument{.name = "curling_hud", .asset = "ui/curling_hud/document"},
    GameDocument{.name = "bug_report", .asset = "ui/bug_report/document"},
    GameDocument{.name = "save_indicator", .asset = "ui/save_indicator/document"},
    GameDocument{.name = "world_map", .asset = "ui/world_map/document"},
};

} // namespace dwe::app
