#include "CuratedAliasLoader.hpp"

#include <BinaryData.h>

#include "AliasRegistry.hpp"

namespace magda {

namespace {

/**
 * @brief Parse a single per-plugin alias file and add its entries to 'out'.
 *
 * @param pluginKey  Canonical plugin key (e.g. "eq") from the index.
 * @param fileJson   Full JSON text of the per-plugin file.
 * @param out        Map to receive the parsed StoredAlias entries.
 */
static void parsePluginFile(const juce::String& pluginKey, const juce::String& fileJson,
                            std::map<juce::String, StoredAlias>& out) {
    juce::var parsed;
    if (juce::JSON::parse(fileJson, parsed).failed())
        return;

    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return;

    // Read aliasesByName lookup table (alias key -> array of param names).
    std::map<juce::String, juce::String> primaryNames;  // alias key -> first name in list
    auto byNameVar = root->getProperty("aliasesByName");
    if (auto* byNameObj = byNameVar.getDynamicObject()) {
        for (const auto& prop : byNameObj->getProperties()) {
            const juce::String aliasKey = prop.name.toString();
            if (prop.value.isArray() && prop.value.getArray()->size() > 0)
                primaryNames[aliasKey] = (*prop.value.getArray())[0].toString();
        }
    }

    // Read "aliases" object.
    auto aliasesVar = root->getProperty("aliases");
    auto* aliasesObj = aliasesVar.getDynamicObject();
    if (aliasesObj == nullptr)
        return;

    for (const auto& prop : aliasesObj->getProperties()) {
        const juce::String aliasKey = prop.name.toString();
        auto* entryObj = prop.value.getDynamicObject();
        if (entryObj == nullptr)
            continue;

        const int paramIndex = static_cast<int>(entryObj->getProperty("paramIndex"));

        StoredAlias alias;
        alias.pluginTypeKey = pluginKey;
        alias.paramIndex = paramIndex;

        // Use first aliasesByName entry as drift-fallback name.
        auto nameIt = primaryNames.find(aliasKey);
        if (nameIt != primaryNames.end())
            alias.paramNameAtSetTime = nameIt->second;

        // No concrete path for curated aliases -- resolved at runtime.
        alias.path = std::nullopt;

        // Canonical alias name is "{pluginKey}.{aliasKey}"
        juce::String canonicalName = pluginKey + "." + aliasKey;
        out[canonicalName] = alias;
    }
}

}  // namespace

// ============================================================================
// loadFromBinary
// ============================================================================

void CuratedAliasLoader::loadFromBinary() {
    // Resolve embedded binary data files by name.
    auto binaryResolver = [](const juce::String& filename) -> juce::String {
        int dataSize = 0;
        const char* data = BinaryData::getNamedResource(filename.toRawUTF8(), dataSize);
        if (data == nullptr || dataSize <= 0)
            return {};
        return juce::String::fromUTF8(data, dataSize);
    };

    // Load curated_index.json from BinaryData.
    int indexSize = 0;
    const char* indexData = BinaryData::getNamedResource("curated_index_json", indexSize);
    if (indexData == nullptr || indexSize <= 0)
        return;

    juce::String indexJson = juce::String::fromUTF8(indexData, indexSize);
    loadFromString(indexJson, binaryResolver);
}

// ============================================================================
// loadFromString
// ============================================================================

void CuratedAliasLoader::loadFromString(
    const juce::String& indexJson,
    const std::function<juce::String(const juce::String& filename)>& fileResolver) {
    juce::var indexParsed;
    if (juce::JSON::parse(indexJson, indexParsed).failed())
        return;

    auto* indexRoot = indexParsed.getDynamicObject();
    if (indexRoot == nullptr)
        return;

    auto pluginsVar = indexRoot->getProperty("plugins");
    if (!pluginsVar.isArray())
        return;

    std::map<juce::String, StoredAlias> allEntries;

    for (const auto& pluginEntry : *pluginsVar.getArray()) {
        auto* entryObj = pluginEntry.getDynamicObject();
        if (entryObj == nullptr)
            continue;

        const juce::String pluginKey = entryObj->getProperty("key").toString();
        const juce::String fileName = entryObj->getProperty("file").toString();

        if (pluginKey.isEmpty() || fileName.isEmpty())
            continue;

        // Convert filename to BinaryData-compatible resource name.
        // juce_add_binary_data replaces '.' with '_' and '-' with '_' in resource names.
        juce::String resourceName = fileName.replaceCharacter('.', '_').replaceCharacter('-', '_');
        juce::String fileContent = fileResolver(resourceName);

        if (fileContent.isEmpty())
            continue;

        parsePluginFile(pluginKey, fileContent, allEntries);
    }

    // Atomically replace the Curated layer.
    AliasRegistry::getInstance().replaceLayer(AliasLayer::Curated, allEntries);
}

}  // namespace magda
