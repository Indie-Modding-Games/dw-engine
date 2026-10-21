#pragma once

#include <cstdint>

namespace dwe::game::partner {

// What happened to the partner, for the UI and the session to react to. Each comes once, from Partner::takeEvents.
enum class EventKind : std::uint8_t {
    // A need or state started.
    BecameHungry,
    BecameSleepy,
    BecameTired,
    NeedsToilet,
    StartedSulking,
    GotSick,
    GotInjured,
    // The hunger window closed: value 1 if the partner ate enough before it did.
    HungerWindowClosed,
    // value: the CareMistakeCause.
    CareMistake,
    // value: the size of the poop (raise.yaml: poop_size). The field places it (PoopPiles).
    PoopedOnGround,
    UsedToilet,
    // The partner refused an offered item.
    Refused,
    // The partner slept and woke up at its wake hour; the session autosaves.
    Slept,
    // A new day started (midnight was crossed while playing, sleeping or training); value: the days crossed.
    NewDay,
    // value: the hours of the session.
    Trained,
    // A life was lost (to a lost battle or to an untreated sickness); value: the lives left. The original runs its
    // fainting script.
    LostLife,
    // The partner died; value 1 for a natural death (only that one passes stats on). The session plays the death
    // and calls Partner::rebirth.
    Died,
    // value: the index of the baby in the life section's rebirth babies.
    Reborn,
    // The partner became another species (changeSpecies: an evolution, the virus form or its way back).
    Evolved,
    // value: the change of the tamer's level (+1 or -1), already rolled (tamer.c: addTamerLevel).
    TamerLevelChanged,
    // The virus bar is full: the evolution system turns the partner into the virus form (tickConditions).
    VirusBarFull,
    // A rookie was scolded with no discipline and the lowest happiness (handlePraiseScold: NANIMON_TRIGGER).
    ScoldedAtRockBottom,
};

// Why a care mistake was counted.
enum class CareMistakeCause : std::uint8_t { HungerWindow, MissedSleep, PoopOnGround };

struct PartnerEvent {
    EventKind kind = EventKind::BecameHungry;
    std::int32_t value = 0;

    friend bool operator==(const PartnerEvent&, const PartnerEvent&) = default;
};

} // namespace dwe::game::partner
