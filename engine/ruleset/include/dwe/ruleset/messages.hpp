#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

namespace dwe::ruleset::messages {

using core::formatMessage;

// The error messages of the ruleset module, as std::format templates for core::formatMessage.

// Where a problem is.
inline constexpr std::string_view InFile = "{}: {}";
inline constexpr std::string_view AtNode = "{}: line {}, column {}: {}";
inline constexpr std::string_view AtPath = "{}.{}: {}";
inline constexpr std::string_view TunedValue = "{}.{} = {}: {}";

// Names and documents.
inline constexpr std::string_view BadName =
    "{} '{}' must be lowercase letters, digits and underscores";
inline constexpr std::string_view NotInPacks = "{} {} is in no pack";
inline constexpr std::string_view NoSection = "ruleset {} has no section '{}'";
inline constexpr std::string_view NoKey = "there is no '{}'";

// The manifest.
inline constexpr std::string_view RulesetIdText = "ruleset id";
inline constexpr std::string_view RulesetText = "ruleset";
inline constexpr std::string_view SectionText = "section";
inline constexpr std::string_view TableRoleText = "table role";
inline constexpr std::string_view ManifestText = "a ruleset manifest";
inline constexpr std::string_view WrongId = "the id must be '{}', the name of the ruleset's folder";
inline constexpr std::string_view BadExtends =
    "extends must name a ruleset: lowercase letters, digits and underscores";
inline constexpr std::string_view TablesNotMapping =
    "tables must be a mapping of a role to an asset id";
inline constexpr std::string_view BadTable = "table {}: {}";
inline constexpr std::string_view MissingTable = "table {}: {} is in no pack";
inline constexpr std::string_view BadSection =
    "section '{}' must be lowercase letters, digits and underscores, and not "
    "'{}'";
inline constexpr std::string_view RepeatedSection = "section '{}' is listed twice";
inline constexpr std::string_view BadScript =
    "script '{}' must be a .lua file of the ruleset's folder";
inline constexpr std::string_view MissingScript = "script '{}' is in no pack";
inline constexpr std::string_view ExtendsCycle = "ruleset {} extends itself through {}";
inline constexpr std::string_view ExtendsTooDeep =
    "ruleset {}: more than {} rulesets extend each other";

// The clock section.
inline constexpr std::string_view ClockSectionText = "the clock section";
inline constexpr std::string_view NotPositive = "{} must be more than zero";
inline constexpr std::string_view OutOfRange = "{} must be 0 to {}";

// Quirks.
inline constexpr std::string_view QuirksText = "quirks";

// Values and section files.
inline constexpr std::string_view NotOneLine = "the value does not fit on one line: {}";
inline constexpr std::string_view ShapeChanged =
    "{}: only values can be written back in place, not a change of shape";
inline constexpr std::string_view NotInPlace =
    "{}: the value at line {} is not on one line of the file";
inline constexpr std::string_view NotOnDisk =
    "{} is not a file on disk (an archived pack cannot be written)";
inline constexpr std::string_view WriteFailed = "could not write {}";
inline constexpr std::string_view RootPath = "(root)";

// Strategies.
inline constexpr std::string_view MissingStrategy = "{} is missing";
inline constexpr std::string_view StrategyNotName = "{} must name a {}";
inline constexpr std::string_view UnknownStrategy = "unknown {} '{}'; known: {}";
inline constexpr std::string_view NameSeparator = ", ";

} // namespace dwe::ruleset::messages
