#include "scripting.hpp"

#include <dwe/core/log.hpp>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/ObserverPtr.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "code_trust.hpp"
#include "messages.hpp"

namespace dwe::ui {
namespace {

// Raises a Lua error after the script position, as luaL_error does. Only plain values may be alive in the callers,
// because lua_error unwinds with longjmp.
[[noreturn]] void raise(lua_State* state, const char* message) {
    luaL_where(state, 1);
    lua_pushstring(state, message);
    lua_concat(state, 2);
    lua_error(state);
    std::unreachable();
}

void checkName(lua_State* state, std::string_view name) {
    if (name.size() > MaxScriptName) {
        raise(state, "the name is too long");
    }
}

// The prefix of the attributes that RmlUi turns into event listeners (onclick, onkeydown...), in any case.
constexpr std::string_view EventAttributePrefix = "on";

bool isEventAttribute(std::string_view name) {
    return name.size() >= EventAttributePrefix.size() &&
           std::ranges::equal(name.substr(0, EventAttributePrefix.size()), EventAttributePrefix,
                              [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
}

void checkText(lua_State* state, std::string_view text) {
    if (text.size() > MaxScriptText) {
        raise(state, "the text is too long");
    }
}

// A placeholder value of t() as Lua holds it: a view into one of Lua's strings, or a number.
struct LuaTextArgument {
    std::string_view name;
    std::string_view text;
    double number = 0.0;
    bool isNumber = false;
};

using LuaTextArguments = std::array<LuaTextArgument, MaxTextArguments>;

// Reads the table of placeholder values at a stack index: its entries with a string key and a string or number value.
// Only plain values are alive here, because the checks raise Lua errors.
std::size_t readTextArguments(lua_State* state, int index, LuaTextArguments& arguments) {
    if (lua_type(state, index) != LUA_TTABLE) {
        return 0;
    }
    luaL_checkstack(state, 2, "too many nested calls");
    std::size_t count = 0;
    lua_pushnil(state);
    while (lua_next(state, index) != 0) {
        const int value = lua_type(state, -1);
        if (lua_type(state, -2) == LUA_TSTRING && (value == LUA_TSTRING || value == LUA_TNUMBER)) {
            if (count == arguments.size()) {
                raise(state, "too many placeholder values");
            }
            LuaTextArgument& argument = arguments.at(count++);
            std::size_t length = 0;
            const char* name = lua_tolstring(state, -2, &length);
            argument.name = {name, length};
            checkName(state, argument.name);
            argument.isNumber = value == LUA_TNUMBER;
            if (argument.isNumber) {
                argument.number = lua_tonumber(state, -1);
            } else {
                const char* text = lua_tolstring(state, -1, &length);
                argument.text = {text, length};
                checkText(state, argument.text);
            }
        }
        lua_pop(state, 1);
    }
    return count;
}

std::vector<localization::TextArgument> textArgumentsOf(std::span<const LuaTextArgument> read) {
    std::vector<localization::TextArgument> arguments;
    arguments.reserve(read.size());
    for (const LuaTextArgument& argument : read) {
        arguments.push_back(
            {.name = argument.name,
             .value = argument.isNumber ? localization::numberText(argument.number) : std::string{argument.text}});
    }
    return arguments;
}

UiValue toUiValue(const sol::stack_object& value) {
    switch (value.get_type()) {
    case sol::type::boolean:
        return value.as<bool>();
    case sol::type::number:
        return value.as<double>();
    case sol::type::string:
        return std::string{value.as<std::string_view>()};
    default:
        return std::monostate{};
    }
}

void pushOne(lua_State* state, const std::string& text) {
    lua_pushlstring(state, text.data(), text.size());
}

void pushOne(lua_State* state, const std::optional<std::string>& text) {
    if (text) {
        pushOne(state, *text);
    } else {
        lua_pushnil(state);
    }
}

void pushOne(lua_State* state, const LuaElement& element) {
    if (element.element) {
        sol::stack::push(state, element);
    } else {
        lua_pushnil(state);
    }
}

void pushOne(lua_State* state, const UiValue& value) {
    std::visit(
        [state](const auto& held) {
            using Held = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<Held, std::monostate>) {
                lua_pushnil(state);
            } else if constexpr (std::is_same_v<Held, bool>) {
                lua_pushboolean(state, held ? 1 : 0);
            } else if constexpr (std::is_same_v<Held, double>) {
                lua_pushnumber(state, held);
            } else {
                pushOne(state, held);
            }
        },
        value);
}

void pushOne(lua_State* state, const std::vector<std::string>& texts) {
    lua_createtable(state, static_cast<int>(texts.size()), 0);
    for (std::size_t i = 0; i < texts.size(); ++i) {
        pushOne(state, texts[i]);
        lua_rawseti(state, -2, static_cast<lua_Integer>(i) + 1);
    }
}

template <typename Value>
int pushValue(lua_State* state) {
    pushOne(state, *static_cast<const Value*>(lua_touserdata(state, 1)));
    return 1;
}

// Returns what produce() makes to Lua. The value is pushed in a protected call and destroyed before a failed push
// (no memory left) is raised again, so the longjmp skips no destructor.
template <typename Produce>
sol::stack_object result(lua_State* state, const Produce& produce) {
    bool pushed = false;
    {
        auto value = produce();
        lua_pushcfunction(state, &pushValue<decltype(value)>);
        lua_pushlightuserdata(state, &value);
        pushed = lua_pcall(state, 1, 1, 0) == LUA_OK;
    }
    if (!pushed) {
        lua_error(state);
        std::unreachable();
    }
    return sol::stack_object{state, -1};
}

// getmetatable for documents: the metatables of strings and of the engine's types are shared by every document, so
// scripts only see those of their own tables.
int tableMetatable(lua_State* state) {
    if (lua_type(state, 1) != LUA_TTABLE || lua_getmetatable(state, 1) == 0) {
        lua_pushnil(state);
        return 1;
    }
    // A protected metatable shows its __metatable field instead, as with Lua's own getmetatable.
    luaL_getmetafield(state, 1, "__metatable");
    return 1;
}

// Builds a document's environment: every global, with the tables (the libraries, `ui`) copied, and `_G` naming the
// environment itself.
int buildEnvironment(lua_State* state) {
    lua_newtable(state);
    const int environment = lua_gettop(state);
    lua_pushglobaltable(state);
    const int globals = lua_gettop(state);
    lua_pushnil(state);
    while (lua_next(state, globals) != 0) {
        if (lua_rawequal(state, -1, globals) != 0) {
            lua_pop(state, 1);
            continue;
        }
        if (lua_type(state, -1) == LUA_TTABLE) {
            const int library = lua_gettop(state);
            lua_newtable(state);
            lua_pushnil(state);
            while (lua_next(state, library) != 0) {
                lua_pushvalue(state, -2);
                lua_insert(state, -2);
                lua_rawset(state, library + 1);
            }
            lua_remove(state, library);
        }
        lua_pushvalue(state, -2);
        lua_insert(state, -2);
        lua_rawset(state, environment);
    }
    lua_pop(state, 1);
    lua_pushvalue(state, environment);
    lua_setfield(state, environment, "_G");
    lua_pushcfunction(state, tableMetatable);
    lua_setfield(state, environment, "getmetatable");
    return 1;
}

// The handler of an event attribute: its Lua code runs with `event`, `element` and `document` as locals, in the
// environment of the element's document. It is compiled the first time it runs, because the element is not in its
// document yet when RmlUi creates the listener.
class LuaListener final : public Rml::EventListener {
public:
    explicit LuaListener(Rml::String code) : code_{std::move(code)} {}

    void ProcessEvent(Rml::Event& event) override {
        Rml::Element* element = event.GetCurrentElement();
        auto* document = dynamic_cast<ScriptedDocument*>(element != nullptr ? element->GetOwnerDocument() : nullptr);
        // A document whose load turned out to read a template from a mod runs none of its own code.
        if (document == nullptr || !document->ownCode()) {
            return;
        }
        Scripting& scripting = document->scripting();
        if (!compiled_) {
            auto compiled = scripting.compile("local event, element, document = ...\n" + code_,
                                              std::format("={}:{}", document->GetSourceURL(), event.GetType()),
                                              document->environment());
            if (!compiled) {
                core::logError("ui: {}", compiled.error().message);
                code_.clear();
                compiled_ = sol::protected_function{};
                return;
            }
            compiled_ = std::move(*compiled);
        }
        if (!compiled_->valid()) {
            return;
        }
        auto handle = std::make_shared<Rml::Event*>(&event);
        scripting.run(LuaCallee{.function = &*compiled_, .environment = nullptr, .name = {}}, document->name(),
                      event.GetType(), LuaEvent{handle}, LuaElement{element->GetObserverPtr()},
                      LuaElement{document->GetObserverPtr()});
        // A script that kept the event cannot use it after the handler.
        *handle = nullptr;
    }

    // RmlUi hands listeners made by an instancer to the element, which detaches them when it no longer needs them.
    void OnDetach(Rml::Element* /*element*/) override {
        delete this; // NOLINT(cppcoreguidelines-owning-memory): RmlUi's ownership of instanced listeners.
    }

private:
    Rml::String code_;
    std::optional<sol::protected_function> compiled_;
};

} // namespace

ScriptedDocument::ScriptedDocument(const Rml::String& tag, Scripting& scripting)
    : Rml::ElementDocument{tag}, scripting_{&scripting}, environment_{scripting.newEnvironment()},
      ownCode_{scripting.trust().loadVerdict()} {}

void ScriptedDocument::setName(std::string_view name) {
    name_ = name;
    environment_["document_name"] = name_;
}

void ScriptedDocument::LoadInlineScript(const Rml::String& content, const Rml::String& sourcePath, int sourceLine) {
    // The script is part of the document, or of a template it reads: both must come from the engine's layers.
    if (auto trusted = scripting_->trust().checkInlineSource(sourcePath); !trusted) {
        ownCode_ = std::move(trusted);
        scripting_->trust().reportRefusedInline();
        return;
    }
    auto compiled = scripting_->compile(content, std::format("={}:{}", sourcePath, sourceLine), environment_);
    if (!compiled) {
        core::logError("ui: {}", compiled.error().message);
        return;
    }
    scripting_->run(LuaCallee{.function = &*compiled, .environment = nullptr, .name = {}}, sourcePath, "load");
}

void ScriptedDocument::LoadExternalScript(const Rml::String& sourcePath) {
    // The script file is the engine's code only if the engine's layers provide it, whoever names it.
    if (auto trusted = scripting_->trust().checkScript(sourcePath); !trusted) {
        core::logError("ui: {}", trusted.error().message);
        return;
    }
    Rml::String source;
    if (!Rml::GetFileInterface()->LoadFile(sourcePath, source)) {
        core::logError("ui: could not read the script {}", sourcePath);
        return;
    }
    auto compiled = scripting_->compile(source, "@" + sourcePath, environment_);
    if (!compiled) {
        core::logError("ui: {}", compiled.error().message);
        return;
    }
    scripting_->run(LuaCallee{.function = &*compiled, .environment = nullptr, .name = {}}, sourcePath, "load");
}

Scripting::Scripting(script::ScriptVm& vm, ScriptHost host, CodeTrust& trust)
    : vm_{&vm}, host_{std::move(host)}, trust_{&trust} {
    registerApi();
}

void Scripting::install() {
    Rml::Factory::RegisterElementInstancer("body", &documentInstancer_);
    Rml::Factory::RegisterEventListenerInstancer(&listenerInstancer_);
}

core::Result<sol::protected_function> Scripting::compile(std::string_view source, std::string_view chunkName,
                                                         const sol::environment& environment) {
    // Text only: precompiled bytecode is not checked by Lua and could break out of the sandbox.
    const script::ScriptCall scope{*vm_};
    sol::load_result loaded = scope.lua().load(source, std::string{chunkName}, sol::load_mode::text);
    if (!loaded.valid()) {
        const sol::error error = loaded;
        return core::fail(error.what());
    }
    sol::protected_function function = loaded;
    sol::set_environment(environment, function);
    return function;
}

sol::environment Scripting::newEnvironment() {
    const script::ScriptCall scope{*vm_};
    lua_State* state = scope.lua().lua_state();
    lua_pushcfunction(state, buildEnvironment);
    if (lua_pcall(state, 0, 1, 0) != LUA_OK) {
        lua_pop(state, 1);
        core::logError("ui: no memory left for a document's scripts");
        return sol::environment{scope.lua(), sol::create};
    }
    sol::environment environment{state, -1};
    lua_pop(state, 1);
    return environment;
}

bool Scripting::pushCallee(lua_State* state, const LuaCallee& callee) {
    if (callee.function != nullptr) {
        callee.function->push(state);
        return true;
    }
    callee.environment->push(state);
    lua_pushlstring(state, callee.name.data(), callee.name.size());
    const bool found = lua_rawget(state, -2) == LUA_TFUNCTION;
    lua_remove(state, -2);
    if (!found) {
        lua_pop(state, 1);
    }
    return found;
}

void Scripting::pushArgument(lua_State* state, const LuaElement& element) {
    pushOne(state, element);
}

void Scripting::pushArgument(lua_State* state, const LuaEvent& event) {
    sol::stack::push(state, event);
}

void Scripting::pushArgument(lua_State* state, std::string_view text) {
    lua_pushlstring(state, text.data(), text.size());
}

void Scripting::pushArgument(lua_State* state, double number) {
    lua_pushnumber(state, number);
}

void Scripting::pushArgument(lua_State* state, std::span<const std::string> texts) {
    luaL_checkstack(state, static_cast<int>(texts.size()), "too many arguments");
    for (const std::string& text : texts) {
        pushArgument(state, std::string_view{text});
    }
}

core::Result<LuaCallResult> Scripting::protectedCall(lua_State* state, int top) {
    // call() holds the ScriptCall that gives this its budget.
    if (lua_pcall(state, 1, 2, 0) != LUA_OK) {
        std::string message = lua_type(state, -1) == LUA_TSTRING
                                  ? std::string{lua_tostring(state, -1)}
                                  : std::string{"the script raised a value that is not a text"};
        lua_settop(state, top);
        return core::fail(std::move(message));
    }
    const LuaCallResult called{
        .called = lua_toboolean(state, -2) != 0,
        .returnedTrue = lua_type(state, -1) == LUA_TBOOLEAN && lua_toboolean(state, -1) != 0,
    };
    lua_settop(state, top);
    return called;
}

Rml::ElementPtr Scripting::DocumentInstancer::InstanceElement(Rml::Element* /*parent*/, const Rml::String& tag,
                                                              const Rml::XMLAttributes& /*attributes*/) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): RmlUi gives the element back to ReleaseElement.
    return Rml::ElementPtr{new ScriptedDocument{tag, *scripting_}};
}

