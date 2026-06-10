#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

#include "../../magda/agents/llama_model_manager.hpp"
#include "../../magda/agents/llm_presets.hpp"
#include "api/magda_api_live.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/AppPaths.hpp"
#include "core/ClipManager.hpp"
#include "core/Config.hpp"
#include "core/ModulatorEngine.hpp"
#include "core/PluginPreferences.hpp"
#include "core/PresetManager.hpp"
#include "core/TrackManager.hpp"
#include "core/UIScale.hpp"
#include "core/UpdateChecker.hpp"
#include "core/controllers/ControllerActivation.hpp"
#include "core/controllers/ControllerProfileRegistry.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "magda/scripting/LuaController.hpp"
#include "magda/scripting/LuaScriptStore.hpp"
#include "media_db/MediaDbContext.hpp"
#include "project/ProjectManager.hpp"
#include "scripting_app.hpp"
#include "ui/dialogs/SplashScreen.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/MainLookAndFeel.hpp"
#include "ui/windows/MainWindow.hpp"
#include "version.hpp"

using namespace juce;

namespace {
// Previous std::terminate handler, so we still abort (or chain) after logging.
std::terminate_handler g_previousTerminateHandler = nullptr;

// Logs the active exception's type/message and a stack backtrace before the
// process aborts. Unhandled C++ exceptions thrown inside an event callback
// propagate out through AppKit and call std::terminate, which the crash
// reporter records only as a bare SIGABRT in the message loop with no throw
// site. This handler captures the throw site to magda.log so intermittent
// crashes are self-diagnosing. Must be noexcept and captureless (it is passed
// to std::set_terminate as a plain function pointer).
void magdaTerminateHandler() noexcept {
    juce::String msg;
    msg << "\n=== UNHANDLED EXCEPTION (std::terminate) ===\n";
    if (auto ex = std::current_exception()) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            msg << "Type: std::exception\nwhat(): " << e.what() << "\n";
        } catch (...) {
            msg << "Type: non-std::exception (unknown)\n";
        }
    } else {
        msg << "(no active exception - direct std::terminate call)\n";
    }
    msg << "Backtrace:\n" << juce::SystemStats::getStackBacktrace() << "\n";

    // FileLogger flushes on each write, so the message is on disk before abort.
    juce::Logger::writeToLog(msg);

    if (g_previousTerminateHandler != nullptr)
        g_previousTerminateHandler();
    std::abort();
}
}  // namespace

class MagdaDAWApplication : public JUCEApplication {
  private:
    std::unique_ptr<juce::FileLogger> fileLogger_;
    std::unique_ptr<magda::TracktionEngineWrapper> daw_engine_;
    // Lua-driven MIDI controller scripts (issue #592). Lives in the app
    // layer rather than inside TracktionEngineWrapper so the engine library
    // (magda_daw) doesn't pull magda_scripting into its link line.
    std::unique_ptr<magda::scripting::LuaController> luaController_;
    // Latches true once the deferred startup auto-load has fired. The
    // engine's onMidiDevicesReady callback runs on every device-list
    // change, but we only want to auto-load the active script once.
    bool scriptAutoLoaded_ = false;
    std::unique_ptr<magda::MainWindow> mainWindow_;
    std::unique_ptr<magda::MainLookAndFeel> lookAndFeel_;
    std::unique_ptr<magda::SplashScreen> splashScreen_;

  public:
    /** Convenience accessor used by the free functions in scripting_app.hpp. */
    static MagdaDAWApplication* getMagdaInstance() noexcept {
        return dynamic_cast<MagdaDAWApplication*>(JUCEApplication::getInstance());
    }

    bool reloadActiveLuaScript();
    bool loadLuaScript(const juce::File& file);
    void unloadLuaScript();
    juce::String activeLuaScriptName() const;
    void revealLuaScriptsFolder();

  private:
    // Cancellable deferred init timer — destroyed in shutdown() to prevent
    // callbacks into a partially torn-down application.
    struct InitTimer : public juce::Timer {
        MagdaDAWApplication& app;
        explicit InitTimer(MagdaDAWApplication& a) : app(a) {}
        void timerCallback() override {
            stopTimer();
            app.finishInitialisation();
        }
    };
    std::unique_ptr<InitTimer> initTimer_;
    std::thread modelLoadThread_;
    std::thread sampleTaggerLoadThread_;

