#include "simple_agent.hpp"

SimpleAgent::SimpleAgent(const std::string& id, const std::string& name, const std::string& type)
    : id_(id), name_(name), type_(type), running_(false) {
}

SimpleAgent::~SimpleAgent() {
    stop();
}

std::map<std::string, std::string> SimpleAgent::getCapabilities() const {
    return {
        {"version", "1.0"},
        {"description", "A simple example agent for demonstration"},
        {"supports_messages", "true"},
        {"supports_commands", "true"}
    };
}

bool SimpleAgent::start() {
    if (running_.load()) {
        return true; // Already running
    }
    
    running_.store(true);
    juce::Logger::writeToLog("SimpleAgent '" + name_ + "' started");
    
    return true;
}

void SimpleAgent::stop() {
    if (!running_.load()) {
        return; // Already stopped
    }
    
    running_.store(false);
    juce::Logger::writeToLog("SimpleAgent '" + name_ + "' stopped");
}

std::string SimpleAgent::processMessage(const std::string& message) {
    if (!running_.load()) {
        return "";
    }
    
    juce::Logger::writeToLog("SimpleAgent '" + name_ + "' received message: " + message);
    
    // Simple echo response for demonstration
    if (message.find("hello") != std::string::npos) {
        return "Hello from " + name_ + "!";
    } else if (message.find("status") != std::string::npos) {
        return "Agent " + name_ + " is running and ready";
    } else if (message.find("capabilities") != std::string::npos) {
        return "I'm a simple demo agent that can respond to basic messages";
    }
    
    return "Message received: " + message;
}

void SimpleAgent::setMessageCallback(std::function<void(const std::string&, const std::string&)> callback) {
    messageCallback_ = callback;
} 