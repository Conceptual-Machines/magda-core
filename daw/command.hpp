#pragma once

#include <string>
#include <map>
#include <variant>
#include <vector>
#include <juce_core/juce_core.h>

/**
 * @brief Represents a command that can be sent through the MCP protocol
 * 
 * Commands are JSON-based messages that agents use to control the DAW.
 * Each command has a type, optional target, and parameters.
 */
class Command {
    public:
        using ParamValue = std::variant<std::string, int, double, bool, std::vector<double>>;
        using Parameters = std::map<std::string, ParamValue>;
        
        /**
         * @brief Construct a command with the given type
         */
        explicit Command(const std::string& command_type);
        
        /**
         * @brief Construct a command from JSON
         */
        explicit Command(const juce::var& json);
        
        /**
         * @brief Get the command type
         */
        const std::string& getType() const { return type_; }
        
        /**
         * @brief Set a parameter
         */
        template<typename T>
        void setParameter(const std::string& key, const T& value) {
            parameters_[key] = value;
        }
        
        /**
         * @brief Get a parameter
         */
        template<typename T>
        T getParameter(const std::string& key) const {
            auto it = parameters_.find(key);
            if (it != parameters_.end()) {
                return std::get<T>(it->second);
            }
            throw std::runtime_error("Parameter not found: " + key);
        }
        
        /**
         * @brief Check if parameter exists
         */
        bool hasParameter(const std::string& key) const;
        
        /**
         * @brief Convert to JSON
         */
        juce::var toJson() const;
        
        /**
         * @brief Create from JSON string
         */
        static Command fromJsonString(const std::string& json_str);
        
        /**
         * @brief Convert to JSON string
         */
        std::string toJsonString() const;
        
    private:
        std::string type_;
        Parameters parameters_;
    };
    
/**
 * @brief Response to a command
 */
class CommandResponse {
public:
    enum class Status {
        Success,
        Error,
        Pending
    };
    
    CommandResponse(Status status, const std::string& message = "");
    
    Status getStatus() const { return status_; }
    const std::string& getMessage() const { return message_; }
    
    void setData(const juce::var& data) { data_ = data; }
    const juce::var& getData() const { return data_; }
    
    juce::var toJson() const;
    
private:
    Status status_;
    std::string message_;
    juce::var data_;
}; 