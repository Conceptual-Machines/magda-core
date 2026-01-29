#include "PanelContentFactory.hpp"

// Include all content implementations for registration
#include "AIChatConsoleContent.hpp"
#include "EmptyContent.hpp"
#include "InspectorContent.hpp"
#include "MediaExplorerContent.hpp"
#include "PianoRollContent.hpp"
#include "PluginBrowserContent.hpp"
#include "PresetBrowserContent.hpp"
#include "ScriptingConsoleContent.hpp"
#include "TrackChainContent.hpp"
#include "WaveformEditorContent.hpp"

namespace magda::daw::ui {

PanelContentFactory& PanelContentFactory::getInstance() {
    static PanelContentFactory instance;
    return instance;
}

PanelContentFactory::PanelContentFactory() {
    registerBuiltinTypes();
}

void PanelContentFactory::registerBuiltinTypes() {
    // Register all built-in content types
    registerContentType(PanelContentType::Empty, []() { return std::make_unique<EmptyContent>(); });

    registerContentType(PanelContentType::PluginBrowser,
                        []() { return std::make_unique<PluginBrowserContent>(); });

    registerContentType(PanelContentType::MediaExplorer,
                        []() { return std::make_unique<MediaExplorerContent>(); });

    registerContentType(PanelContentType::PresetBrowser,
                        []() { return std::make_unique<PresetBrowserContent>(); });

    registerContentType(PanelContentType::Inspector,
                        []() { return std::make_unique<InspectorContent>(); });

    registerContentType(PanelContentType::AIChatConsole,
                        []() { return std::make_unique<AIChatConsoleContent>(); });

    registerContentType(PanelContentType::ScriptingConsole,
                        []() { return std::make_unique<ScriptingConsoleContent>(); });

    registerContentType(PanelContentType::TrackChain,
                        []() { return std::make_unique<TrackChainContent>(); });

    registerContentType(PanelContentType::PianoRoll,
                        []() { return std::make_unique<PianoRollContent>(); });

    registerContentType(PanelContentType::WaveformEditor,
                        []() { return std::make_unique<WaveformEditorContent>(); });
}

void PanelContentFactory::registerContentType(PanelContentType type, ContentCreator creator) {
    creators_[type] = std::move(creator);
}

std::unique_ptr<PanelContent> PanelContentFactory::createContent(PanelContentType type) {
    auto it = creators_.find(type);
    if (it != creators_.end()) {
        return it->second();
    }
    return nullptr;
}

bool PanelContentFactory::isRegistered(PanelContentType type) const {
    return creators_.find(type) != creators_.end();
}

std::vector<PanelContentType> PanelContentFactory::getAvailableTypes() const {
    std::vector<PanelContentType> types;
    types.reserve(creators_.size());
    for (const auto& [type, creator] : creators_) {
        types.push_back(type);
    }
    return types;
}

PanelContentInfo PanelContentFactory::getContentInfo(PanelContentType type) const {
    return PanelContentInfo{type, getContentTypeName(type), getContentTypeName(type),
                            getContentTypeIcon(type)};
}

}  // namespace magda::daw::ui
