#include <memory>
#include <iostream>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/tracktion_engine_wrapper.hpp"

using namespace juce;

class MagicaDAWApplication : public JUCEApplication {
private:
    std::unique_ptr<magica::TracktionEngineWrapper> daw_engine_;
    
public:
    MagicaDAWApplication() = default;

    const String getApplicationName() override { return "Magica DAW"; }
    const String getApplicationVersion() override { return "1.0.0"; }
    
    void initialise(const String& commandLine) override {
        // 1. Initialize audio engine
        daw_engine_ = std::make_unique<magica::TracktionEngineWrapper>();
        if (!daw_engine_->initialize()) {
            std::cerr << "ERROR: Failed to initialize Tracktion Engine" << std::endl;
            quit();
            return;
        }
        
        std::cout << "✓ Audio engine initialized" << std::endl;
        
        // 2. Create a simple window
        auto window = std::make_unique<DocumentWindow>(
            "Magica DAW",
            Colours::darkgrey,
            DocumentWindow::allButtons
        );
        
        window->setVisible(true);
        window->setResizable(true, true);
        window->setSize(800, 600);
        
        // Keep the window alive
        mainWindow = std::move(window);
        
        std::cout << "🎵 Magica DAW is ready!" << std::endl;
    }
    
    void shutdown() override {
        // Graceful shutdown
        mainWindow.reset();
        daw_engine_.reset();
        
        std::cout << "👋 Magica DAW shutdown complete" << std::endl;
    }
    
    void systemRequestedQuit() override {
        quit();
    }
    
private:
    std::unique_ptr<DocumentWindow> mainWindow;
};

// JUCE application startup
START_JUCE_APPLICATION(MagicaDAWApplication) 