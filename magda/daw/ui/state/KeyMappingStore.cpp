#include "KeyMappingStore.hpp"

#include "core/Config.hpp"

namespace magda {

KeyMappingStore::KeyMappingStore(juce::ApplicationCommandManager& commandManager)
    : commandManager_(commandManager) {
    commandManager_.getKeyMappings()->addChangeListener(this);
}

KeyMappingStore::~KeyMappingStore() {
    commandManager_.getKeyMappings()->removeChangeListener(this);
}

void KeyMappingStore::restore() {
    const auto stored = Config::getInstance().getKeyboardBindings();
    if (!stored.isString())
        return;

    const auto xmlText = stored.toString();
    if (xmlText.isEmpty())
        return;

    if (auto xml = juce::parseXML(xmlText)) {
        // restoreFromXml() resets to defaults then layers the saved diffs on
        // top. Suppress the resulting change callback so we don't re-persist
        // what we just loaded.
        const juce::ScopedValueSetter<bool> guard(suppressPersist_, true);
        commandManager_.getKeyMappings()->restoreFromXml(*xml);
    }
}

void KeyMappingStore::resetToDefaults() {
    {
        const juce::ScopedValueSetter<bool> guard(suppressPersist_, true);
        commandManager_.getKeyMappings()->resetToDefaultMappings();
    }
    // Persist the cleared state explicitly (the callback was suppressed).
    persist();
}

void KeyMappingStore::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (suppressPersist_ || source != commandManager_.getKeyMappings())
        return;
    persist();
}

void KeyMappingStore::persist() {
    auto& config = Config::getInstance();
    // Store only the differences from the defaults (basedOnDefaults=true).
    if (auto xml = commandManager_.getKeyMappings()->createXml(true)) {
        config.setKeyboardBindings(xml->toString());
    } else {
        config.setKeyboardBindings(juce::var());
    }
    config.save();
}

}  // namespace magda
