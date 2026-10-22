#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/log.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/ui/ui.hpp>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListenerInstancer.h>
#include <RmlUi/Core/ObserverPtr.h>
#include <sol/sol.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace dwe::ui {

class Scripting;
class CodeTrust;

// The longest name (element id, class, document, command, setting) and text a script may pass to
// the UI, in bytes.
inline constexpr std::size_t MaxScriptName = 256;
inline constexpr std::size_t MaxScriptText = std::size_t{64} * 1024;
// The most placeholder values one call of t() may pass.
inline constexpr std::size_t MaxTextArguments = 16;
// The global function that gives the text of a key: t(key[, {name = value}]).
inline constexpr std::string_view TextFunction = "t";

// What the Lua API asks of the UI.
struct ScriptHost {
  // Queues a command for the app; false when the queue is full.
  std::function<bool(UiCommand)> send;
  std::function<UiValue(std::string_view)> setting;
  std::function<std::string(std::string_view)> prompt;
  // The text of a key in the player's language with its placeholders filled.
  std::function<std::string(std::string_view, std::span<const localization::TextArgument>)> text;
  // The languages the player can choose.
  std::function<std::vector<std::string>()> languages;
  // The skins the player can choose, by id, and the text key of a skin's name (empty for an unknown
  // id).
  std::function<std::vector<std::string>()> skins;
  std::function<std::string(std::string_view)> skinNameKey;
  std::function<std::string()> device;
  std::function<std::string()> gamepadFamily;
  std::function<void(std::string_view)> show;
  std::function<void(std::string_view)> hide;
  std::function<bool(std::string_view)> isVisible;
};

// An element seen from Lua. It does not keep the element alive: once the element is gone every
// method does nothing and returns nil.
struct LuaElement {
  Rml::ObserverPtr<Rml::Element> element;
};

// An event seen from Lua, valid only while its handler runs.
struct LuaEvent {
  std::shared_ptr<Rml::Event*> event;
};

// A document whose scripts, and the handlers in its event attributes (onclick="..."), run in its
// own Lua environment, so two documents may define functions with the same names. The environment
// holds its own copies of the libraries and of the `ui` table, so a document cannot change what
// another one sees.
class ScriptedDocument final : public Rml::ElementDocument {
public:
  ScriptedDocument(const Rml::String& tag, Scripting& scripting);

  void LoadInlineScript(const Rml::String& content, const Rml::String& sourcePath,
                        int sourceLine) override;
  void LoadExternalScript(const Rml::String& sourcePath) override;

  // The name the UI knows the document by, also visible to its scripts as `document_name`.
  void setName(std::string_view name);

  // Whether the document may run the Lua written inside it (CodeTrust). Until its load ends it may
  // not.
  void setOwnCode(core::Result<void> verdict) { ownCode_ = std::move(verdict); }
  [[nodiscard]] const core::Result<void>& ownCode() const { return ownCode_; }
  [[nodiscard]] const std::string& name() const { return name_; }

  [[nodiscard]] sol::environment& environment() { return environment_; }
  [[nodiscard]] Scripting& scripting() { return *scripting_; }

  // Calls a function of the document's environment, if it defines one, with the document and the
  // arguments. Returns whether the function returned true; errors are logged.
  template <typename... Arguments>
  bool callHandler(std::string_view function, const Arguments&... arguments);

private:
  Scripting* scripting_;
  sol::environment environment_;
  std::string name_;
  core::Result<void> ownCode_;
};

// A function to call: compiled code, or a function a document's environment defines (none when it
// does not).
struct LuaCallee {
  const sol::reference* function = nullptr;
  const sol::environment* environment = nullptr;
  std::string_view name;
};

// What a call into Lua did.
struct LuaCallResult {
  // False when the environment does not define the function.
  bool called = false;
  // Whether its first result was true.
  bool returnedTrue = false;
};

// The Lua side of the UI: the `ui` table, the Element and Event types, and the RmlUi instancers
// that give documents and event attributes their scripts. Everything runs in the UI's own sandboxed
// ScriptVm.
//
// Lua raises errors with longjmp, which skips C++ destructors. Calls into Lua therefore convert
// their arguments and run inside one lua_pcall (call()), and the API's functions keep no C++ object
// alive while Lua may raise: they take strings as views into Lua's own, and push their results in a
// protected call after which only plain values remain.
class Scripting {
public:
  // trust decides which Lua the documents may run, and must outlive this.
  Scripting(script::ScriptVm& vm, ScriptHost host, CodeTrust& trust);

  Scripting(const Scripting&) = delete;
  Scripting& operator=(const Scripting&) = delete;
  Scripting(Scripting&&) = delete;
  Scripting& operator=(Scripting&&) = delete;
  ~Scripting() = default;

  // Makes RmlUi create ScriptedDocuments and Lua event listeners. Call after Rml::Initialise.
  void install();

  // Compiles Lua source as text in an environment.
  core::Result<sol::protected_function> compile(std::string_view source, std::string_view chunkName,
                                                const sol::environment& environment);

