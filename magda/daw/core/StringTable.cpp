#include "StringTable.hpp"

namespace magda {

StringTable& StringTable::getInstance() {
    static StringTable instance;
    return instance;
}

StringTable::StringTable() {
    // Try to load the bundled en.json from the app's lang/ directory.
    // Falls back to empty (all keys return themselves).
    auto appDir =
        juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory();

    // Check several locations: next to the binary, in Resources (macOS bundle),
    // and in the source tree (for development).
    for (const auto& candidate :
         {appDir.getChildFile("lang/en.json"), appDir.getChildFile("../Resources/lang/en.json"),
          appDir.getChildFile("../../../../lang/en.json")}) {
        if (candidate.existsAsFile()) {
            if (load(candidate))
                return;
        }
    }

    DBG("StringTable: no lang/en.json found, using key fallback");
}

bool StringTable::load(const juce::File& jsonFile) {
    auto text = jsonFile.loadFileAsString();
    if (text.isEmpty())
        return false;
    bool ok = loadFromString(text);
    if (ok)
        DBG("StringTable: loaded " << strings_.size() << " strings from "
                                   << jsonFile.getFileName());
    return ok;
}

bool StringTable::loadFromString(const juce::String& json) {
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject())
        return false;

    strings_.clear();
    parseObject(parsed, "");
    return !strings_.empty();
}

void StringTable::parseObject(const juce::var& obj, const juce::String& prefix) {
    if (auto* dynObj = obj.getDynamicObject()) {
        for (const auto& prop : dynObj->getProperties()) {
            auto key =
                prefix.isEmpty() ? prop.name.toString() : prefix + "." + prop.name.toString();
            if (prop.value.isObject()) {
                parseObject(prop.value, key);
            } else {
                strings_[key] = prop.value.toString();
            }
        }
    }
}

juce::String StringTable::get(const juce::String& key) const {
    auto it = strings_.find(key);
    if (it != strings_.end())
        return it->second;
    return key;  // Fallback: return the key itself so missing translations are visible
}

bool StringTable::loadLanguage(const juce::String& languageCode) {
    auto appDir =
        juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory();

    auto filename = languageCode + ".json";
    for (const auto& candidate : {appDir.getChildFile("lang").getChildFile(filename),
                                  appDir.getChildFile("../Resources/lang").getChildFile(filename),
                                  appDir.getChildFile("../../../../lang").getChildFile(filename)}) {
        if (candidate.existsAsFile()) {
            if (load(candidate)) {
                language_ = languageCode;
                return true;
            }
        }
    }

    DBG("StringTable::loadLanguage: no lang/" << languageCode << ".json found");
    return false;
}

}  // namespace magda
