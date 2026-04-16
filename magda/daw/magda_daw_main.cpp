#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include <memory>

#include "../../magda/agents/llama_model_manager.hpp"
#include "../../magda/agents/llm_presets.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/ClipManager.hpp"
#include "core/Config.hpp"
#include "core/ModulatorEngine.hpp"
#include "core/TrackManager.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "project/ProjectManager.hpp"
#include "ui/dialogs/SplashScreen.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/windows/MainWindow.hpp"
#include "version.hpp"

using namespace juce;

class MagdaDAWApplication : public JUCEApplication {
  private:
    std::unique_ptr<juce::FileLogger> fileLogger_;
    std::unique_ptr<magda::TracktionEngineWrapper> daw_engine_;
    std::unique_ptr<magda::MainWindow> mainWindow_;
    std::unique_ptr<juce::LookAndFeel> lookAndFeel_;
    std::unique_ptr<magda::SplashScreen> splashScreen_;

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

        // Set up file logger early so all startup activity is captured
        auto logDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("MAGDA")
                          .getChildFile("Logs");
        if (!logDir.createDirectory()) {
            // Fall back to temp directory if APPDATA is not writable
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

        // 1. Initialize fonts
        magda::FontManager::getInstance().initialize();

        // 2. Set up dark theme
        lookAndFeel_ = std::make_unique<juce::LookAndFeel_V4>();
        magda::DarkTheme::applyToLookAndFeel(*lookAndFeel_);
        juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel_.get());

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
    }

    void shutdown() override {
        initTimer_.reset();
        DBG("=== SHUTDOWN START ===");

        if (modelLoadThread_.joinable())
            modelLoadThread_.join();

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

// JUCE application startup
START_JUCE_APPLICATION(MagdaDAWApplication)