  public:
    MagdaDAWApplication() = default;

    const String getApplicationName() override {
        return "MAGDA";
    }
    const String getApplicationVersion() override {
        return MAGDA_VERSION;
    }

    void initialise(const String& commandLine) override {
        // Check if we're being launched as a plugin scanner subprocess
        if (tracktion::PluginManager::startChildProcessPluginScan(commandLine)) {
            // This process is a plugin scanner - it will exit when done
            return;
        }

        // 0. Configurable user-data paths. Two-phase resolution (issue:
        //    custom data dir).
        //    Phase 1: env vars only — Config not yet loaded. Lets MAGDA_DATA_DIR
        //    redirect even before config.json is read. Files created by font
        //    init / look-and-feel / etc. land in the right place.
        magda::paths::resolve();

        // 1. Load Config from its always-OS-default path. config.json itself
        //    cannot move with the data dir override (chicken-and-egg: we
        //    have to read it to know where to redirect to).
        magda::Config::getInstance().load();

        //    Phase 2: re-resolve now that Config has provided any persisted
        //    path overrides.
        magda::paths::resolve();

        // 2. File logger — set up only NOW so it lands in the configured
        //    data dir / Logs. The 4-5 ms of pre-logger init below this
        //    runs without logging, which is fine; nothing of substance
        //    logged there pre-refactor either.
        auto logDir = magda::paths::logsDir();
        if (!logDir.createDirectory()) {
            logDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("MAGDA-Logs");
            logDir.createDirectory();
        }
        fileLogger_ = std::make_unique<juce::FileLogger>(
            logDir.getChildFile("magda.log"), "MAGDA v" + getApplicationVersion(), 1024 * 512);
        juce::Logger::setCurrentLogger(fileLogger_.get());
        juce::Logger::writeToLog("=== MAGDA " + getApplicationVersion() + " starting ===");
        juce::Logger::writeToLog("OS: " + juce::SystemStats::getOperatingSystemName());
        juce::Logger::writeToLog("Command line: " + commandLine);
        juce::Logger::writeToLog(
            "Data dir: " + magda::paths::dataDir().getFullPathName() +
            (magda::paths::dataDirOverriddenByEnv() ? " (MAGDA_DATA_DIR override)" : ""));

        // Install the terminate handler now that the file logger exists, so any
        // unhandled C++ exception logs its throw site + backtrace to magda.log
        // before aborting (otherwise the crash reporter shows only a bare
        // SIGABRT in the message loop with no throw site).
        g_previousTerminateHandler = std::set_terminate(magdaTerminateHandler);

        // Teach the controllers layer how to tell whether the profile system is
        // the active input surface. A loaded Lua script owns the surface and
        // suppresses profile automap/pinned indicators; this provider keeps that
        // knowledge in the app layer so core/controllers needs no scripting dep.
        magda::controllers::setProfileSurfaceActiveProvider(
            [] { return magda::scripting_app::activeLuaScriptName().isEmpty(); });

        // Eagerly create the configured per-user folders so they exist on
        // disk immediately after launch — users expect to find them when
        // they look at the paths shown in Preferences. PresetManager is a
        // singleton whose ctor mkdir's Chains/Racks/Devices; touching it
        // here forces that. LuaScriptStore::ensureExists() does the same
        // for Scripts/Controllers/.
        magda::PresetManager::getInstance();
        magda::PluginPreferences::getInstance();
        magda::scripting::LuaScriptStore{}.ensureExists();

        // 3. Initialize fonts
        magda::FontManager::getInstance().initialize();

        // 4. Set up dark theme
        lookAndFeel_ = std::make_unique<magda::MainLookAndFeel>();
        magda::DarkTheme::applyToLookAndFeel(*lookAndFeel_);
        juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel_.get());

        // 5. Apply HiDPI scale before any window is created.
        // setGlobalScaleFactor must run before TopLevelWindows exist for clean
        // sizing; see juce_TopLevelWindow.cpp.
        const double uiScale = magda::resolveStartupScale();
        juce::Desktop::getInstance().setGlobalScaleFactor(static_cast<float>(uiScale));
        juce::Logger::writeToLog("UI scale: " + juce::String(uiScale, 2) + "x");

