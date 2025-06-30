#include "magda/core/command.hpp"
#include <stdexcept>

// Command implementation
Command::Command(const std::string& command_type) : type_(command_type) {}

Command::Command(const nlohmann::json& json) {
    if (!json.contains("command")) {
        throw std::runtime_error("JSON missing 'command' field");
    }
    
    type_ = json["command"];
    
    // Parse parameters
    for (auto& [key, value] : json.items()) {
        if (key == "command") continue;
        
        if (value.is_string()) {
            parameters_[key] = value.get<std::string>();
        } else if (value.is_number_integer()) {
            parameters_[key] = value.get<int>();
        } else if (value.is_number_float()) {
            parameters_[key] = value.get<double>();
        } else if (value.is_boolean()) {
            parameters_[key] = value.get<bool>();
        } else if (value.is_array() && value[0].is_number()) {
            parameters_[key] = value.get<std::vector<double>>();
        }
    }
}

bool Command::hasParameter(const std::string& key) const {
    return parameters_.find(key) != parameters_.end();
}

nlohmann::json Command::toJson() const {
    nlohmann::json json;
    json["command"] = type_;
    
    for (const auto& [key, value] : parameters_) {
        std::visit([&](const auto& v) {
            json[key] = v;
        }, value);
    }
    
    return json;
}

Command Command::fromJsonString(const std::string& json_str) {
    nlohmann::json json = nlohmann::json::parse(json_str);
    return Command(json);
}

std::string Command::toJsonString() const {
    return toJson().dump();
}

// CommandResponse implementation
CommandResponse::CommandResponse(Status status, const std::string& message) 
    : status_(status), message_(message) {}

nlohmann::json CommandResponse::toJson() const {
    nlohmann::json json;
    
    switch (status_) {
        case Status::Success:
            json["status"] = "success";
            break;
        case Status::Error:
            json["status"] = "error";
            break;
        case Status::Pending:
            json["status"] = "pending";
            break;
    }
    
    if (!message_.empty()) {
        json["message"] = message_;
    }
    
    if (!data_.empty()) {
        json["data"] = data_;
    }
    
    return json;
} 