  // Calls a function with the arguments (elements, events, strings, numbers). Only the outermost
  // call gets a fresh instruction budget: a handler that clicks an element or shows a document runs
  // other handlers inside its own call, and those must not refill its budget.
  template <typename... Arguments>
  core::Result<LuaCallResult> call(const LuaCallee& callee, const Arguments&... arguments);

  // call(), logging an error as `ui script <owner> <what>: ...`. Returns whether the function
  // returned true.
  template <typename... Arguments>
  bool run(const LuaCallee& callee, std::string_view owner, std::string_view what,
           const Arguments&... arguments);

  // A fresh environment for a document: copies of the global libraries and of `ui`, and no way back
  // to the globals.
  sol::environment newEnvironment();

  [[nodiscard]] CodeTrust& trust() { return *trust_; }

private:
  class DocumentInstancer final : public Rml::ElementInstancer {
  public:
    explicit DocumentInstancer(Scripting& scripting) : scripting_{&scripting} {}
    Rml::ElementPtr InstanceElement(Rml::Element* parent, const Rml::String& tag,
                                    const Rml::XMLAttributes& attributes) override;
    void ReleaseElement(Rml::Element* element) override;

  private:
    Scripting* scripting_;
  };

  // Makes the listener of an event attribute such as onclick, which is Lua written inside a
  // document: none when the document may not run its own code.
  class ListenerInstancer final : public Rml::EventListenerInstancer {
  public:
    explicit ListenerInstancer(Scripting& scripting) : scripting_{&scripting} {}
    Rml::EventListener* InstanceEventListener(const Rml::String& value,
                                              Rml::Element* element) override;

  private:
    Scripting* scripting_;
  };

  // The argument pack of one call, read by the trampoline inside the protected call.
  template <typename... Arguments>
  struct CallFrame {
    const LuaCallee* callee;
    std::tuple<const Arguments&...> arguments;
  };

  template <typename... Arguments>
  static int trampoline(lua_State* state);

  // Pushes the callee: true when there is a function to call.
  static bool pushCallee(lua_State* state, const LuaCallee& callee);

  static void pushArgument(lua_State* state, const LuaElement& element);
  static void pushArgument(lua_State* state, const LuaEvent& event);
  static void pushArgument(lua_State* state, std::string_view text);
  static void pushArgument(lua_State* state, double number);
  static void pushArgument(lua_State* state, std::span<const std::string> texts);

  // Runs a pushed function and its arguments protected; afterwards the stack is back to `top`.
  static core::Result<LuaCallResult> protectedCall(lua_State* state, int top);

  void registerApi();

  script::ScriptVm* vm_;
  ScriptHost host_;
  CodeTrust* trust_;
  DocumentInstancer documentInstancer_{*this};
  ListenerInstancer listenerInstancer_{*this};
};

template <typename... Arguments>
int Scripting::trampoline(lua_State* state) {
  // Only plain pointers are alive here: an error raised while pushing unwinds to protectedCall's
  // lua_pcall.
  const auto* frame = static_cast<const CallFrame<Arguments...>*>(lua_touserdata(state, 1));
  if (!pushCallee(state, *frame->callee)) {
    lua_pushboolean(state, 0);
    lua_pushboolean(state, 0);
    return 2;
  }
  const int function = lua_gettop(state);
  std::apply([state](const auto&... argument) { (pushArgument(state, argument), ...); },
             frame->arguments);
  lua_call(state, lua_gettop(state) - function, 1);
  lua_pushboolean(state, 1);
  lua_insert(state, -2);
  return 2;
}

template <typename... Arguments>
core::Result<LuaCallResult> Scripting::call(const LuaCallee& callee,
                                            const Arguments&... arguments) {
  // Nested calls (a document's script calling into another) share the budget of the outermost one.
  const script::ScriptCall scope{*vm_};
  lua_State* state = scope.lua().lua_state();
  const int top = lua_gettop(state);
  CallFrame<Arguments...> frame{.callee = &callee, .arguments = {arguments...}};
  if (lua_checkstack(state, 2) == 0) {
    return core::fail("the script has no memory left");
  }
  lua_pushcfunction(state, &Scripting::trampoline<Arguments...>);
  lua_pushlightuserdata(state, &frame);
  return protectedCall(state, top);
}

template <typename... Arguments>
bool Scripting::run(const LuaCallee& callee, std::string_view owner, std::string_view what,
                    const Arguments&... arguments) {
  auto result = call(callee, arguments...);
  if (!result) {
    core::logError("ui script {} {}: {}", owner, what, result.error().message);
    return false;
  }
  return result->returnedTrue;
}

template <typename... Arguments>
bool ScriptedDocument::callHandler(std::string_view function, const Arguments&... arguments) {
  const LuaCallee callee{.function = nullptr, .environment = &environment_, .name = function};
  return scripting_->run(callee, name_, function, LuaElement{GetObserverPtr()}, arguments...);
}

} // namespace dwe::ui