        // 2b. Show splash screen
        splashScreen_ = magda::SplashScreen::create();

        // Defer heavy initialization so the message loop can paint the splash.
        // A short timer delay gives macOS time to composite the window.
        initTimer_ = std::make_unique<InitTimer>(*this);
        initTimer_->startTimer(100);
    }

    void finishInitialisation() {
        juce::Logger::writeToLog("finishInitialisation() entered");

        // 3. Initialize audio engine
        daw_engine_ = std::make_unique<magda::TracktionEngineWrapper>();

        // Show plugin scan status on splash screen
        daw_engine_->onPluginScanStatus = [this](const juce::String& status) {
            if (splashScreen_)
                splashScreen_->setStatus(status);
        };

        if (splashScreen_)
            splashScreen_->setStatus("Initializing audio engine...");

        juce::Logger::writeToLog("Calling daw_engine_->initialize()...");
        if (!daw_engine_->initialize()) {
            juce::Logger::writeToLog("ERROR: Failed to initialize Tracktion Engine");
            quit();
            return;
        }

        juce::Logger::writeToLog("Audio engine initialized");

        // 3a. Wire the Lua controller scripts (issue #592). Owned here so
        // magda_daw stays free of magda_scripting symbols. attach() is
        // skipped if the engine has no MidiBridge yet (headless / failure).
        if (auto* bridge = daw_engine_->getMidiBridge()) {
            juce::Logger::writeToLog("[lua-debug] startup: MidiBridge available, "
                                     "constructing LuaController + attaching");
            luaController_ =
                std::make_unique<magda::scripting::LuaController>(daw_engine_->getMagdaApi());
            luaController_->attach(*bridge);
            // Defer the script load until JUCE has opened the MIDI output ports.
            // Loading on_load before that means SysEx sends (DAW-mode handshake,
            // LED priming) get dropped silently and the device behaves as if no
            // script were loaded. Hook fires on first MIDI device-list change
            // after engine init, then on every subsequent change. We only want
            // the auto-load to fire once.
            daw_engine_->onMidiDevicesReady = [this]() {
                if (scriptAutoLoaded_)
                    return;
                scriptAutoLoaded_ = true;
                juce::Logger::writeToLog("[lua-debug] onMidiDevicesReady fired - "
                                         "running deferred reloadActiveLuaScript");
                const bool ok = reloadActiveLuaScript();
                juce::Logger::writeToLog(
                    "[lua-debug] deferred reloadActiveLuaScript -> " +
                    juce::String(ok ? "true" : "false") + " active='" +
                    (luaController_ ? luaController_->currentScriptName() : juce::String{}) + "'");
                if (!ok)
                    juce::Logger::writeToLog("[lua] No controller script loaded");
            };
            juce::MessageManager::callAsync([this]() {
                if (scriptAutoLoaded_ || luaController_ == nullptr)
                    return;
                scriptAutoLoaded_ = true;
                juce::Logger::writeToLog("[lua-debug] startup fallback - "
                                         "running reloadActiveLuaScript");
                const bool ok = reloadActiveLuaScript();
                juce::Logger::writeToLog(
                    "[lua-debug] startup fallback reloadActiveLuaScript -> " +
                    juce::String(ok ? "true" : "false") + " active='" +
                    (luaController_ ? luaController_->currentScriptName() : juce::String{}) + "'");
                if (!ok)
                    juce::Logger::writeToLog("[lua] No controller script loaded");
            });
        } else {
            juce::Logger::writeToLog("[lua-debug] startup: MidiBridge null, "
                                     "LuaController NOT created");
        }

        // 3b. Clean up stale temp media directories from previous sessions
        magda::ProjectManager::cleanupStaleTempDirectories();

        // 4. Create main window with full UI (pass the audio engine)
        juce::Logger::writeToLog("Creating MainWindow...");
        mainWindow_ = std::make_unique<magda::MainWindow>(daw_engine_.get());
        juce::Logger::writeToLog("MainWindow created");

        // 5. Dismiss splash screen
        splashScreen_.reset();

        // 6. Auto-load local model if configured and enabled
        {
            auto& config = magda::Config::getInstance();
            if (config.getLoadModelOnStartup() && !config.getLocalModelPath().empty()) {
                magda::LlamaModelManager::Config modelCfg;
                modelCfg.modelPath = config.getLocalModelPath();
                modelCfg.gpuLayers = config.getLocalLlamaGpuLayers();
                modelCfg.contextSize = config.getLocalLlamaContextSize();
                DBG("Auto-loading local model: " << modelCfg.modelPath);
                modelLoadThread_ = std::thread([modelCfg]() {
                    bool ok = magda::LlamaModelManager::getInstance().loadModel(modelCfg);
                    if (ok) {
                        DBG("Local model loaded successfully");
                    } else {
                        DBG("Failed to load local model");
                    }
                });
            }

            // Sample Tagger preload — same idea but for the media-DB
            // encoders + tokenizer. Background thread keeps startup
            // responsive. The lazy accessors are no-ops when files
            // aren't on disk, so the toggle being on without the bundle
            // installed silently does nothing.
            if (config.getLoadSampleTaggerOnStartup()) {
                DBG("Auto-loading Sample Tagger encoders");
                sampleTaggerLoadThread_ = std::thread(
                    []() { magda::media::MediaDbContext::getInstance().preloadModels(); });
            }
        }

        // Open project file if passed on command line (e.g. double-click .mgd in file manager)
        auto cmdLine = getCommandLineParameters();
        if (cmdLine.isNotEmpty()) {
            auto filePath = cmdLine.unquoted().trim();
            juce::File projectFile(filePath);
            if (projectFile.existsAsFile() && projectFile.hasFileExtension("mgd")) {
                juce::Logger::writeToLog("Opening project from command line: " + filePath);
                mainWindow_->openProjectFile(projectFile);
            }
        }

        juce::Logger::writeToLog("=== MAGDA is ready! ===");

        // Silent GitHub release check. Rate-limited to once per 24h via
        // Config; never blocks startup and only surfaces UI when an update
        // is actually available.
        if (magda::UpdateChecker::shouldAutoCheck()) {
            magda::UpdateChecker::checkAsync([](const magda::UpdateChecker::Result& r) {
                if (!r.success) {
                    juce::Logger::writeToLog("UpdateChecker: " + r.errorMessage);
                    return;
                }
                magda::UpdateChecker::markChecked();
                if (!r.updateAvailable)
                    return;
                juce::AlertWindow::showOkCancelBox(
                    juce::AlertWindow::InfoIcon, "MAGDA " + r.latestVersion + " available",
                    "A new version of MAGDA is available (" + r.latestVersion +
                        "). You're running " + r.currentVersion + ".",
                    "View release", "Later", nullptr,
                    juce::ModalCallbackFunction::create([url = r.releaseUrl](int result) {
                        if (result == 1 && url.isNotEmpty())
                            juce::URL(url).launchInDefaultBrowser();
                    }));
            });
        }
    }

    void shutdown() override {
        initTimer_.reset();
        DBG("=== SHUTDOWN START ===");

        if (modelLoadThread_.joinable())
            modelLoadThread_.join();
        if (sampleTaggerLoadThread_.joinable())
            sampleTaggerLoadThread_.join();

        // Stop timers first to prevent callbacks during destruction
        DBG("[1] ModulatorEngine shutdown...");
        magda::ModulatorEngine::getInstance().shutdown();  // Destroy timer

        // Clear default LookAndFeel BEFORE destroying windows
        // This ensures components switch away from our custom L&F before we delete them
        DBG("[2] Clearing LookAndFeel...");
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);

        // Destroy UI FIRST while all singletons (ClipManager, TrackManager etc.)
        // are still intact. Component destructors trigger removeChildComponent() →
        // repaint() chains that need valid heap state. If singletons are cleared
        // first, freed data + dangling listener references cause heap corruption.
        DBG("[3] Destroying MainWindow...");
        mainWindow_.reset();

        // Tear down the Lua controller before the engine — its dtor
        // unregisters from MidiBridge::removeRawMidiListener.
        DBG("[3b] Destroying LuaController...");
        luaController_.reset();

        // Now shut down singletons — no live UI components reference them
        DBG("[4] TrackManager shutdown...");
        magda::TrackManager::getInstance().shutdown();

        DBG("[5] ClipManager shutdown...");
        magda::ClipManager::getInstance().shutdown();

        DBG("[5b] AudioThumbnailManager shutdown...");
        magda::AudioThumbnailManager::getInstance().shutdown();

        // Now destroy engine
        DBG("[6] Destroying DAW engine...");
        daw_engine_.reset();

        // Destroy our custom LookAndFeel (no components reference it now)
        DBG("[7] Destroying LookAndFeel...");
        lookAndFeel_.reset();

        // Release fonts before JUCE's leak detector runs
        DBG("[8] FontManager shutdown...");
        magda::FontManager::getInstance().shutdown();

        juce::Logger::writeToLog("=== SHUTDOWN COMPLETE ===");
        juce::Logger::setCurrentLogger(nullptr);
        fileLogger_.reset();

        DBG("MAGDA shutdown complete");
        DBG("=== SHUTDOWN END ===");

        // Use _exit() to skip static destructors of loaded plugin dylibs.
        // Some third-party plugins (e.g. "Kick 3") have buggy static destructors
        // that cause heap corruption during normal exit(). Since all our own
        // cleanup is already done above, _exit() is safe here.
        _exit(0);
    }

    void anotherInstanceStarted(const String& commandLine) override {
        // Another instance was launched with a file path (e.g. double-click .mgd while app is
        // running)
        auto filePath = commandLine.unquoted().trim();
        juce::File projectFile(filePath);
        if (projectFile.existsAsFile() && projectFile.hasFileExtension("mgd") && mainWindow_) {
            mainWindow_->openProjectFile(projectFile);
        }
    }

    void systemRequestedQuit() override {
        auto& pm = magda::ProjectManager::getInstance();
        if (pm.isDirty()) {
            if (!pm.showUnsavedChangesDialog())
                return;  // User cancelled
        }
        quit();
    }
};

