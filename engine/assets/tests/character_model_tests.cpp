#include <dwe/assets/character_model.hpp>
#include <dwe/assets/model_data.hpp>

#include <doctest/doctest.h>

#include <array>
#include <string>

using dwe::assets::parseCharacterModel;

TEST_CASE("a character model maps animation names to clip ids") {
    const auto character = parseCharacterModel("model: digimon/boys/model\nanimations:\n  idle: 0\n  walk: 2\n");
    REQUIRE_MESSAGE(character.has_value(), (character ? "" : character.error().message));
    CHECK(character->model.str() == "digimon/boys/model");
    CHECK(character->clipOf("idle") == 0U);
    CHECK(character->clipOf("walk") == 2U);
    CHECK_FALSE(character->clipOf("run").has_value());
}

TEST_CASE("a character model that breaks its schema is refused with the place") {
    const std::array broken{
        "animations:\n  idle: 0\n",
        "model: digimon/boys/model\n",
        "model: Digimon/Boys\nanimations:\n  idle: 0\n",
        "model: digimon/boys/model\nanimations:\n  Idle: 0\n",
        "model: digimon/boys/model\nanimations:\n  idle: 4294967296\n",
        "model: digimon/boys/model\nanimations:\n  idle: -1\n",
        "model: digimon/boys/model\nanimations: {}\n",
        "model: digimon/boys/model\nanimations:\n  idle: 0\nspeed: 2\n",
    };
    for (const char* yaml : broken) {
        CAPTURE(yaml);
        const auto character = parseCharacterModel(yaml);
        REQUIRE_FALSE(character.has_value());
        CHECK(character.error().message.find("line") != std::string::npos);
    }
}

TEST_CASE("a clip id may be any 32-bit number") {
    const auto character = parseCharacterModel("model: digimon/boys/model\nanimations:\n  idle: 4294967295\n");
    REQUIRE(character.has_value());
    CHECK(character->clipOf("idle") == 4294967295U);
}

TEST_CASE("checkCharacterModel wants a clip for every animation") {
    const auto character = parseCharacterModel("model: digimon/boys/model\nanimations:\n  idle: 0\n  run: 3\n");
    REQUIRE(character.has_value());
    dwe::assets::ModelData model;
    model.clips.resize(1);
    model.clips[0].id = 0;
    CHECK_FALSE(checkCharacterModel(*character, model).has_value());
    model.clips.resize(2);
    model.clips[1].id = 3;
    CHECK(checkCharacterModel(*character, model).has_value());
}
