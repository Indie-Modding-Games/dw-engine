#pragma once

#include <string_view>

// The error and log messages of script. Templates take their arguments in the order of their
// placeholders; the parts of a message that a Lua error raises are joined by raiseError, after the
// script's position.
namespace dwe::script::messages {

// The sandbox (script_vm.cpp, budget.cpp, print_to_log.cpp).
inline constexpr std::string_view RanTooLong = "the script ran too long and was stopped";
// Why it was stopped, after RanTooLong.
inline constexpr std::string_view RanOutOfInstructions =
    ": it ran all the instructions of its budget";
inline constexpr std::string_view ReachedTimeCeiling =
    ": it reached the time ceiling of its budget";
inline constexpr std::string_view ErrorNotText = "the error is not a string";
inline constexpr std::string_view GarbageOption = "collectgarbage('";
inline constexpr std::string_view GarbageOptionEnd = "') is not available to scripts";
// A C string: Lua takes it without a size.
inline constexpr const char* PathTooDeep = "the path is too deep";
inline constexpr std::string_view CannotOpenScript = "could not open the script {}";
inline constexpr std::string_view CallFailed = "{}: {}";
inline constexpr std::string_view Reloaded = "reloaded {}";
inline constexpr std::string_view ReloadFailed = "could not reload {}: {}";
inline constexpr std::string_view EngineWriter = "lua";
inline constexpr std::string_view ModWriter = "mod {}";
inline constexpr std::string_view PrintedLine = "{}: {}";
inline constexpr std::string_view ClippedText = "{} [{} more bytes]";
inline constexpr std::string_view PrintedTooMuch =
    "{}: printed more than {} lines in one call; the rest are left out";

// The log of each pack (pack_logs.cpp).
inline constexpr std::string_view LogFlooded =
    "mod {}: writes more than {} bytes per second to the log; its lines are left out until it writes less";
inline constexpr std::string_view LogLinesLeftOut = "mod {}: {} lines were left out of the log";
inline constexpr std::string_view ErrorRepeated = "mod {}: the last error repeated {} times";

// Tables that scripts cannot change (locked_tables.cpp). LockedMetatable is what getmetatable gives
// for them.
inline constexpr std::string_view LockedMetatable = "locked by the engine";
inline constexpr std::string_view ReadOnlyTable = "this table of the engine is read-only";
inline constexpr std::string_view FinalizerRefused =
    "setmetatable: scripts cannot use __gc, since finalizers run outside their calls and their budget";
inline constexpr std::string_view RawsetOnLockedTable =
    "rawset cannot change a table locked by the engine";
inline constexpr std::string_view UnknownGlobal = "unknown global '";
inline constexpr std::string_view UnknownGlobalEnd =
    "': a pack's scripts keep their variables in locals";
inline constexpr std::string_view NewGlobal = "cannot assign the global '";
inline constexpr std::string_view NewGlobalEnd = "': declare it local";
inline constexpr std::string_view GlobalWithoutName = "(a key that is not a text)";

// Lua's own errors of the pattern functions (lstrlib.c), with the same texts.
inline constexpr std::string_view PatternTooComplex = "pattern too complex";
inline constexpr std::string_view PatternEndsWithEscape = "malformed pattern (ends with '%')";
inline constexpr std::string_view PatternMissingBracket = "malformed pattern (missing ']')";
inline constexpr std::string_view PatternMissingBalanceArguments =
    "malformed pattern (missing arguments to '%b')";
inline constexpr std::string_view FrontierWithoutSet = "missing '[' after '%f' in pattern";
// A C string: Lua takes it without a size.
inline constexpr const char* TooManyCaptures = "too many captures";
inline constexpr std::string_view InvalidPatternCapture = "invalid pattern capture";
inline constexpr std::string_view InvalidCaptureIndex = "invalid capture index %";
inline constexpr std::string_view UnfinishedCapture = "unfinished capture";
inline constexpr std::string_view BadReplacementEscape = "invalid use of '%' in replacement string";
inline constexpr std::string_view BadReplacementValue = "invalid replacement value (a ";
inline constexpr std::string_view BadReplacementValueEnd = ")";
// A C string: Lua takes it without a size.
inline constexpr const char* ReplacementTypes = "string/function/table";

// string.rep (budgeted_strings.cpp), Lua's own text (lstrlib.c).
inline constexpr std::string_view ResultTooLarge = "resulting string too large";

// The table functions (budgeted_tables.cpp); the last two are Lua's own texts (ltablib.c).
inline constexpr std::string_view TableMoveTooLarge = "table.move: the range is too large";
inline constexpr std::string_view PositionOutOfBounds = "position out of bounds";
inline constexpr std::string_view WrongInsertArguments = "wrong number of arguments to 'insert'";

// Script assets (script_assets.cpp, script_vm.cpp, mod_environment.cpp).
inline constexpr std::string_view NoScriptAsset = "no pack has the script {}";
// A pack's math.random (pack_random.cpp). C strings: Lua takes them without a size.
inline constexpr const char* WrongRandomArguments = "wrong number of arguments";
inline constexpr const char* EmptyRandomInterval = "interval is empty";

inline constexpr std::string_view NotLuaScript = "{} is not a Lua script: {}";
inline constexpr std::string_view ScriptTooLarge =
    "the script {} has {} bytes; scripts may have at most {}";
inline constexpr std::string_view EngineScriptFromMod =
    "the engine script {} comes from the pack '{}' ({}, {} layer); only the engine's own packs of the base and "
    "remaster layers provide engine scripts, and a pack's scripts run in its own environment";
inline constexpr std::string_view EngineDocumentFromMod =
    "the document {} comes from the pack '{}' ({}, {} layer); its inline scripts and event attributes do not run, "
    "since only the engine's own packs of the base and remaster layers provide code that runs as the engine";
inline constexpr std::string_view NoCodeAsset = "no pack has {}";
inline constexpr std::string_view AssetOfAnotherPack =
    "the script {} comes from the pack '{}', not from '{}'";
inline constexpr std::string_view CannotMakeEnvironment =
    "cannot make the environment of the pack '{}': {}";
inline constexpr std::string_view ModReloaded = "reloaded the scripts of the pack '{}'";
inline constexpr std::string_view ModReloadFailed =
    "could not reload the scripts of the pack '{}', which keeps its previous ones: {}";

// The API (api_registry.cpp, api_functions.cpp).
inline constexpr std::string_view BadApiName =
    "'{}.{}' is not an API name: use 1 to 64 characters of a-z, 0-9 and _, starting with a letter";
inline constexpr std::string_view ReservedNamespace =
    "'{}' is reserved: no API namespace can take it";
inline constexpr std::string_view NamespaceTakesGlobal =
    "the API namespace '{}' would hide a global of the scripts";
inline constexpr std::string_view AlreadyPublished = "{}.{} is published already";
inline constexpr std::string_view CannotPublish = "cannot publish {}: {}";
inline constexpr std::string_view ApiFunctionErrorSeparator = ": ";
inline constexpr std::string_view BadApiArgument = ": argument ";
inline constexpr std::string_view BadApiArgumentType = " is a ";
inline constexpr std::string_view BadApiArgumentEnd =
    "; the API takes nil, booleans, numbers and texts";
inline constexpr std::string_view ApiArgumentMustBe = "argument {} must be {}";
inline constexpr std::string_view BooleanKind = "a boolean";
inline constexpr std::string_view IntegerKind = "an integer";
inline constexpr std::string_view NumberKind = "a number";
inline constexpr std::string_view TextKind = "a text";
inline constexpr std::string_view WaitOutsideStep =
    " waits, so only a section the engine runs can call it, outside any coroutine of its own";

// Coroutines the engine steps (script_coroutine.cpp).
inline constexpr std::string_view NoModuleFunction = "{} has no function at {}";
inline constexpr std::string_view YieldWithoutWait =
    "the coroutine yielded without a waiting function; only the engine's waiting functions may yield it";
inline constexpr std::string_view BadReturnValue =
    "the coroutine returned a {}; it may return nil, a boolean, a number or a text";
inline constexpr std::string_view CoroutineOver = "the coroutine already finished";
inline constexpr std::string_view ResumeOutOfMemory = "not enough memory to resume the coroutine";

// Hooks (hook_registry.cpp).
inline constexpr std::string_view BadEventName =
    "'{}' is not an event name: use a namespace and a name joined by a dot, such as field.map_entered";
inline constexpr std::string_view UndeclaredEvent = "the event {} is not declared";
inline constexpr std::string_view UnknownEvent = "there is no event '";
inline constexpr std::string_view UnknownEventEnd = "' to handle";
inline constexpr std::string_view HookFailed = "mod {}: a handler of {} failed: {}";
inline constexpr std::string_view HookBudgetSpent =
    "mod {}: its hook handlers spent their budget of this event or frame; the rest of the handlers of {} are skipped";
inline constexpr std::string_view TooManyHandlers =
    "a pack cannot register more handlers of the event ";

} // namespace dwe::script::messages
