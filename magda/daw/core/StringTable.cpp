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
        if (en.existsAsFile() && load(en)) {
            DBG("StringTable: loaded en.json from " << langDir.getFullPathName());
            return;
        }
    }
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    DBG("StringTable: no lang/en.json found near " << appFile.getFullPathName()
                                                   << ", using key fallback");
}

juce::File StringTable::findLangDirectory() {
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

    // macOS: appFile is the .app bundle itself; resources live inside Contents/Resources.
    juce::Array<juce::File> candidates;
#if JUCE_MAC
    candidates.add(appFile.getChildFile("Contents/Resources/lang"));
#endif
#if JUCE_LINUX
    // Under an AppImage, JUCE's currentApplicationFile returns the outer
    // .AppImage launcher path (via argv[0]/dladdr), so `lang` ends up looked
    // for next to the launcher instead of inside the mount. /proc/self/exe
    // always resolves to the real binary inside the AppImage mount.
    // Linux-only: BSD would need /proc/curproc/file and procfs isn't guaranteed.
    if (auto real = juce::File("/proc/self/exe").getLinkedTarget(); real.exists())
        candidates.add(real.getParentDirectory().getChildFile("lang"));
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
        DBG("StringTable: loaded " << localized_.size() << " strings from "
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

    // Treat the first successful load as the English master (the constructor
    // loads en.json this way). Subsequent loadLanguage() calls overwrite only
    // the localized_ map, keeping english_ as the fallback source.
    if (english_.empty())
        english_ = parsedStrings;
    localized_ = std::move(parsedStrings);
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
    // Prefer the active locale, then fall back to English, then finally to the
    // key itself. That way missing translations stay readable as English rather
    // than surfacing the raw dotted key to users.
    if (auto it = localized_.find(key); it != localized_.end())
        return it->second;
    if (auto it = english_.find(key); it != english_.end())
        return it->second;
    return key;
}

bool StringTable::loadLanguage(const juce::String& languageCode) {
    if (languageCode == "en") {
        // Reset to English: alias localized_ onto the master table. Returns
        // false if the English master never loaded (e.g. packaging error),
        // so callers can surface the failure rather than silently using an
        // empty string table.
        if (english_.empty()) {
            DBG("StringTable::loadLanguage(\"en\"): english_ is empty — en.json not loaded");
            return false;
        }
        localized_ = english_;
        language_ = "en";
        return true;
    }

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
