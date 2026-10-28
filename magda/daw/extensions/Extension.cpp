// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "extensions/Extension.hpp"

namespace magda::extensions {

namespace {

struct Registry {
    std::vector<ExtensionFactory> factories;
    std::vector<std::unique_ptr<Extension>> instances;
    std::optional<Edition> edition;
    bool started = false;
};

/** @brief Function-local so registration from other archives' static initialisers is safe. */
Registry& registry() {
    static Registry instance;
    return instance;
}

const Edition& defaultEdition() {
    static const Edition edition;
    return edition;
}

}  // namespace

bool registerExtension(ExtensionFactory factory) {
    if (factory == nullptr)
        return false;
    registry().factories.push_back(factory);
    return true;
}

void createExtensions() {
    auto& reg = registry();
    jassert(reg.instances.empty());
    for (auto factory : reg.factories) {
        auto extension = factory();
        if (extension == nullptr)
            continue;
        if (auto edition = extension->edition()) {
            // Two editions in one build is a packaging mistake, not something to merge.
            jassert(!reg.edition.has_value());
            if (!reg.edition)
                reg.edition = std::move(edition);
        }
        juce::Logger::writeToLog(juce::String("[extensions] Loaded ") + extension->id());
        reg.instances.push_back(std::move(extension));
    }
}

void startExtensions(AppContext& context) {
    auto& reg = registry();
    jassert(!reg.started);
    reg.started = true;
    for (auto& extension : reg.instances)
        extension->onStartup(context);
}

void destroyExtensions() {
    auto& reg = registry();
    if (reg.started)
        for (auto it = reg.instances.rbegin(); it != reg.instances.rend(); ++it)
            (*it)->onShutdown();
    while (!reg.instances.empty())
        reg.instances.pop_back();
    reg.edition.reset();
    reg.started = false;
}

std::vector<juce::String> loadedExtensionIds() {
    std::vector<juce::String> ids;
    for (const auto& extension : registry().instances)
        ids.emplace_back(extension->id());
    return ids;
}

const Edition& currentEdition() {
    const auto& reg = registry();
    return reg.edition ? *reg.edition : defaultEdition();
}

}  // namespace magda::extensions
