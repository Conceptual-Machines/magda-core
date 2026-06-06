#pragma once

#include <juce_core/juce_core.h>

#include "../../daw/core/Config.hpp"
#include "../mcp/MCPClient.hpp"

namespace magda {

class MCPServerManager : public ConfigListener {
  public:
    static MCPServerManager& getInstance();

    MCPClient* getServer(const juce::String& name);

    // Read-only status queries (no side effects — unlike getServer, these
    // never spawn the server). isServerEnabled reflects config; isServerRunning
    // is true only once a client has actually been started and handshaked.
    bool isServerEnabled(const juce::String& name) const;
    bool isServerRunning(const juce::String& name) const;

    void stopAll();

    void configChanged() override;

    ~MCPServerManager() override;

  private:
    MCPServerManager();

    void reloadFromConfig();
    std::map<juce::String, std::unique_ptr<MCPClient>> clients_;
    std::map<juce::String, Config::MCPServerConfig> configs_;
};

}  // namespace magda
