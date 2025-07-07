#include "agent_manager.hpp"
#include <algorithm>

AgentManager::AgentManager() {
}

AgentManager::~AgentManager() {
    stopAllAgents();
}

bool AgentManager::registerAgent(std::shared_ptr<AgentInterface> agent) {
    if (!agent) {
        return false;
    }

    std::lock_guard<std::mutex> lock(agentsMutex_);
    
    const std::string agentId = agent->getId();
    if (agents_.find(agentId) != agents_.end()) {
        // Agent already registered
        return false;
    }

    // Set up the message callback
    agent->setMessageCallback([this](const std::string& fromAgent, const std::string& message) {
        handleAgentMessage(fromAgent, message);
    });

    agents_[agentId] = agent;
    
    // Start the agent
    if (!agent->start()) {
        agents_.erase(agentId);
        return false;
    }

    return true;
}

bool AgentManager::unregisterAgent(const std::string& agentId) {
    std::lock_guard<std::mutex> lock(agentsMutex_);
    
    auto it = agents_.find(agentId);
    if (it == agents_.end()) {
        return false;
    }

    // Stop the agent
    it->second->stop();
    agents_.erase(it);
    
    return true;
}

std::shared_ptr<AgentInterface> AgentManager::getAgent(const std::string& agentId) {
    std::lock_guard<std::mutex> lock(agentsMutex_);
    
    auto it = agents_.find(agentId);
    if (it != agents_.end()) {
        return it->second;
    }
    
    return nullptr;
}

std::vector<std::shared_ptr<AgentInterface>> AgentManager::getAllAgents() const {
    std::lock_guard<std::mutex> lock(agentsMutex_);
    
    std::vector<std::shared_ptr<AgentInterface>> result;
    result.reserve(agents_.size());
    
    for (const auto& pair : agents_) {
        result.push_back(pair.second);
    }
    
    return result;
}

std::string AgentManager::sendToAgent(const std::string& agentId, const std::string& message) {
    auto agent = getAgent(agentId);
    if (agent && agent->isRunning()) {
        return agent->processMessage(message);
    }
    
    return "";
}

void AgentManager::broadcastMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(agentsMutex_);
    
    for (const auto& pair : agents_) {
        if (pair.second->isRunning()) {
            pair.second->processMessage(message);
        }
    }
}

size_t AgentManager::getAgentCount() const {
    std::lock_guard<std::mutex> lock(agentsMutex_);
    return agents_.size();
}

void AgentManager::startAllAgents() {
    std::lock_guard<std::mutex> lock(agentsMutex_);
    
    for (const auto& pair : agents_) {
        if (!pair.second->isRunning()) {
            pair.second->start();
        }
    }
}

void AgentManager::stopAllAgents() {
    std::lock_guard<std::mutex> lock(agentsMutex_);
    
    for (const auto& pair : agents_) {
        if (pair.second->isRunning()) {
            pair.second->stop();
        }
    }
}

void AgentManager::handleAgentMessage(const std::string& fromAgent, const std::string& message) {
    // For now, just log the message
    // In the future, this could route messages between agents or to the DAW
    juce::Logger::writeToLog("Agent " + fromAgent + " sent message: " + message);
} 