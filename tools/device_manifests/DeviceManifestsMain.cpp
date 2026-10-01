/**
 * @file DeviceManifestsMain.cpp
 * @brief Writes the parameter manifest of every base-pack device (#2939).
 */

#include <filesystem>
#include <fstream>
#include <iostream>

#include "magda/daw/audio/plugins/DeviceManifests.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: magda_device_manifests <output-directory>\n";
        return 2;
    }

    const std::filesystem::path outputDirectory = argv[1];
    std::filesystem::create_directories(outputDirectory);

    int failures = 0;
    for (const auto& entry : magda::daw::audio::buildBasePackManifests()) {
        const auto id = entry.pluginId.toStdString();
        if (!entry.manifest) {
            std::cout << "skipped " << id << ": " << entry.skipReason.toStdString() << "\n";
            continue;
        }

        std::string error;
        const auto text = magda::sdk::writeManifest(*entry.manifest, error);
        if (!text) {
            std::cerr << "failed " << id << ": " << error << "\n";
            ++failures;
            continue;
        }

        std::ofstream(outputDirectory / (id + ".manifest.json"), std::ios::binary) << *text << "\n";
        std::cout << "wrote " << id << " (" << entry.manifest->parameters.size()
                  << " parameters)\n";
    }
    return failures == 0 ? 0 : 1;
}
