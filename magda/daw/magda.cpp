#include "magda.hpp"

#include <iostream>
#include <memory>

#include "engine/TracktionEngineWrapper.hpp"

// Global engine instance
static std::unique_ptr<magda::TracktionEngineWrapper> g_engine;

bool magda_initialize() {
    std::cout << "MAGDA v" << MAGDA_VERSION
              << " - Multi-Agent Interface for Creative Audio" << std::endl;
    std::cout << "Initializing system..." << std::endl;

    try {
        // Initialize Tracktion Engine
        g_engine = std::make_unique<magda::TracktionEngineWrapper>();
        if (!g_engine->initialize()) {
            std::cerr << "ERROR: Failed to initialize Tracktion Engine" << std::endl;
            return false;
        }

        // TODO: Initialize additional systems
        // - WebSocket server setup
        // - Interface registry
        // - Plugin discovery

        std::cout << "MAGDA initialized successfully!" << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "ERROR: MAGDA initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void magda_shutdown() {
    std::cout << "Shutting down MAGDA..." << std::endl;

    try {
        // Shutdown Tracktion Engine
        if (g_engine) {
            g_engine->shutdown();
            g_engine.reset();
        }

        // TODO: Cleanup additional systems
        // - Stop WebSocket server
        // - Cleanup resources
        // - Unload plugins

        std::cout << "MAGDA shutdown complete." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "ERROR: Error during shutdown: " << e.what() << std::endl;
    }
}

// Access to the global engine instance for MCP server
magda::TracktionEngineWrapper* magda_get_engine() {
    return g_engine.get();
}
