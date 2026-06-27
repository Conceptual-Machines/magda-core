#include "slot/DeviceSlotAnalyzerContextActions.hpp"

#include "audio/AudioBridge.hpp"
#include "audio/plugins/OscilloscopePlugin.hpp"
#include "audio/plugins/SpectrumAnalyzerPlugin.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "custom_ui/AnalyzerWindow.hpp"
#include "custom_ui/OscilloscopeUI.hpp"
#include "custom_ui/SpectrumAnalyzerUI.hpp"
#include "engine/AudioEngine.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

void toggleDeviceSlotAnalyzerWindow(std::unique_ptr<AnalyzerWindow>& analyzerWindow,
                                    const magda::DeviceInfo& device,
                                    const magda::ChainNodePath& nodePath,
                                    magda::SvgButton* uiButton) {
    if (analyzerWindow != nullptr) {
        const bool show = !analyzerWindow->isVisible();
        analyzerWindow->setVisible(show);
        if (show)
            analyzerWindow->toFront(true);
        if (uiButton != nullptr) {
            uiButton->setToggleState(show, juce::dontSendNotification);
            uiButton->setActive(show);
        }
        return;
    }

    auto* engine = magda::TrackManager::getInstance().getAudioEngine();
    auto* bridge = engine != nullptr ? engine->getAudioBridge() : nullptr;
    if (bridge == nullptr)
        return;

    auto plugin = bridge->getPlugin(nodePath);
    std::unique_ptr<juce::Component> content;
    if (auto* scope = dynamic_cast<daw::audio::OscilloscopePlugin*>(plugin.get())) {
        auto ui = std::make_unique<OscilloscopeUI>();
        ui->setPlugin(scope);
        content = std::move(ui);
    } else if (auto* spec = dynamic_cast<daw::audio::SpectrumAnalyzerPlugin*>(plugin.get())) {
        auto ui = std::make_unique<SpectrumAnalyzerUI>();
        ui->setPlugin(spec);
        ui->setTrackId(nodePath.trackId);  // enables the masking overlay in the external window
        content = std::move(ui);
    }

    if (content == nullptr)
        return;

    analyzerWindow = std::make_unique<AnalyzerWindow>(device.name, std::move(content));
    if (uiButton != nullptr) {
        uiButton->setToggleState(true, juce::dontSendNotification);
        uiButton->setActive(true);
    }
}

void showDeviceSlotContextMenu(juce::Component& owner, const magda::ChainNodePath& nodePath,
                               const std::function<void()>& onDeviceDeleted) {
    juce::PopupMenu menu;
    auto& selection = magda::SelectionManager::getInstance();
    const bool hasMultiSelection =
        selection.isChainNodeSelected(nodePath) && selection.getSelectedChainNodes().size() > 1;
    menu.addItem(1, hasMultiSelection ? "Add Selection to New Rack" : "Add to New Rack");

    menu.addSeparator();
    menu.addItem(100, "Delete");

    auto safeOwner = juce::Component::SafePointer<juce::Component>(&owner);
    auto path = nodePath;
    auto callback = onDeviceDeleted;
    auto selectedPaths = hasMultiSelection ? selection.getSelectedChainNodes()
                                           : std::vector<magda::ChainNodePath>{path};

    menu.showMenuAsync(
        juce::PopupMenu::Options(), [safeOwner, path, callback, selectedPaths](int result) {
            if (safeOwner == nullptr || result == 0)
                return;

            if (result == 1) {
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::WrapChainElementsInRackCommand>(selectedPaths));
            } else if (result == 100) {
                juce::MessageManager::callAsync([path, callback]() {
                    if (path.topLevelDeviceId != magda::INVALID_DEVICE_ID) {
                        magda::UndoManager::getInstance().executeCommand(
                            std::make_unique<magda::RemoveDeviceFromTrackCommand>(
                                path.trackId, path.topLevelDeviceId));
                    } else {
                        magda::TrackManager::getInstance().removeDeviceFromChainByPath(path);
                    }
                    if (callback)
                        callback();
                });
            }
        });
}

}  // namespace magda::daw::ui
