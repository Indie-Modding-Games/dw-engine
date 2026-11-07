#pragma once

#include <dwe/core/error.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/script/budget_stop.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

// NOLINTNEXTLINE(readability-identifier-naming): a third-party type, named by its library.
struct lua_State;

namespace dwe::script {

namespace detail {
struct VmState;
}

struct CoroutineWaiting {
  ApiValue value;
  // Lua functions between the coroutine's own function and the caller of the waiting function: 0
  // when the coroutine's function called it. Tail calls, metamethods and for iterators add nothing.
  // The story host takes it as the link depth of a jump to another script, whose caller then waits
  // under it.
  std::uint32_t callDepth = 0;
};

struct CoroutineFinished {
  ApiValue value;
};

struct CoroutineFailed {
  std::string message;
  BudgetStop stop = BudgetStop::None;
};

using CoroutineStep = std::variant<CoroutineWaiting, CoroutineFinished, CoroutineFailed>;

class ScriptCoroutine;
class ScriptModule;

// A function of a module, found by the same raw walk as ScriptModule::coroutine.
struct ModuleFunction {
  const ScriptModule* module = nullptr;
  std::span<const ApiValue> path;
};

// What a pack's script asset returned, kept alive for the coroutines the engine steps. It must not
// outlive the VM.
class ScriptModule {
public:
  ScriptModule(detail::VmState& vm, std::size_t mod, int reference, std::string name);
  ScriptModule(const ScriptModule&) = delete;
  ScriptModule& operator=(const ScriptModule&) = delete;
  ScriptModule(ScriptModule&& other) noexcept;
  ScriptModule& operator=(ScriptModule&& other) noexcept;
  ~ScriptModule();

  [[nodiscard]] const std::string& name() const { return name_; }

  // path walks tables from the module by raw reads, so a module's metatables never run here.
  [[nodiscard]] core::Result<ScriptCoroutine> coroutine(std::span<const ApiValue> path) const;
  // The same, and the coroutine's function gets `argument` as its first argument: a function of
  // another module of the same VM, or nil when there is none at its path. The function keeps the
  // environment of its own module; this module's pack pays for the whole coroutine. A story patch
  // gets so the section it wraps.
  [[nodiscard]] core::Result<ScriptCoroutine> coroutine(std::span<const ApiValue> path,
                                                        ModuleFunction argument) const;

  // The value at path, read by raw reads like coroutine(): nil when a step is missing or not a
  // table. A value that is not nil, a boolean, a number or a text is an error.
  [[nodiscard]] core::Result<ApiValue> value(std::span<const ApiValue> path) const;
  // The integer keys of the table at path whose values are functions, sorted; none when path is no
  // table.
  [[nodiscard]] core::Result<std::vector<std::int64_t>>
  functionKeys(std::span<const ApiValue> path) const;

private:
  friend class ScriptCoroutine;

  void release();

  detail::VmState* vm_;
  std::size_t mod_;
  int reference_;
  std::string name_;
};

// Each resume is an engine call with ScriptLimits' resume budget, paid by the module's pack.
// Destroying a suspended coroutine closes it, which runs its pending to-be-closed variables under
// that budget. It must not outlive the VM.
class ScriptCoroutine {
public:
  // `arguments`: how many values the thread holds over its function for the first resume.
  ScriptCoroutine(detail::VmState& vm, std::size_t mod, int reference, lua_State* thread,
                  int arguments = 0);
  ScriptCoroutine(const ScriptCoroutine&) = delete;
  ScriptCoroutine& operator=(const ScriptCoroutine&) = delete;
  ScriptCoroutine(ScriptCoroutine&& other) noexcept;
  ScriptCoroutine& operator=(ScriptCoroutine&& other) noexcept;
  ~ScriptCoroutine();

  // The first resume calls the function with the arguments it was made with (none, or a module's
  // function) and ignores value.
  CoroutineStep resume(const ApiValue& value = {});

  [[nodiscard]] bool finished() const { return thread_ == nullptr; }

  // The line each Lua function of the coroutine is at, the innermost first (the functions of C
  // left out): where a waiting order was called and, last, the line of the coroutine's own
  // function that leads there. Empty before the first resume and once it finished.
  [[nodiscard]] std::vector<std::int32_t> lines() const;

private:
  void close(lua_State* from);

  detail::VmState* vm_;
  std::size_t mod_;
  int reference_;
  lua_State* thread_;
  int arguments_ = 0;
  bool started_ = false;
};

} // namespace dwe::script