// -----------------------------------------------------------------------------
// Lua controller scripts — out-of-line method definitions + free functions
// implementing scripting_app.hpp.
// -----------------------------------------------------------------------------

namespace {

magda::scripting_app::LuaScriptPorts getLuaScriptPortsFromConfig(const juce::String& scriptName) {
    magda::scripting_app::LuaScriptPorts ports;
    const auto scripts = magda::Config::getInstance().getLuaScripts();
    if (!scripts.isArray())
        return ports;

    for (const auto& item : *scripts.getArray()) {
        auto* obj = item.getDynamicObject();
        if (obj == nullptr || obj->getProperty("scriptName").toString() != scriptName)
            continue;
        ports.midiOutputPort = obj->getProperty("midiOutputPort").toString();
        ports.dawInputPort = obj->getProperty("dawInputPort").toString();
        return ports;
    }
    return ports;
}

void setLuaScriptPortsInConfig(const juce::String& scriptName,
                               const magda::scripting_app::LuaScriptPorts& ports) {
    juce::Array<juce::var> entries;
    const auto scripts = magda::Config::getInstance().getLuaScripts();
    if (scripts.isArray()) {
        for (const auto& item : *scripts.getArray()) {
            auto* obj = item.getDynamicObject();
            if (obj == nullptr || obj->getProperty("scriptName").toString() != scriptName)
                entries.add(item);
        }
    }

    auto* entry = new juce::DynamicObject();
    entry->setProperty("scriptName", scriptName);
    entry->setProperty("midiOutputPort", ports.midiOutputPort);
    entry->setProperty("dawInputPort", ports.dawInputPort);
    entries.add(juce::var(entry));

    auto& cfg = magda::Config::getInstance();
    cfg.setLuaScripts(juce::var(entries));
    cfg.save();
}

}  // namespace

