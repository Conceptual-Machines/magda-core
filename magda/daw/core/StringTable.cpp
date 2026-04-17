#include "StringTable.hpp"

namespace magda {

StringTable& StringTable::getInstance() {
    static StringTable instance;
    return instance;
}

StringTable::StringTable() {
    auto langDir = findLangDirectory();
    if (langDir.isDirectory()) {
        auto en = langDir.getChildFile("en.json");
        if (en.existsAsFile() && load(en))
            return;
    }
    DBG("StringTable: no lang/en.json found, using key fallback");
}

juce::File StringTable::findLangDirectory() {
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

    // macOS: appFile is the .app bundle itself; resources live inside Contents/Resources.
    juce::Array<juce::File> candidates;
#if JUCE_MAC
    candidates.add(appFile.getChildFile("Contents/Resources/lang"));
#endif
    // Next to the binary (Windows/Linux, and macOS portable layout).
    candidates.add(appFile.getParentDirectory().getChildFile("lang"));

    // Dev-tree fallback: walk up looking for a lang/en.json sibling.
    auto walk = appFile.getParentDirectory();
    for (int i = 0; i < 8 && walk.exists(); ++i) {
        auto maybe = walk.getChildFile("lang");
        if (maybe.getChildFile("en.json").existsAsFile()) {
            candidates.add(maybe);
            break;
        }
        walk = walk.getParentDirectory();
    }

    for (const auto& c : candidates)
        if (c.isDirectory())
            return c;
    return {};
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

    std::unordered_map<juce::String, juce::String> parsedStrings;
    parseObject(parsed, "", parsedStrings);
    if (parsedStrings.empty())
        return false;

    strings_ = std::move(parsedStrings);
    return true;
}

void StringTable::parseObject(const juce::var& obj, const juce::String& prefix,
                              std::unordered_map<juce::String, juce::String>& out) {
    if (auto* dynObj = obj.getDynamicObject()) {
        for (const auto& prop : dynObj->getProperties()) {
            auto key =
                prefix.isEmpty() ? prop.name.toString() : prefix + "." + prop.name.toString();
            if (prop.value.isObject()) {
                parseObject(prop.value, key, out);
            } else {
                out[key] = prop.value.toString();
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
    auto langDir = findLangDirectory();
    if (langDir.isDirectory()) {
        auto file = langDir.getChildFile(languageCode + ".json");
        if (file.existsAsFile() && load(file)) {
            language_ = languageCode;
            return true;
        }
    }
    DBG("StringTable::loadLanguage: no lang/" << languageCode << ".json found");
    return false;
}

}  // namespace magda
