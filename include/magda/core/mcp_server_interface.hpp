#pragma once

#include <string>
#include <vector>
#include <functional>
#include "command.hpp"

/**
 * @brief Base interface for MCP (Multi-agent Control Protocol) servers
 * 
 * This interface defines the common functionality that both WebSocket
 * and gRPC server implementations must provide.
 */
class MCPServerInterface {
public:
    using CommandHandler = std::function<CommandResponse(const Command&)>;
    
    virtual ~MCPServerInterface() = default;
    
    /**
     * @brief Start the server
     * @return true if started successfully
     */
    virtual bool start() = 0;
    
    /**
     * @brief Stop the server
     */
    virtual void stop() = 0;
    
    /**
     * @brief Check if server is running
     */
    virtual bool isRunning() const = 0;
    
    /**
     * @brief Register a command handler
     * @param command_type The command type to handle
     * @param handler Function to handle the command
     */
    virtual void registerCommandHandler(const std::string& command_type, CommandHandler handler) = 0;
    
    /**
     * @brief Send a message to all connected agents
     * @param message JSON message to broadcast
     */
    virtual void broadcastMessage(const std::string& message) = 0;
    
    /**
     * @brief Send a message to a specific agent
     * @param agent_id The agent ID
     * @param message JSON message to send
     */
    virtual void sendToAgent(const std::string& agent_id, const std::string& message) = 0;
    
    /**
     * @brief Get list of connected agent IDs
     */
    virtual std::vector<std::string> getConnectedAgents() const = 0;
    
    /**
     * @brief Get number of connected agents
     */
    virtual size_t getAgentCount() const = 0;
    
    /**
     * @brief Get server type identifier
     */
    virtual std::string getServerType() const = 0;
    
    /**
     * @brief Get server port
     */
    virtual int getPort() const = 0;
}; 