bool MagdaDAWApplication::reloadActiveLuaScript() {
    if (luaController_ == nullptr) {
        juce::Logger::writeToLog("[lua-debug] reloadActiveLuaScript: luaController_ null");
        return false;
    }

    // If a script is already active, reload that exact file. Otherwise pick
    // the persisted active script, then fall back to the alphabetically-first
    // script so a fresh install still has a sensible default to load.
    auto activeName = luaController_->currentScriptName();
    if (activeName.isEmpty()) {
        const auto persisted = magda::Config::getInstance().getActiveLuaScript();
        activeName = juce::String::fromUTF8(persisted.c_str(), static_cast<int>(persisted.size()));
    }

    // Migration: an existing config with activeLuaScript pointing at a bundled
    // factory script means the user was running it under the pre-gate auto-
    // discovery model. Promote it into enabledFactoryLuaScripts so this
    // reload, and every future enumerate, still includes it.
    if (activeName.isNotEmpty()) {
        auto bundledDir = magda::scripting::LuaScriptStore::findBundledScriptsDirectory();
        if (bundledDir.isDirectory() && bundledDir.getChildFile(activeName).existsAsFile()) {
            auto& cfg = magda::Config::getInstance();
            auto enabled = cfg.getEnabledFactoryLuaScripts();
            const auto persistedName = activeName.toStdString();
            if (std::find(enabled.begin(), enabled.end(), persistedName) == enabled.end()) {
                enabled.push_back(persistedName);
                cfg.setEnabledFactoryLuaScripts(std::move(enabled));
                cfg.save();
            }
        }
    }

    auto scripts = magda::scripting_app::enumerateLuaScripts();
    juce::Logger::writeToLog("[lua-debug] reloadActiveLuaScript: scripts.size=" +
                             juce::String(static_cast<int>(scripts.size())) + " activeName='" +
                             activeName + "'");
    if (scripts.empty()) {
        juce::Logger::writeToLog("[lua-debug] reloadActiveLuaScript: no scripts, unloading");
        luaController_->unloadScript();
        if (auto* audioBridge = daw_engine_->getAudioBridge())
            audioBridge->clearSurfaceOnlyMidiInputPorts();
        return false;
    }

    juce::File target = scripts.front();
    if (activeName.isNotEmpty()) {
        for (const auto& s : scripts) {
            if (s.getFileName() == activeName) {
                target = s;
                break;
            }
        }
    }
    juce::Logger::writeToLog("[lua-debug] reloadActiveLuaScript: target='" + target.getFileName() +
                             "'");
    return loadLuaScript(target);
}