void Scripting::DocumentInstancer::ReleaseElement(Rml::Element* element) {
    delete element; // NOLINT(cppcoreguidelines-owning-memory): the other half of InstanceElement.
}

Rml::EventListener* Scripting::ListenerInstancer::InstanceEventListener(const Rml::String& value,
                                                                        Rml::Element* element) {
    // While a document loads, its elements are not in it yet: the load's verdict applies. Later, the element's
    // document's. An element with neither is refused.
    CodeTrust& trust = scripting_->trust();
    core::Result<void> trusted = core::fail(std::string{messages::CodeOutsideLoad});
    if (trust.loading()) {
        trusted = trust.loadVerdict();
    } else if (auto* document =
                   dynamic_cast<ScriptedDocument*>(element != nullptr ? element->GetOwnerDocument() : nullptr);
               document != nullptr) {
        trusted = document->ownCode();
    }
    if (!trusted) {
        if (trust.loading()) {
            trust.reportRefusedInline();
        } else {
            core::logError("ui: {}", trusted.error().message);
        }
        return nullptr;
    }
    return new LuaListener{value}; // NOLINT(cppcoreguidelines-owning-memory): deleted by OnDetach.
}

void Scripting::registerApi() {
    const script::ScriptCall scope{*vm_};
    sol::state& state = scope.lua();

    // Every function takes its strings as views into Lua's own and returns through result(): see the class comment.
    state.new_usertype<LuaElement>(
        "Element", sol::no_constructor, "valid", [](const LuaElement& self) { return static_cast<bool>(self.element); },
        "id", sol::readonly_property([](const LuaElement& self, sol::this_state lua) {
            return result(lua, [&] { return self.element ? self.element->GetId() : std::string{}; });
        }),
        "get",
        [](const LuaElement& self, std::string_view id, sol::this_state lua) {
            checkName(lua, id);
            return result(lua, [&] {
                Rml::Element* found = self.element ? self.element->GetElementById(Rml::String{id}) : nullptr;
                return LuaElement{found != nullptr ? found->GetObserverPtr() : Rml::ObserverPtr<Rml::Element>{}};
            });
        },
        "set_text",
        [](const LuaElement& self, std::string_view text, sol::this_state lua) {
            checkText(lua, text);
            if (self.element) {
                self.element->SetInnerRML(Rml::StringUtilities::EncodeRml(Rml::String{text}));
            }
        },
        "text",
        [](const LuaElement& self, sol::this_state lua) {
            return result(lua, [&] { return self.element ? self.element->GetInnerRML() : std::string{}; });
        },
        "set_class",
        [](const LuaElement& self, std::string_view name, bool on, sol::this_state lua) {
            checkName(lua, name);
            if (self.element) {
                self.element->SetClass(Rml::String{name}, on);
            }
        },
        "has_class",
        [](const LuaElement& self, std::string_view name, sol::this_state lua) {
            checkName(lua, name);
            return self.element && self.element->IsClassSet(Rml::String{name});
        },
        "attribute",
        [](const LuaElement& self, std::string_view name, sol::this_state lua) {
            checkName(lua, name);
            return result(lua, [&]() -> std::optional<std::string> {
                const Rml::String key{name};
                if (!self.element || self.element->GetAttribute(key) == nullptr) {
                    return std::nullopt;
                }
                return self.element->GetAttribute<Rml::String>(key, "");
            });
        },
        "set_attribute",
        [](const LuaElement& self, std::string_view name, std::string_view value, sol::this_state lua) {
            checkName(lua, name);
            checkText(lua, value);
            // A listener made from a script's text would run code that no document brought, past the trust.
            if (isEventAttribute(name)) {
                raise(lua, messages::EventAttributeFromScript);
            }
            if (self.element) {
                self.element->SetAttribute(Rml::String{name}, Rml::String{value});
            }
        },
        "set_property",
        [](const LuaElement& self, std::string_view name, std::string_view value, sol::this_state lua) {
            checkName(lua, name);
            checkText(lua, value);
            if (self.element) {
                self.element->SetProperty(Rml::String{name}, Rml::String{value});
            }
        },
        "focus",
        [](const LuaElement& self) {
            if (self.element) {
                self.element->Focus(true);
            }
        },
        "has_focus",
        [](const LuaElement& self) {
            Rml::Context* context = self.element ? self.element->GetContext() : nullptr;
            return context != nullptr && context->GetFocusElement() == self.element.get();
        },
        "click",
        [](const LuaElement& self) {
            if (self.element) {
                self.element->Click();
            }
        });

    state.new_usertype<LuaEvent>(
        "Event", sol::no_constructor, "type", sol::readonly_property([](const LuaEvent& self, sol::this_state lua) {
            return result(lua, [&] { return *self.event != nullptr ? (*self.event)->GetType() : std::string{}; });
        }),
        "stop", [](const LuaEvent& self) {
            if (*self.event != nullptr) {
                (*self.event)->StopPropagation();
            }
        });

    sol::table api = state.create_named_table("ui");
    api["send"] = [this](std::string_view name, sol::stack_object value, sol::this_state lua) {
        checkName(lua, name);
        if (value.get_type() == sol::type::string) {
            checkText(lua, value.as<std::string_view>());
        }
        if (!host_.send(UiCommand{.name = std::string{name}, .value = toUiValue(value)})) {
            raise(lua, "too many commands: the game takes them once per frame");
        }
    };
    api["setting"] = [this](std::string_view name, sol::this_state lua) {
        checkName(lua, name);
        return result(lua, [&] { return host_.setting(name); });
    };
    api["prompt"] = [this](std::string_view action, sol::this_state lua) {
        checkName(lua, action);
        return result(lua, [&] { return host_.prompt(action); });
    };
    api["languages"] = [this](sol::this_state lua) { return result(lua, [&] { return host_.languages(); }); };
    api["skins"] = [this](sol::this_state lua) { return result(lua, [&] { return host_.skins(); }); };
    api["skin_name_key"] = [this](std::string_view id, sol::this_state lua) {
        checkName(lua, id);
        return result(lua, [&] { return host_.skinNameKey(id); });
    };
    // The key is argument 1 and the table of placeholder values, if any, argument 2.
    state[std::string{TextFunction}] = [this](std::string_view key, sol::this_state lua) {
        checkName(lua, key);
        LuaTextArguments read{};
        const std::size_t count = readTextArguments(lua, 2, read);
        return result(
            lua, [&] { return host_.text(key, textArgumentsOf(std::span<const LuaTextArgument>{read}.first(count))); });
    };
    api["device"] = [this](sol::this_state lua) { return result(lua, [&] { return host_.device(); }); };
    api["gamepad_family"] = [this](sol::this_state lua) { return result(lua, [&] { return host_.gamepadFamily(); }); };
    api["show"] = [this](std::string_view name, sol::this_state lua) {
        checkName(lua, name);
        host_.show(name);
    };
    api["hide"] = [this](std::string_view name, sol::this_state lua) {
        checkName(lua, name);
        host_.hide(name);
    };
    api["is_visible"] = [this](std::string_view name, sol::this_state lua) {
        checkName(lua, name);
        return host_.isVisible(name);
    };
}

} // namespace dwe::ui
