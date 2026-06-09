#include "MCPServerManager.hpp"

namespace magda {

MCPServerManager& MCPServerManager::getInstance() {
    static MCPServerManager instance;
    return instance;
}

MCPServerManager::MCPServerManager() {
    auto& config = Config::getInstance();
    config.addListener(this);
    reloadFromConfig();
}

MCPServerManager::~MCPServerManager() {
    Config::getInstance().removeListener(this);
    stopAll();
}

MCPClient* MCPServerManager::getServer(const juce::String& name) {
    auto configIt = configs_.find(name);
    if (configIt == configs_.end() || !configIt->second.enabled)
        return nullptr;

    auto& client = clients_[name];
    if (client == nullptr) {
        const auto& cfg = configIt->second;
        juce::StringArray args;
        for (const auto& a : cfg.args)
            args.add(juce::String(a));

        client = std::make_unique<MCPClient>(juce::String(cfg.command), args);
    }

    if (!client->isRunning()) {
        if (!client->start()) {
            DBG("MCPServerManager: failed to start server: " + name);
            clients_.erase(name);
            return nullptr;
        }
    }

    return client.get();
}

bool MCPServerManager::isServerEnabled(const juce::String& name) const {
    auto it = configs_.find(name);
    return it != configs_.end() && it->second.enabled;
}

bool MCPServerManager::isServerRunning(const juce::String& name) const {
    auto it = clients_.find(name);
    return it != clients_.end() && it->second != nullptr && it->second->isRunning();
}

void MCPServerManager::stopAll() {
    clients_.clear();
}

void MCPServerManager::configChanged() {
    reloadFromConfig();
}

void MCPServerManager::reloadFromConfig() {
    auto serverConfigs = Config::getInstance().getMCPServers();

    std::map<juce::String, Config::MCPServerConfig> newConfigs;
    for (auto& cfg : serverConfigs)
        newConfigs[juce::String(cfg.name)] = std::move(cfg);

    for (auto it = clients_.begin(); it != clients_.end();) {
        auto newIt = newConfigs.find(it->first);
        if (newIt == newConfigs.end()) {
            it = clients_.erase(it);
        } else {
            auto oldIt = configs_.find(it->first);
            if (oldIt != configs_.end() && (oldIt->second.command != newIt->second.command ||
                                            oldIt->second.args != newIt->second.args)) {
                it = clients_.erase(it);
            } else {
                ++it;
            }
        }
    }

    configs_ = std::move(newConfigs);
}

}  // namespace magda