bool MagdaDAWApplication::loadLuaScript(const juce::File& file) {
    if (luaController_ == nullptr)
        return false;

    const auto ports = getLuaScriptPortsFromConfig(file.getFileName());
    luaController_->setDawInputPort(ports.dawInputPort);
    if (auto* audioBridge = daw_engine_->getAudioBridge())
        audioBridge->setSurfaceOnlyMidiInputPort(ports.dawInputPort);
    if (auto* liveApi = dynamic_cast<magda::MagdaApiLive*>(&daw_engine_->getMagdaApi()))
        liveApi->setDefaultMidiOutputPort(ports.midiOutputPort);

    if (luaController_->loadScript(file)) {
        juce::Logger::writeToLog("[lua] Loaded controller script: " + file.getFileName());
        auto& cfg = magda::Config::getInstance();
        cfg.setActiveLuaScript(file.getFileName().toStdString());
        cfg.save();
        return true;
    }
    if (auto* audioBridge = daw_engine_->getAudioBridge())
        audioBridge->clearSurfaceOnlyMidiInputPorts();
    juce::Logger::writeToLog("[lua] Failed to load " + file.getFileName() + ": " +
                             luaController_->lastError());
    return false;
}

void MagdaDAWApplication::unloadLuaScript() {
    if (luaController_ != nullptr)
        luaController_->unloadScript();
    if (daw_engine_ != nullptr) {
        if (auto* audioBridge = daw_engine_->getAudioBridge())
            audioBridge->clearSurfaceOnlyMidiInputPorts();
    }
    auto& cfg = magda::Config::getInstance();
    cfg.setActiveLuaScript(std::string{});
    cfg.save();
}

juce::String MagdaDAWApplication::activeLuaScriptName() const {
    return luaController_ != nullptr ? luaController_->currentScriptName() : juce::String{};
}

void MagdaDAWApplication::revealLuaScriptsFolder() {
    magda::scripting::LuaScriptStore store;
    store.ensureExists();
    store.root().revealToUser();
}

