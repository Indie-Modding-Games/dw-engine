#pragma once

#include <dwe/core/error.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/script/budget_stop.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>

struct
    lua_State; // NOLINT(readability-identifier-naming): a third-party type, named by its library.

namespace dwe::script {

namespace detail {
struct VmState;
}

struct CoroutineWaiting {
  ApiValue value;
  // Lua functions between the coroutine's own function and the caller of the waiting function: 0
  // when the coroutine's function called it. A story jump from inside a linked routine needs it to
  // keep its caller waiting.
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

private:
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
  ScriptCoroutine(detail::VmState& vm, std::size_t mod, int reference, lua_State* thread);
  ScriptCoroutine(const ScriptCoroutine&) = delete;
  ScriptCoroutine& operator=(const ScriptCoroutine&) = delete;
  ScriptCoroutine(ScriptCoroutine&& other) noexcept;
  ScriptCoroutine& operator=(ScriptCoroutine&& other) noexcept;
  ~ScriptCoroutine();

  // The first resume calls the function without arguments and ignores value.
  CoroutineStep resume(const ApiValue& value = {});

  [[nodiscard]] bool finished() const { return thread_ == nullptr; }

private:
  void close();

  detail::VmState* vm_;
  std::size_t mod_;
  int reference_;
  lua_State* thread_;
  bool started_ = false;
};

} // namespace dwe::script
