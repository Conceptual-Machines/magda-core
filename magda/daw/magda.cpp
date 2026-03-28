#include "magda.hpp"

#include <memory>

#include "../../magda/agents/llama_model_manager.hpp"
#include "engine/TracktionEngineWrapper.hpp"

// Global engine instance
static std::unique_ptr<magda::TracktionEngineWrapper> g_engine;

bool magda_initialize() {
    DBG("MAGDA v" << MAGDA_VERSION << " - Multi-Agent Generative Interface for Creative Audio");
    DBG("Initializing system...");

    try {
        // Initialize Tracktion Engine
        g_engine = std::make_unique<magda::TracktionEngineWrapper>();
        if (!g_engine->initialize()) {
            DBG("ERROR: Failed to initialize Tracktion Engine");
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

        // Unload embedded model if loaded
        magda::LlamaModelManager::getInstance().unloadModel();

        DBG("MAGDA shutdown complete.");

    } catch (const std::exception& e) {
        DBG("ERROR: Error during shutdown: " << e.what());
    }
}

// Access to the global engine instance for MCP server
magda::TracktionEngineWrapper* magda_get_engine() {
    return g_engine.get();
}
