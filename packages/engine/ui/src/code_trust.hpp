#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace dwe::ui {

// Which Lua that documents bring may run as the engine. The rule is script::checkEngineScript and
// script::checkEngineDocument: only the base and remaster layers provide such code. A document from
// another layer, or one that reads a template from another layer, still loads; its inline scripts
// and event attributes do not run. A <script src> runs only if its file comes from the engine's
// layers, whatever the document's.
//
// A UI addon's code is the exception: once the UI takes an addon (allowAddon), the documents,
// templates and scripts under its folder run, sandboxed (sandboxed()): the Lua that loads them
// gives their environment a ui.send that only takes the commands the game allows addons.
class CodeTrust {
public:
  explicit CodeTrust(const assets::VirtualFileSystem& files) : files_{&files} {}

  // Starts and ends the load of a document. endLoad gives whether the document may run its own
  // code.
  void beginLoad(std::string_view documentPath);
  core::Result<void> endLoad();

  // Every file RmlUi reads while a document loads: a template (.rml) from another layer takes the
  // document's trust.
  void noteRead(std::string_view path);

  [[nodiscard]] bool loading() const { return loading_; }

  // Whether the document that is loading may run its own code so far. Outside a load, no document
  // may.
  [[nodiscard]] core::Result<void> loadVerdict() const;

  // Whether code written inside a file (a document or a template) may run, given the load it is
  // part of.
  [[nodiscard]] core::Result<void> checkInlineSource(std::string_view path) const;

  // Whether a script file named by <script src> may run.
  [[nodiscard]] core::Result<void> checkScript(std::string_view path) const;

  // Logs why the loading document's own code does not run, once per load.
  void reportRefusedInline();

  // The asset folder of an addon the UI took ("ui/addons/clock/"): its code runs, sandboxed.
  void allowAddon(std::string_view folder);
  void forgetAddons();

  // Whether the document that is loading is an addon's, whose code runs sandboxed.
  [[nodiscard]] bool sandboxed() const { return loading_ && sandboxed_; }

private:
  [[nodiscard]] core::Result<void> checkDocument(std::string_view path) const;
  // Whether a file is under the folder of an addon the UI took.
  [[nodiscard]] bool inAddon(std::string_view path) const;

  const assets::VirtualFileSystem* files_;
  bool loading_ = false;
  bool reported_ = false;
  bool sandboxed_ = false;
  std::vector<std::string> addonFolders_;
  core::Result<void> verdict_;
};

} // namespace dwe::ui
