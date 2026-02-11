#include "Config.hpp"

#include <fstream>
#include <iostream>

namespace magda {

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

void Config::saveToFile(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file for writing: " << filename << std::endl;
        return;
    }

    // Simple key-value format for now (later could be JSON)
    file << "defaultTimelineLength=" << defaultTimelineLength << std::endl;
    file << "defaultZoomViewDuration=" << defaultZoomViewDuration << std::endl;
    file << "minZoomLevel=" << minZoomLevel << std::endl;
    file << "maxZoomLevel=" << maxZoomLevel << std::endl;
    file << "zoomInSensitivity=" << zoomInSensitivity << std::endl;
    file << "zoomOutSensitivity=" << zoomOutSensitivity << std::endl;
    file << "zoomInSensitivityShift=" << zoomInSensitivityShift << std::endl;
    file << "zoomOutSensitivityShift=" << zoomOutSensitivityShift << std::endl;
    file << "scrollbarOnLeft=" << (scrollbarOnLeft ? 1 : 0) << std::endl;
    // Save custom plugin paths as tab-delimited string (tab cannot appear in paths)
    {
        std::string joined;
        for (size_t i = 0; i < customPluginPaths.size(); ++i) {
            if (i > 0)
                joined += "\t";
            joined += customPluginPaths[i];
        }
        file << "customPluginPaths=" << joined << std::endl;
    }
    file << "renderFolder=" << renderFolder << std::endl;
    file << "preferredAudioDevice=" << preferredAudioDevice << std::endl;
    file << "preferredInputDevice=" << preferredInputDevice << std::endl;
    file << "preferredOutputDevice=" << preferredOutputDevice << std::endl;
    file << "preferredInputChannels=" << preferredInputChannels << std::endl;
    file << "preferredOutputChannels=" << preferredOutputChannels << std::endl;

    file.close();
    std::cout << "Config saved to: " << filename << std::endl;
}

void Config::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "Config file not found, using defaults: " << filename << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        size_t equalPos = line.find('=');
        if (equalPos == std::string::npos)
            continue;

        std::string key = line.substr(0, equalPos);
        std::string value = line.substr(equalPos + 1);

        parseConfigLine(key, value);
    }

    file.close();
    std::cout << "Config loaded from: " << filename << std::endl;
}

void Config::parseConfigLine(const std::string& key, const std::string& value) {
    try {
        // Handle string values
        if (key == "customPluginPaths") {
            customPluginPaths.clear();
            if (!value.empty()) {
                // Support both tab-delimited (current) and semicolon-delimited (legacy)
                char delimiter = (value.find('\t') != std::string::npos) ? '\t' : ';';
                std::string remaining = value;
                while (!remaining.empty()) {
                    auto pos = remaining.find(delimiter);
                    std::string path;
                    if (pos == std::string::npos) {
                        path = remaining;
                        remaining.clear();
                    } else {
                        path = remaining.substr(0, pos);
                        remaining = remaining.substr(pos + 1);
                    }
                    if (!path.empty())
                        customPluginPaths.push_back(path);
                }
            }
            return;
        }
        if (key == "renderFolder") {
            renderFolder = value;
            return;
        }
        if (key == "preferredAudioDevice") {
            preferredAudioDevice = value;
            return;
        }
        if (key == "preferredInputDevice") {
            preferredInputDevice = value;
            return;
        }
        if (key == "preferredOutputDevice") {
            preferredOutputDevice = value;
            return;
        }

        // Handle numeric values
        double numValue = 0.0;
        numValue = std::stod(value);

        if (key == "defaultTimelineLength") {
            defaultTimelineLength = numValue;
        } else if (key == "defaultZoomViewDuration") {
            defaultZoomViewDuration = numValue;
        } else if (key == "minZoomLevel") {
            minZoomLevel = numValue;
        } else if (key == "maxZoomLevel") {
            maxZoomLevel = numValue;
        } else if (key == "zoomInSensitivity") {
            zoomInSensitivity = numValue;
        } else if (key == "zoomOutSensitivity") {
            zoomOutSensitivity = numValue;
        } else if (key == "zoomInSensitivityShift") {
            zoomInSensitivityShift = numValue;
        } else if (key == "zoomOutSensitivityShift") {
            zoomOutSensitivityShift = numValue;
        } else if (key == "scrollbarOnLeft") {
            scrollbarOnLeft = (numValue != 0);
        } else if (key == "preferredInputChannels") {
            preferredInputChannels = static_cast<int>(numValue);
        } else if (key == "preferredOutputChannels") {
            preferredOutputChannels = static_cast<int>(numValue);
        }
        // Skip unknown keys silently
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config value: " << key << "=" << value << " (" << e.what()
                  << ")" << std::endl;
    }
}

}  // namespace magda
