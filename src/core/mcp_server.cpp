#include "magda/core/mcp_server.hpp"
#include <random>
#include <sstream>
#include <iostream>

WebSocketMCPServer::WebSocketMCPServer(int port) : port_(port), running_(false) {
    // Set up server
    server_.set_access_channels(websocketpp::log::alevel::all);
    server_.clear_access_channels(websocketpp::log::alevel::frame_payload);
    
    server_.init_asio();
    
    // Set up handlers
    server_.set_open_handler([this](ConnectionHdl hdl) { onOpen(hdl); });
    server_.set_close_handler([this](ConnectionHdl hdl) { onClose(hdl); });
    server_.set_message_handler([this](ConnectionHdl hdl, WebSocketServer::message_ptr msg) { 
        onMessage(hdl, msg); 
    });
}

WebSocketMCPServer::~WebSocketMCPServer() {
    stop();
}

bool WebSocketMCPServer::start() {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (running_) {
            return true;
        }
        
        server_.listen(port_);
        server_.start_accept();
        
        // Run server in a separate thread
        server_thread_ = std::thread([this]() {
            try {
                server_.run();
            } catch (const std::exception& e) {
                std::cerr << "Server error: " << e.what() << std::endl;
            }
        });
        
        running_ = true;
        std::cout << "WebSocket MCP Server started on port " << port_ << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to start server: " << e.what() << std::endl;
        return false;
    }
}

void WebSocketMCPServer::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!running_) {
        return;
    }
    
    try {
        server_.stop();
        
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        
        running_ = false;
        connections_.clear();
        agents_.clear();
        
        std::cout << "WebSocket MCP Server stopped" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error stopping server: " << e.what() << std::endl;
    }
}

void WebSocketMCPServer::registerCommandHandler(const std::string& command_type, CommandHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    command_handlers_[command_type] = handler;
    std::cout << "Registered handler for command: " << command_type << std::endl;
}

void WebSocketMCPServer::broadcastMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& [hdl, agent_id] : connections_) {
        try {
            server_.send(hdl, message, websocketpp::frame::opcode::text);
        } catch (const std::exception& e) {
            std::cerr << "Failed to send message to agent " << agent_id << ": " << e.what() << std::endl;
        }
    }
}

void WebSocketMCPServer::sendToAgent(const std::string& agent_id, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = agents_.find(agent_id);
    if (it != agents_.end()) {
        try {
            server_.send(it->second, message, websocketpp::frame::opcode::text);
        } catch (const std::exception& e) {
            std::cerr << "Failed to send message to agent " << agent_id << ": " << e.what() << std::endl;
        }
    }
}

std::vector<std::string> WebSocketMCPServer::getConnectedAgents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> agent_ids;
    for (const auto& [agent_id, hdl] : agents_) {
        agent_ids.push_back(agent_id);
    }
    return agent_ids;
}

size_t WebSocketMCPServer::getAgentCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return agents_.size();
}

void WebSocketMCPServer::onOpen(ConnectionHdl hdl) {
    std::string agent_id = generateAgentId();
    registerAgent(hdl, agent_id);
    
    std::cout << "Agent connected: " << agent_id << std::endl;
    
    // Send welcome message
    nlohmann::json welcome = {
        {"type", "welcome"},
        {"agent_id", agent_id},
        {"server_version", "0.1.0"}
    };
    
    try {
        server_.send(hdl, welcome.dump(), websocketpp::frame::opcode::text);
    } catch (const std::exception& e) {
        std::cerr << "Failed to send welcome message: " << e.what() << std::endl;
    }
}

void WebSocketMCPServer::onClose(ConnectionHdl hdl) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = connections_.find(hdl);
    if (it != connections_.end()) {
        std::string agent_id = it->second;
        std::cout << "Agent disconnected: " << agent_id << std::endl;
        
        agents_.erase(agent_id);
        connections_.erase(it);
    }
}

void WebSocketMCPServer::onMessage(ConnectionHdl hdl, WebSocketServer::message_ptr msg) {
    try {
        processCommand(hdl, msg->get_payload());
    } catch (const std::exception& e) {
        std::cerr << "Error processing message: " << e.what() << std::endl;
        
        // Send error response
        CommandResponse error_response(CommandResponse::Status::Error, e.what());
        try {
            server_.send(hdl, error_response.toJson().dump(), websocketpp::frame::opcode::text);
        } catch (const std::exception& send_error) {
            std::cerr << "Failed to send error response: " << send_error.what() << std::endl;
        }
    }
}

void WebSocketMCPServer::processCommand(ConnectionHdl hdl, const std::string& message) {
    Command command = Command::fromJsonString(message);
    CommandResponse response = executeCommand(command);
    
    try {
        server_.send(hdl, response.toJson().dump(), websocketpp::frame::opcode::text);
    } catch (const std::exception& e) {
        std::cerr << "Failed to send response: " << e.what() << std::endl;
    }
}

CommandResponse WebSocketMCPServer::executeCommand(const Command& command) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = command_handlers_.find(command.getType());
    if (it != command_handlers_.end()) {
        try {
            return it->second(command);
        } catch (const std::exception& e) {
            return CommandResponse(CommandResponse::Status::Error, 
                                 "Command execution failed: " + std::string(e.what()));
        }
    }
    
    return CommandResponse(CommandResponse::Status::Error, 
                         "Unknown command: " + command.getType());
}

std::string WebSocketMCPServer::generateAgentId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    std::stringstream ss;
    ss << "agent_" << dis(gen);
    return ss.str();
}

void WebSocketMCPServer::registerAgent(ConnectionHdl hdl, const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_[hdl] = agent_id;
    agents_[agent_id] = hdl;
} 