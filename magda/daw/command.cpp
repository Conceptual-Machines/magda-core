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
            if (key == "command")
                continue;

            ParamValue parsed_value;
            if (parseParameterValue(prop.value, parsed_value)) {
                parameters_[key] = std::move(parsed_value);
            }
        }
    }
}

bool Command::parseParameterValue(const juce::var& value, ParamValue& output) {
    if (value.isString()) {
        output = value.toString().toStdString();
        return true;
    }
    if (value.isInt()) {
        output = static_cast<int>(value);
        return true;
    }
    if (value.isDouble()) {
        output = static_cast<double>(value);
        return true;
    }
    if (value.isBool()) {
        output = static_cast<bool>(value);
        return true;
    }
    if (value.isArray()) {
        auto* arr = value.getArray();
        if (arr && arr->size() > 0 && (*arr)[0].isDouble()) {
            std::vector<double> vec;
            vec.reserve(arr->size());
            for (int i = 0; i < arr->size(); ++i) {
                vec.push_back(static_cast<double>((*arr)[i]));
            }
            output = std::move(vec);
            return true;
        }
    }
    return false;
}

bool Command::hasParameter(const std::string& key) const {
    return parameters_.find(key) != parameters_.end();
}

juce::var Command::toJson() const {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("command", juce::String(type_));

    for (const auto& [key, value] : parameters_) {
        std::string keyStr = key;  // Copy the key to avoid capture issues
        std::visit(
            [&obj, keyStr](const auto& v) {
                if constexpr (std::is_same_v<decltype(v), const std::string&>) {
                    obj->setProperty(juce::Identifier(keyStr), juce::String(v));
                } else if constexpr (std::is_same_v<decltype(v), const int&>) {
                    obj->setProperty(juce::Identifier(keyStr), v);
                } else if constexpr (std::is_same_v<decltype(v), const double&>) {
                    obj->setProperty(juce::Identifier(keyStr), v);
                } else if constexpr (std::is_same_v<decltype(v), const bool&>) {
                    obj->setProperty(juce::Identifier(keyStr), v);
                } else if constexpr (std::is_same_v<decltype(v), const std::vector<double>&>) {
                    juce::Array<juce::var> arr;
                    for (double d : v) {
                        arr.add(d);
                    }
                    obj->setProperty(juce::Identifier(keyStr), arr);
                }
            },
            value);
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
