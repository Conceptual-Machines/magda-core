#include "BottomPanel.hpp"

#include "AudioBridge.hpp"
#include "AudioEngine.hpp"
#include "PanelTabBar.hpp"
#include "audio/DrumGridPlugin.hpp"
#include "state/PanelController.hpp"

namespace magda {

//==============================================================================
// EditorTabBar - subclass to capture tab changes
//==============================================================================
class BottomPanel::EditorTabBar : public juce::TabbedButtonBar {
  public:
    EditorTabBar(BottomPanel& owner)
        : juce::TabbedButtonBar(juce::TabbedButtonBar::TabsAtTop), owner_(owner) {}

    void currentTabChanged(int newTabIndex, const juce::String& /*newTabName*/) override {
        owner_.onEditorTabChanged(newTabIndex);
    }

  private:
    BottomPanel& owner_;
};

namespace {
namespace te = tracktion::engine;

daw::audio::DrumGridPlugin* findDrumGridOnTrack(TrackId trackId) {
    auto* audioEngine = TrackManager::getInstance().getAudioEngine();
    if (!audioEngine)
        return nullptr;
    auto* bridge = audioEngine->getAudioBridge();
    if (!bridge)
        return nullptr;
    auto* teTrack = bridge->getAudioTrack(trackId);
    if (!teTrack)
        return nullptr;

    for (auto* plugin : teTrack->pluginList) {
        if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(plugin))
            return dg;
        if (auto* rackInstance = dynamic_cast<te::RackInstance*>(plugin)) {
            if (rackInstance->type != nullptr) {
                for (auto* innerPlugin : rackInstance->type->getPlugins()) {
                    if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(innerPlugin))
                        return dg;
                }
            }
        }
    }
    return nullptr;
}
}  // namespace

BottomPanel::BottomPanel() : TabbedPanel(daw::ui::PanelLocation::Bottom) {
    setName("Bottom Panel");

    // Create the editor tab bar (hidden by default)
    editorTabBar_ = std::make_unique<EditorTabBar>(*this);
    editorTabBar_->addTab("Piano Roll", juce::Colours::transparentBlack, 0);
    editorTabBar_->addTab("Drum Grid", juce::Colours::transparentBlack, 1);
    editorTabBar_->setVisible(false);
    addChildComponent(editorTabBar_.get());

    // Register as listener for selection changes
    ClipManager::getInstance().addListener(this);
    TrackManager::getInstance().addListener(this);

    // Set initial content based on current selection
    updateContentBasedOnSelection();
}

BottomPanel::~BottomPanel() {
    ClipManager::getInstance().removeListener(this);
    TrackManager::getInstance().removeListener(this);
}

void BottomPanel::setCollapsed(bool collapsed) {
    daw::ui::PanelController::getInstance().setCollapsed(daw::ui::PanelLocation::Bottom, collapsed);
}

void BottomPanel::paint(juce::Graphics& g) {
    TabbedPanel::paint(g);
}

void BottomPanel::resized() {
    // Position editor tab bar at the top of the panel area
    if (showEditorTabs_ && editorTabBar_) {
        auto tabBounds = getLocalBounds().removeFromTop(EDITOR_TAB_HEIGHT);
        editorTabBar_->setBounds(tabBounds);
    }

    // TabbedPanel::resized() uses getContentBounds() which accounts for the tab bar
    TabbedPanel::resized();
}

void BottomPanel::clipsChanged() {
    updateContentBasedOnSelection();
}

void BottomPanel::clipSelectionChanged(ClipId /*clipId*/) {
    updateContentBasedOnSelection();
}

void BottomPanel::tracksChanged() {
    updateContentBasedOnSelection();
}

void BottomPanel::trackSelectionChanged(TrackId /*trackId*/) {
    updateContentBasedOnSelection();
}

bool BottomPanel::trackHasDrumGrid(TrackId trackId) const {
    return findDrumGridOnTrack(trackId) != nullptr;
}

void BottomPanel::updateContentBasedOnSelection() {
    auto& clipManager = ClipManager::getInstance();
    auto& trackManager = TrackManager::getInstance();

    ClipId selectedClip = clipManager.getSelectedClip();
    TrackId selectedTrack = trackManager.getSelectedTrack();

    daw::ui::PanelContentType targetContent = daw::ui::PanelContentType::Empty;
    bool needsTabs = false;

    if (selectedClip != INVALID_CLIP_ID) {
        const auto* clip = clipManager.getClip(selectedClip);
        if (clip) {
            if (clip->type == ClipType::MIDI) {
                if (trackHasDrumGrid(clip->trackId)) {
                    needsTabs = true;
                    // Restore user's last tab choice for DrumGrid tracks
                    targetContent = (lastDrumGridTabChoice_ == 1)
                                        ? daw::ui::PanelContentType::DrumGridClipView
                                        : daw::ui::PanelContentType::PianoRoll;
                } else {
                    targetContent = daw::ui::PanelContentType::PianoRoll;
                }
            } else if (clip->type == ClipType::Audio) {
                targetContent = daw::ui::PanelContentType::WaveformEditor;
            }
        }
    } else if (selectedTrack != INVALID_TRACK_ID) {
        targetContent = daw::ui::PanelContentType::TrackChain;
    }

    // Update tab bar visibility
    showEditorTabs_ = needsTabs;
    if (editorTabBar_) {
        editorTabBar_->setVisible(showEditorTabs_);
        if (showEditorTabs_) {
            // Set tab bar to match target content, with guard to prevent re-entrancy
            updatingTabs_ = true;
            editorTabBar_->setCurrentTabIndex(lastDrumGridTabChoice_, false);
            updatingTabs_ = false;
        }
    }
    resized();

    // Switch to the appropriate content via PanelController
    daw::ui::PanelController::getInstance().setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                                               targetContent);
}

juce::Rectangle<int> BottomPanel::getCollapseButtonBounds() {
    if (isCollapsed()) {
        return juce::Rectangle<int>(getWidth() / 2 - 10, 2, 20, 20);
    } else {
        // Collapse button on the right side of the header
        return juce::Rectangle<int>(getWidth() - 28, 4, 20, 20);
    }
}

juce::Rectangle<int> BottomPanel::getTabBarBounds() {
    // No tab bar for bottom panel - content is auto-switched based on selection
    return juce::Rectangle<int>();
}

juce::Rectangle<int> BottomPanel::getContentBounds() {
    auto bounds = getLocalBounds();
    if (showEditorTabs_) {
        bounds.removeFromTop(EDITOR_TAB_HEIGHT);
    }
    return bounds;
}

void BottomPanel::onEditorTabChanged(int tabIndex) {
    if (updatingTabs_)
        return;

    lastDrumGridTabChoice_ = tabIndex;

    auto targetType = (tabIndex == 1) ? daw::ui::PanelContentType::DrumGridClipView
                                      : daw::ui::PanelContentType::PianoRoll;
    daw::ui::PanelController::getInstance().setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                                               targetType);
}

}  // namespace magda
