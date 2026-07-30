#include "custom_ui/FaustCustomUIRegistry.hpp"

namespace magda::daw::ui {

FaustCustomUIRegistry& FaustCustomUIRegistry::getInstance() {
    // Meyers singleton: thread-safe under C++11+ for the initialization
    // itself; the map's mutation/read still happens on the main thread,
    // which is the contract documented in the header.
    static FaustCustomUIRegistry instance;
    return instance;
}

void FaustCustomUIRegistry::registerView(const juce::String& name, Factory factory) {
    factories_[name] = std::move(factory);
}

std::unique_ptr<FaustCustomView> FaustCustomUIRegistry::create(
    const juce::String& name, magda::daw::audio::IFaustEditorModel& plugin) const {
    if (name.isEmpty())
        return nullptr;
    auto it = factories_.find(name);
    if (it == factories_.end() || !it->second)
        return nullptr;
    return it->second(plugin);
}

}  // namespace magda::daw::ui
