#include <dwe/game/battle/text_keys.hpp>

namespace dwe::game::battle {

std::string_view commandTextKey(Command command) {
    switch (command) {
    case Command::None:
        break;
    case Command::Flee:
        return "battle.command.flee";
    case Command::Attack:
        return "battle.command.attack";
    case Command::YourCall:
        return "battle.command.your_call";
    case Command::Moderate:
        return "battle.command.moderate";
    case Command::Distance:
        return "battle.command.distance";
    case Command::Defend:
        return "battle.command.defend";
    case Command::Change:
        return "battle.command.change";
    case Command::Technique1:
        return "battle.command.technique_1";
    case Command::Technique2:
        return "battle.command.technique_2";
    case Command::Technique3:
        return "battle.command.technique_3";
    case Command::Finisher:
        return "battle.command.finisher";
    }
    return "battle.command.none";
}

std::string_view outcomeTextKey(Outcome outcome) {
    switch (outcome) {
    case Outcome::Victory:
        return "battle.outcome.victory";
    case Outcome::Fled:
        return "battle.outcome.fled";
    case Outcome::Defeat:
        break;
    }
    return "battle.outcome.defeat";
}

std::string_view statusTextKey(StatusEffect status) {
    switch (status) {
    case StatusEffect::Poison:
        return "battle.status.poison";
    case StatusEffect::Confusion:
        return "battle.status.confusion";
    case StatusEffect::Stun:
        return "battle.status.stun";
    case StatusEffect::Flatten:
        return "battle.status.flatten";
    case StatusEffect::None:
        break;
    }
    return "battle.status.none";
}

} // namespace dwe::game::battle
