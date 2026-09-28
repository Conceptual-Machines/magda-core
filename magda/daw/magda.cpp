#include "magda.hpp"

#include <memory>

#include "core/LLMClientProvider.hpp"
#include "engine/AudioEngine.hpp"

// Global engine instance
namespace {
std::unique_ptr<magda::AudioEngine> g_engine;
}  // namespace

bool magda_initialize() {
    DBG("MAGDA v" << MAGDA_VERSION << " - Multi-Agent Generative Interface for Creative Audio");
    DBG("Initializing system...");

    try {
        // Through the factory, which is where the choice of engine is made
        // (#2551). Naming an implementation is what makes that choice
        // unreachable from whoever took this path.
        g_engine = magda::createDefaultAudioEngine();
        if (!g_engine->initialize()) {
            DBG("ERROR: Failed to initialize the audio engine");
            return false;
        }

        // TODO: Initialize additional systems
        // - WebSocket server setup
        // - Interface registry
        // - Plugin discovery

        DBG("MAGDA initialized successfully!");
        return true;

    } catch (const std::exception& e) {
        DBG("ERROR: MAGDA initialization failed: " << e.what());
        return false;
    }
}

void magda_shutdown() {
    DBG("Shutting down MAGDA...");

    try {
        // Shutdown Tracktion Engine
        if (g_engine) {
            g_engine->shutdown();
            g_engine.reset();
        }

        magda::shutdownLLMClientProvider();

        DBG("MAGDA shutdown complete.");

    } catch (const std::exception& e) {
        DBG("ERROR: Error during shutdown: " << e.what());
    }
}

// Access to the global engine interface for API consumers
magda::AudioEngine* magda_get_engine() {
    return g_engine.get();
}
