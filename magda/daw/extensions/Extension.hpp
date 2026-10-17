/**
 * @file Extension.hpp
 * @brief App-level extensions linked in at build time.
 *
 * An extension is a static library named in MAGDA_EXTENSION_DIRS. It registers a
 * factory from a static initialiser, and the app links the archive whole. Like the
 * device SDK this is a source contract, not a stable ABI; unlike it, an extension
 * may use any of the app's headers.
 */
#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <optional>
#include <vector>

namespace magda {
class AudioEngine;
class MainWindow;
}  // namespace magda

namespace magda::extensions {

/** @brief The product name shown in the title bar, splash and About box. */
struct Edition {
    juce::String productName = "MAGDA";
};

/** @brief What an extension can reach once the main window exists. */
struct AppContext {
    AudioEngine& engine;
    MainWindow& mainWindow;
};

/**
 * @brief A part of the app built outside this repository.
 *
 * Created before the splash, so edition() is read for it. onStartup() runs once the
 * main window exists and onShutdown() before it is destroyed, so UI an extension
 * attached is gone before the window it hangs off.
 */
class Extension {
  public:
    virtual ~Extension() = default;

    virtual const char* id() const = 0;
    virtual std::optional<Edition> edition() const {
        return std::nullopt;
    }
    virtual void onStartup(AppContext&) {}
    virtual void onShutdown() {}
};

using ExtensionFactory = std::unique_ptr<Extension> (*)();

/** @brief Adds @p factory; call from a static initialiser in the extension's archive. */
bool registerExtension(ExtensionFactory factory);

/** @brief Instantiates every registered extension. The app calls it once, at launch. */
void createExtensions();

/** @brief Runs onStartup() on each extension, in registration order. */
void startExtensions(AppContext& context);

/** @brief Runs onShutdown() on the started extensions in reverse order, then destroys all. */
void destroyExtensions();

/** @brief The ids of the instantiated extensions. */
std::vector<juce::String> loadedExtensionIds();

/** @brief The edition an extension supplies, or the default when none does. */
const Edition& currentEdition();

}  // namespace magda::extensions