namespace magda::scripting_app {

bool reloadActiveLuaScript() {
    if (auto* app = MagdaDAWApplication::getMagdaInstance())
        return app->reloadActiveLuaScript();
    return false;
}

bool loadLuaScript(const juce::File& file) {
    if (auto* app = MagdaDAWApplication::getMagdaInstance())
        return app->loadLuaScript(file);
    return false;
}

void unloadLuaScript() {
    if (auto* app = MagdaDAWApplication::getMagdaInstance())
        app->unloadLuaScript();
}

juce::String activeLuaScriptName() {
    if (auto* app = MagdaDAWApplication::getMagdaInstance())
        return app->activeLuaScriptName();
    return {};
}

LuaScriptPorts luaScriptPorts(const juce::String& scriptName) {
    return getLuaScriptPortsFromConfig(scriptName);
}

void setLuaScriptPorts(const juce::String& scriptName, const LuaScriptPorts& ports) {
    setLuaScriptPortsInConfig(scriptName, ports);
    if (auto* app = MagdaDAWApplication::getMagdaInstance()) {
        if (app->activeLuaScriptName() == scriptName)
            app->reloadActiveLuaScript();
    }
}

bool hasAnyLuaScripts() {
    return !enumerateLuaScripts().empty();
}

std::vector<juce::File> enumerateLuaScripts() {
    magda::scripting::LuaScriptStore store;
    store.ensureExists();
    auto userScripts = store.enumerate();

    auto bundledDir = magda::scripting::LuaScriptStore::findBundledScriptsDirectory();
    if (!bundledDir.isDirectory())
        return userScripts;

    juce::StringArray userFilenames;
    for (const auto& f : userScripts)
        userFilenames.add(f.getFileName());

    juce::StringArray enabledFactory;
    for (const auto& name : magda::Config::getInstance().getEnabledFactoryLuaScripts())
        enabledFactory.add(juce::String::fromUTF8(name.c_str(), static_cast<int>(name.size())));

    std::vector<juce::File> merged = userScripts;
    auto bundled =
        bundledDir.findChildFiles(juce::File::findFiles, /*searchRecursively*/ false, "*.lua");
    for (auto& f : bundled) {
        // User pool wins on filename collision (matches profile precedence).
        // Factory scripts only appear once explicitly enabled in the dialog.
        if (userFilenames.contains(f.getFileName()))
            continue;
        if (!enabledFactory.contains(f.getFileName()))
            continue;
        merged.push_back(f);
    }

    std::sort(merged.begin(), merged.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareIgnoreCase(b.getFileName()) < 0;
    });
    return merged;
}

std::vector<juce::File> enumerateAvailableFactoryLuaScripts() {
    auto bundledDir = magda::scripting::LuaScriptStore::findBundledScriptsDirectory();
    std::vector<juce::File> out;
    if (!bundledDir.isDirectory())
        return out;

    juce::StringArray enabledFactory;
    for (const auto& name : magda::Config::getInstance().getEnabledFactoryLuaScripts())
        enabledFactory.add(juce::String::fromUTF8(name.c_str(), static_cast<int>(name.size())));

    auto bundled =
        bundledDir.findChildFiles(juce::File::findFiles, /*searchRecursively*/ false, "*.lua");
    for (auto& f : bundled) {
        if (!enabledFactory.contains(f.getFileName()))
            out.push_back(f);
    }
    std::sort(out.begin(), out.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareIgnoreCase(b.getFileName()) < 0;
    });
    return out;
}

bool isFactoryLuaScript(const juce::File& file) {
    auto bundledDir = magda::scripting::LuaScriptStore::findBundledScriptsDirectory();
    if (!bundledDir.isDirectory())
        return false;
    return file.getParentDirectory() == bundledDir;
}

juce::File luaScriptsFolder() {
    magda::scripting::LuaScriptStore store;
    store.ensureExists();
    return store.root();
}

void revealLuaScriptsFolder() {
    if (auto* app = MagdaDAWApplication::getMagdaInstance())
        app->revealLuaScriptsFolder();
}

}  // namespace magda::scripting_app

// JUCE application startup
START_JUCE_APPLICATION(MagdaDAWApplication)
