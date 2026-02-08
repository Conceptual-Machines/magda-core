#include "InspectorContainer.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "ClipInspector.hpp"
#include "DeviceInspector.hpp"
#include "InspectorFactory.hpp"
#include "NoteInspector.hpp"
#include "TrackInspector.hpp"
#include "core/SelectionManager.hpp"

namespace magda::daw::ui {

InspectorContainer::InspectorContainer() {
    // No selection label
    noSelectionLabel_.setText("No selection", juce::dontSendNotification);
    noSelectionLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    noSelectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    noSelectionLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(noSelectionLabel_);

    // Register as SelectionManager listener
    magda::SelectionManager::getInstance().addListener(this);

    // Initialize with current selection
    currentSelectionType_ = magda::SelectionManager::getInstance().getSelectionType();
    switchToInspector(currentSelectionType_);
}

InspectorContainer::~InspectorContainer() {
    if (currentInspector_) {
        currentInspector_->onDeactivated();
    }
    magda::SelectionManager::getInstance().removeListener(this);
}

PanelContentType InspectorContainer::getContentType() const {
    return PanelContentType::Inspector;
}

PanelContentInfo InspectorContainer::getContentInfo() const {
    return {PanelContentType::Inspector, "Inspector", "View and edit properties of selected items",
            "Inspector"};
}

void InspectorContainer::onActivated() {
    // Activate current inspector when container is shown
    if (currentInspector_) {
        currentInspector_->onActivated();
    }
}

void InspectorContainer::onDeactivated() {
    // Deactivate current inspector when container is hidden
    if (currentInspector_) {
        currentInspector_->onDeactivated();
    }
}

void InspectorContainer::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void InspectorContainer::resized() {
    auto bounds = getLocalBounds();

    if (currentInspector_) {
        currentInspector_->setBounds(bounds);
        noSelectionLabel_.setVisible(false);
    } else {
        // Center the no-selection label
        noSelectionLabel_.setBounds(bounds);
        noSelectionLabel_.setVisible(true);
    }
}

void InspectorContainer::setTimelineController(magda::TimelineController* controller) {
    timelineController_ = controller;
    if (currentInspector_) {
        currentInspector_->setTimelineController(controller);
    }
}

void InspectorContainer::setAudioEngine(magda::AudioEngine* engine) {
    audioEngine_ = engine;
    if (currentInspector_) {
        currentInspector_->setAudioEngine(engine);
    }
}

void InspectorContainer::selectionTypeChanged(magda::SelectionType newType) {
    if (newType != currentSelectionType_) {
        switchToInspector(newType);
    }
}

void InspectorContainer::trackSelectionChanged(magda::TrackId trackId) {
    auto* trackInspector = dynamic_cast<TrackInspector*>(currentInspector_.get());
    if (trackInspector) {
        trackInspector->setSelectedTrack(trackId);
    }
}

void InspectorContainer::clipSelectionChanged(magda::ClipId clipId) {
    auto* clipInspector = dynamic_cast<ClipInspector*>(currentInspector_.get());
    if (clipInspector) {
        clipInspector->setSelectedClip(clipId);
    }
}

void InspectorContainer::noteSelectionChanged(const magda::NoteSelection& selection) {
    auto* noteInspector = dynamic_cast<NoteInspector*>(currentInspector_.get());
    if (noteInspector) {
        noteInspector->setSelectedNotes(selection);
    }
}

void InspectorContainer::chainNodeSelectionChanged(const magda::ChainNodePath& path) {
    auto* deviceInspector = dynamic_cast<DeviceInspector*>(currentInspector_.get());
    if (deviceInspector) {
        deviceInspector->setSelectedChainNode(path);
    }
}

void InspectorContainer::switchToInspector(magda::SelectionType type) {
    // Deactivate current inspector
    if (currentInspector_) {
        currentInspector_->onDeactivated();
        removeChildComponent(currentInspector_.get());
        currentInspector_.reset();
    }

    currentSelectionType_ = type;

    // Create new inspector
    currentInspector_ = InspectorFactory::createInspector(type);

    if (currentInspector_) {
        // Set up dependencies
        currentInspector_->setTimelineController(timelineController_);
        currentInspector_->setAudioEngine(audioEngine_);

        // Activate and add to UI
        currentInspector_->onActivated();
        addAndMakeVisible(*currentInspector_);
    }

    resized();
}

}  // namespace magda::daw::ui
