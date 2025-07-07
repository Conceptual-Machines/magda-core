#pragma once

#include "agent_interface.hpp"
#include <vector>
#include <memory>
#include <mutex>
#include <juce_core/juce_core.h>

/**
 * @brief Manages all AI agents in the Magica DAW
 * 
 * The AgentManager coordinates communication between agents and the DAW,
 * handles agent lifecycle, and provides a simple message routing system.
 */
class AgentManager {
public:
    AgentManager();
    ~AgentManager();

    /**
     * @brief Register a new agent
     * @param agent The agent to register
     * @return true if successful, false otherwise
     */
    bool registerAgent(std::shared_ptr<AgentInterface> agent);

    /**
     * @brief Unregister an agent
     * @param agentId The ID of the agent to unregister
     * @return true if successful, false otherwise
     */
    bool unregisterAgent(const std::string& agentId);

    /**
     * @brief Get an agent by ID
     * @param agentId The ID of the agent
     * @return The agent, or nullptr if not found
     */
    std::shared_ptr<AgentInterface> getAgent(const std::string& agentId);

    /**
     * @brief Get all registered agents
     * @return Vector of all registered agents
     */
    std::vector<std::shared_ptr<AgentInterface>> getAllAgents() const;

    /**
     * @brief Send a message to a specific agent
     * @param agentId The ID of the target agent
     * @param message The message to send
     * @return The agent's response (empty if no response)
     */
    std::string sendToAgent(const std::string& agentId, const std::string& message);

    /**
     * @brief Broadcast a message to all agents
     * @param message The message to broadcast
     */
    void broadcastMessage(const std::string& message);

    /**
     * @brief Get the number of registered agents
     */
    size_t getAgentCount() const;

    /**
     * @brief Start all agents
     */
    void startAllAgents();

    /**
     * @brief Stop all agents
     */
    void stopAllAgents();

private:
    /**
     * @brief Handle messages from agents
     * @param fromAgent The ID of the agent sending the message
     * @param message The message content
     */
    void handleAgentMessage(const std::string& fromAgent, const std::string& message);

    mutable std::mutex agentsMutex_;
    std::map<std::string, std::shared_ptr<AgentInterface>> agents_;
}; 