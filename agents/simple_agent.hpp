#pragma once

#include "agent_interface.hpp"
#include <atomic>
#include <juce_core/juce_core.h>

/**
 * @brief A simple example agent implementation
 * 
 * This demonstrates how to create a basic AI agent that can interact
 * with the Magica DAW system.
 */
class SimpleAgent : public AgentInterface {
public:
    /**
     * @brief Construct a simple agent
     * @param id Unique identifier for the agent
     * @param name Human-readable name
     * @param type Agent type/category
     */
    SimpleAgent(const std::string& id, const std::string& name, const std::string& type);
    
    ~SimpleAgent() override;

    // AgentInterface implementation
    std::string getId() const override { return id_; }
    std::string getName() const override { return name_; }
    std::string getType() const override { return type_; }
    std::map<std::string, std::string> getCapabilities() const override;
    
    bool start() override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }
    
    std::string processMessage(const std::string& message) override;
    void setMessageCallback(std::function<void(const std::string&, const std::string&)> callback) override;

private:
    std::string id_;
    std::string name_;
    std::string type_;
    std::atomic<bool> running_;
    std::function<void(const std::string&, const std::string&)> messageCallback_;
}; 