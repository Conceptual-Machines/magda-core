#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include <memory>

#include "audio/AudioThumbnailManager.hpp"
#include "core/ClipManager.hpp"
#include "core/ModulatorEngine.hpp"
#include "core/TrackManager.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "project/ProjectManager.hpp"
#include "ui/dialogs/SplashScreen.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/windows/MainWindow.hpp"

using namespace juce;

class MagdaDAWApplication : public JUCEApplication {
  private:
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

  public:
    MagdaDAWApplication() = default;

    const String getApplicationName() override {
        return "MAGDA";
    }
    const String getApplicationVersion() override {
        return "1.0.0";
    }

    void initialise(const String& commandLine) override {
        // Check if we're being launched as a plugin scanner subprocess
        if (tracktion::PluginManager::startChildProcessPluginScan(commandLine)) {
            // This process is a plugin scanner - it will exit when done
            return;
        }

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
        // 3. Initialize audio engine
        daw_engine_ = std::make_unique<magda::TracktionEngineWrapper>();

        // Show plugin scan status on splash screen
        daw_engine_->onPluginScanStatus = [this](const juce::String& status) {
            if (splashScreen_)
                splashScreen_->setStatus(status);
        };

        if (splashScreen_)
            splashScreen_->setStatus("Initializing audio engine...");

        if (!daw_engine_->initialize()) {
            DBG("ERROR: Failed to initialize Tracktion Engine");
            quit();
            return;
        }

        DBG("Audio engine initialized");

        // 3b. Clean up stale temp media directories from previous sessions
        magda::ProjectManager::cleanupStaleTempDirectories();

        // 4. Create main window with full UI (pass the audio engine)
        mainWindow_ = std::make_unique<magda::MainWindow>(daw_engine_.get());

        // 5. Dismiss splash screen
        splashScreen_.reset();

        DBG("MAGDA is ready!");
    }

    void shutdown() override {
        initTimer_.reset();
        DBG("=== SHUTDOWN START ===");

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

        DBG("MAGDA shutdown complete");
        DBG("=== SHUTDOWN END ===");

        // Use _exit() to skip static destructors of loaded plugin dylibs.
        // Some third-party plugins (e.g. "Kick 3") have buggy static destructors
        // that cause heap corruption during normal exit(). Since all our own
        // cleanup is already done above, _exit() is safe here.
        _exit(0);
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
