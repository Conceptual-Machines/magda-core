#include "command.hpp"
#include <stdexcept>

// Command implementation
Command::Command(const std::string& command_type) : type_(command_type) {}

Command::Command(const juce::var& json) {
    if (!json.hasProperty("command")) {
        throw std::runtime_error("JSON missing 'command' field");
    }
    
    type_ = json["command"].toString().toStdString();
    
    // Parse parameters
    auto* obj = json.getDynamicObject();
    if (obj) {
        for (auto& prop : obj->getProperties()) {
            std::string key = prop.name.toString().toStdString();
            if (key == "command") continue;
            
            auto value = prop.value;
            if (value.isString()) {
                parameters_[key] = value.toString().toStdString();
            } else if (value.isInt()) {
                parameters_[key] = (int)value;
            } else if (value.isDouble()) {
                parameters_[key] = (double)value;
            } else if (value.isBool()) {
                parameters_[key] = (bool)value;
            } else if (value.isArray()) {
                auto* arr = value.getArray();
                if (arr && arr->size() > 0 && (*arr)[0].isDouble()) {
                    std::vector<double> vec;
                    for (int i = 0; i < arr->size(); ++i) {
                        vec.push_back((double)(*arr)[i]);
                    }
                    parameters_[key] = vec;
                }
            }
        }
    }
}

bool Command::hasParameter(const std::string& key) const {
    return parameters_.find(key) != parameters_.end();
}

juce::var Command::toJson() const {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("command", juce::String(type_));
    
    for (const auto& [key, value] : parameters_) {
        std::visit([&](const auto& v) {
            if constexpr (std::is_same_v<decltype(v), const std::string&>) {
                obj->setProperty(juce::Identifier(key), juce::String(v));
            } else if constexpr (std::is_same_v<decltype(v), const int&>) {
                obj->setProperty(juce::Identifier(key), v);
            } else if constexpr (std::is_same_v<decltype(v), const double&>) {
                obj->setProperty(juce::Identifier(key), v);
            } else if constexpr (std::is_same_v<decltype(v), const bool&>) {
                obj->setProperty(juce::Identifier(key), v);
            } else if constexpr (std::is_same_v<decltype(v), const std::vector<double>&>) {
                juce::Array<juce::var> arr;
                for (double d : v) {
                    arr.add(d);
                }
                obj->setProperty(juce::Identifier(key), arr);
            }
        }, value);
    }
    
    return juce::var(obj.get());
}

Command Command::fromJsonString(const std::string& json_str) {
    juce::var json = juce::JSON::parse(json_str);
    return Command(json);
}

std::string Command::toJsonString() const {
    return juce::JSON::toString(toJson()).toStdString();
}

// CommandResponse implementation
CommandResponse::CommandResponse(Status status, const std::string& message) 
    : status_(status), message_(message) {}

juce::var CommandResponse::toJson() const {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    
    switch (status_) {
        case Status::Success:
            obj->setProperty("status", "success");
            break;
        case Status::Error:
            obj->setProperty("status", "error");
            break;
        case Status::Pending:
            obj->setProperty("status", "pending");
            break;
    }
    
    if (!message_.empty()) {
        obj->setProperty("message", juce::String(message_));
    }
    
    if (!data_.isVoid()) {
        obj->setProperty("data", data_);
    }
    
    return juce::var(obj.get());
} 