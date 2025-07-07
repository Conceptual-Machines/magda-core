#pragma once

#include <string>
#include <memory>
#include <functional>
#include <map>
#include <juce_core/juce_core.h>

/**
 * @brief Base interface for all AI agents in the Magica DAW
 * 
 * This interface provides a simple, lightweight framework for AI agents
 * that can interact with the DAW without the complexity of gRPC.
 */
class AgentInterface {
public:
    virtual ~AgentInterface() = default;

    /**
     * @brief Get the unique identifier for this agent
     */
    virtual std::string getId() const = 0;

    /**
     * @brief Get the human-readable name of this agent
     */
    virtual std::string getName() const = 0;

    /**
     * @brief Get the type/category of this agent (e.g., "mixer", "composition", "effects")
     */
    virtual std::string getType() const = 0;

    /**
     * @brief Get agent capabilities as key-value pairs
     */
    virtual std::map<std::string, std::string> getCapabilities() const = 0;

    /**
     * @brief Start the agent (called when agent is registered)
     */
    virtual bool start() = 0;

    /**
     * @brief Stop the agent (called when agent is unregistered)
     */
    virtual void stop() = 0;

    /**
     * @brief Check if the agent is currently running
     */
    virtual bool isRunning() const = 0;

    /**
     * @brief Process a message/command from the DAW or other agents
     * @param message The message to process
     * @return Response message (empty if no response needed)
     */
    virtual std::string processMessage(const std::string& message) = 0;

    /**
     * @brief Set callback for sending messages to the DAW or other agents
     * @param callback Function to call when agent wants to send a message
     */
    virtual void setMessageCallback(std::function<void(const std::string& from_agent, const std::string& message)> callback) = 0;

protected:
    /**
     * @brief Send a message to the DAW or other agents
     * @param message The message to send
     */
    void sendMessage(const std::string& message) {
        if (messageCallback_) {
            messageCallback_(getId(), message);
        }
    }

private:
    std::function<void(const std::string&, const std::string&)> messageCallback_;
}; 