#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <string_view>

namespace dwe::ui {

// Which Lua that documents bring may run as the engine. The rule is script::checkEngineScript and
// script::checkEngineDocument: only the base and remaster layers provide such code. A document from another layer, or
// one that reads a template from another layer, still loads; its inline scripts and event attributes do not run. A
// <script src> runs only if its file comes from the engine's layers, whatever the document's.
class CodeTrust {
public:
    explicit CodeTrust(const assets::VirtualFileSystem& files) : files_{&files} {}

    // Starts and ends the load of a document. endLoad gives whether the document may run its own code.
    void beginLoad(std::string_view documentPath);
    core::Result<void> endLoad();

    // Every file RmlUi reads while a document loads: a template (.rml) from another layer takes the document's trust.
    void noteRead(std::string_view path);

    [[nodiscard]] bool loading() const { return loading_; }

    // Whether the document that is loading may run its own code so far. Outside a load, no document may.
    [[nodiscard]] core::Result<void> loadVerdict() const;

    // Whether code written inside a file (a document or a template) may run, given the load it is part of.
    [[nodiscard]] core::Result<void> checkInlineSource(std::string_view path) const;

    // Whether a script file named by <script src> may run.
    [[nodiscard]] core::Result<void> checkScript(std::string_view path) const;

    // Logs why the loading document's own code does not run, once per load.
    void reportRefusedInline();

private:
    [[nodiscard]] core::Result<void> checkDocument(std::string_view path) const;

    const assets::VirtualFileSystem* files_;
    bool loading_ = false;
    bool reported_ = false;
    core::Result<void> verdict_;
};

} // namespace dwe::ui
