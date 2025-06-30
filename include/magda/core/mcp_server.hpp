#pragma once

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <string>
#include <memory>
#include <map>
#include <functional>
#include <thread>
#include <mutex>
#include <vector>
#include "command.hpp"
#include "mcp_server_interface.hpp"

// Forward declarations
class Agent;

/**
 * @brief WebSocket-based Multi-agent Control Protocol Server
 * 
 * The WebSocketMCPServer manages WebSocket connections from multiple agents,
 * handles command routing, and provides real-time communication
 * between agents and the DAW.
 */
class WebSocketMCPServer : public MCPServerInterface {
public:
    using WebSocketServer = websocketpp::server<websocketpp::config::asio>;
    using ConnectionHdl = websocketpp::connection_hdl;
    using CommandHandler = std::function<CommandResponse(const Command&)>;
    
    /**
     * @brief Construct WebSocket MCP server
     * @param port Port to listen on
     */
    explicit WebSocketMCPServer(int port = 8080);
    
    /**
     * @brief Destructor
     */
    ~WebSocketMCPServer() override;
    
    // MCPServerInterface implementation
    bool start() override;
    void stop() override;
    bool isRunning() const override { return running_; }
    void registerCommandHandler(const std::string& command_type, CommandHandler handler) override;
    void broadcastMessage(const std::string& message) override;
    void sendToAgent(const std::string& agent_id, const std::string& message) override;
    std::vector<std::string> getConnectedAgents() const override;
    size_t getAgentCount() const override;
    std::string getServerType() const override { return "WebSocket"; }
    int getPort() const override { return port_; }
    
private:
    // WebSocket event handlers
    void onOpen(ConnectionHdl hdl);
    void onClose(ConnectionHdl hdl);
    void onMessage(ConnectionHdl hdl, WebSocketServer::message_ptr msg);
    
    // Command processing
    void processCommand(ConnectionHdl hdl, const std::string& message);
    CommandResponse executeCommand(const Command& command);
    
    // Agent management
    std::string generateAgentId();
    void registerAgent(ConnectionHdl hdl, const std::string& agent_id);
    void unregisterAgent(ConnectionHdl hdl);
    
    WebSocketServer server_;
    std::thread server_thread_;
    
    int port_;
    bool running_;
    
    // Agent tracking
    std::map<ConnectionHdl, std::string, std::owner_less<ConnectionHdl>> connections_;
    std::map<std::string, ConnectionHdl> agents_;
    
    // Command handlers
    std::map<std::string, CommandHandler> command_handlers_;
    
    mutable std::mutex mutex_;
};

// For backward compatibility, alias the old name
using MCPServer = WebSocketMCPServer; 