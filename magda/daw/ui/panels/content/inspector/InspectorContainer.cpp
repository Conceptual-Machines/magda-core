// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "InspectorContainer.hpp"

#include <utility>

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "AutomationClipInspector.hpp"
#include "AutomationPointInspector.hpp"
#include "ClipInspector.hpp"
#include "DeviceInspector.hpp"
#include "InspectorFactory.hpp"
#include "NoteInspector.hpp"
#include "TrackInspector.hpp"
#include "core/SelectionManager.hpp"

namespace magda::daw::ui {

namespace {

bool isModulationSelection(magda::SelectionType type) {
    return type == magda::SelectionType::Mod || type == magda::SelectionType::Macro ||
           type == magda::SelectionType::ModsPanel || type == magda::SelectionType::MacrosPanel;
}

/// Whose mods or macros the selection is in, and the item: -1 for a whole panel.
std::pair<magda::ChainNodePath, int> modulationSelection(const magda::SelectionManager& sm) {
    switch (sm.getSelectionType()) {
        case magda::SelectionType::Mod:
            return {sm.getModSelection().parentPath, sm.getModSelection().modIndex};
        case magda::SelectionType::Macro:
            return {sm.getMacroSelection().parentPath, sm.getMacroSelection().macroIndex};
        case magda::SelectionType::ModsPanel:
            return {sm.getModsPanelSelection().parentPath, -1};
        case magda::SelectionType::MacrosPanel:
            return {sm.getMacrosPanelSelection().parentPath, -1};
        default:
            return {};
    }
}

}  // namespace

InspectorContainer::InspectorContainer() {
    // No selection label
    noSelectionLabel_.setText("No selection", juce::dontSendNotification);
    noSelectionLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    noSelectionLabel_.setColour(juce::Label::textColourId, ActiveTheme::getSecondaryTextColour());
    noSelectionLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(noSelectionLabel_);

    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(8);
    addAndMakeVisible(viewport_);

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
    g.fillAll(ActiveTheme::getPanelBackgroundColour());
}

void InspectorContainer::resized() {
    auto bounds = getLocalBounds();
    viewport_.setBounds(bounds);
    noSelectionLabel_.setBounds(bounds);
    noSelectionLabel_.setVisible(currentInspector_ == nullptr);
    layoutInspector();
}

void InspectorContainer::layoutInspector() {
    if (currentInspector_ == nullptr || layingOut_)
        return;
    const juce::ScopedValueSetter<bool> guard(layingOut_, true);
    const auto position = viewport_.getViewPosition();
    const int width = viewport_.getWidth();
    const int height = viewport_.getHeight();
    // The scrollbar narrows the content, which can change its height: a pass or two settles it.
    for (int pass = 0; pass < 3; ++pass) {
        const int preferred = currentInspector_->getPreferredHeight();
        const bool scrolls = preferred > height;
        const int w = scrolls ? width - viewport_.getScrollBarThickness() : width;
        const int h = scrolls ? preferred : height;
        if (currentInspector_->getWidth() == w && currentInspector_->getHeight() == h)
            break;
        currentInspector_->setSize(w, h);
    }
    viewport_.setViewPosition(position);
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
        clipInspector->setSelectedClips({clipId});
    }
}

void InspectorContainer::multiClipSelectionChanged(
    const std::unordered_set<magda::ClipId>& clipIds) {
    auto* clipInspector = dynamic_cast<ClipInspector*>(currentInspector_.get());
    if (clipInspector) {
        clipInspector->setSelectedClips(clipIds);
    }
}

void InspectorContainer::noteSelectionChanged(const magda::NoteSelection& selection) {
    auto* noteInspector = dynamic_cast<NoteInspector*>(currentInspector_.get());
    if (noteInspector) {
        noteInspector->setSelectedNotes(selection);
    }
}

void InspectorContainer::multiTrackSelectionChanged(
    const std::unordered_set<magda::TrackId>& trackIds) {
    auto* trackInspector = dynamic_cast<TrackInspector*>(currentInspector_.get());
    if (trackInspector) {
        trackInspector->setSelectedTracks(trackIds);
    }
}

void InspectorContainer::chainNodeSelectionChanged(const magda::ChainNodePath& path) {
    auto* deviceInspector = dynamic_cast<DeviceInspector*>(currentInspector_.get());
    if (deviceInspector) {
        deviceInspector->setSelectedChainNode(path);
    }
}

void InspectorContainer::automationClipSelectionChanged(
    const magda::AutomationClipSelection& selection) {
    auto* inspector = dynamic_cast<AutomationClipInspector*>(currentInspector_.get());
    if (inspector) {
        inspector->setSelectedClip(selection);
    }
}

void InspectorContainer::automationPointSelectionChanged(
    const magda::AutomationPointSelection& selection) {
    auto* inspector = dynamic_cast<AutomationPointInspector*>(currentInspector_.get());
    if (inspector) {
        inspector->setSelectedPoints(selection);
    }
}

void InspectorContainer::modSelectionChanged(const magda::ModSelection&) {
    if (currentSelectionType_ == magda::SelectionType::Mod)
        showModulationSelection();
}

void InspectorContainer::macroSelectionChanged(const magda::MacroSelection&) {
    if (currentSelectionType_ == magda::SelectionType::Macro)
        showModulationSelection();
}

void InspectorContainer::modsPanelSelectionChanged(const magda::ModsPanelSelection&) {
    if (currentSelectionType_ == magda::SelectionType::ModsPanel)
        showModulationSelection();
}

void InspectorContainer::macrosPanelSelectionChanged(const magda::MacrosPanelSelection&) {
    if (currentSelectionType_ == magda::SelectionType::MacrosPanel)
        showModulationSelection();
}

void InspectorContainer::showModulationSelection() {
    auto& sm = magda::SelectionManager::getInstance();
    const auto [owner, index] = modulationSelection(sm);
    const bool trackLevel = owner.getType() == magda::ChainNodeType::Track;

    // The owner's inspector, kept when it is already the right kind.
    auto* trackInspector = dynamic_cast<TrackInspector*>(currentInspector_.get());
    auto* deviceInspector = dynamic_cast<DeviceInspector*>(currentInspector_.get());
    if (trackLevel ? trackInspector == nullptr : deviceInspector == nullptr) {
        installInspector(trackLevel ? magda::SelectionType::Track
                                    : magda::SelectionType::ChainNode);
        trackInspector = dynamic_cast<TrackInspector*>(currentInspector_.get());
        deviceInspector = dynamic_cast<DeviceInspector*>(currentInspector_.get());
        resized();
    }

    if (trackInspector != nullptr) {
        trackInspector->setSelectedTrack(owner.trackId);
        layoutInspector();
        trackInspector->focusModulation(sm.getSelectionType(), index);
    } else if (deviceInspector != nullptr) {
        deviceInspector->setSelectedChainNode(owner);
        layoutInspector();
        deviceInspector->focusModulation(sm.getSelectionType(), index);
    }
}

void InspectorContainer::installInspector(magda::SelectionType type) {
    if (currentInspector_) {
        currentInspector_->onDeactivated();
        viewport_.setViewedComponent(nullptr, false);
        currentInspector_.reset();
    }

    currentInspector_ = InspectorFactory::createInspector(type);
    if (currentInspector_) {
        currentInspector_->setTimelineController(timelineController_);
        currentInspector_->setAudioEngine(audioEngine_);
        currentInspector_->onPreferredHeightChanged = [this] { layoutInspector(); };
        currentInspector_->onActivated();
        viewport_.setViewedComponent(currentInspector_.get(), false);
        viewport_.setViewPosition(0, 0);
    }
}

void InspectorContainer::switchToInspector(magda::SelectionType type) {
    currentSelectionType_ = type;
    if (isModulationSelection(type)) {
        showModulationSelection();
        return;
    }

    installInspector(type);

    if (currentInspector_) {
        // Forward current selection data to the newly created inspector
        auto& sm = magda::SelectionManager::getInstance();
        if (type == magda::SelectionType::Track) {
            auto* trackInspector = dynamic_cast<TrackInspector*>(currentInspector_.get());
            if (trackInspector)
                trackInspector->setSelectedTrack(sm.getSelectedTrack());
        } else if (type == magda::SelectionType::MultiTrack) {
            auto* trackInspector = dynamic_cast<TrackInspector*>(currentInspector_.get());
            if (trackInspector)
                trackInspector->setSelectedTracks(sm.getSelectedTracks());
        } else if (type == magda::SelectionType::Clip) {
            auto* clipInspector = dynamic_cast<ClipInspector*>(currentInspector_.get());
            if (clipInspector)
                clipInspector->setSelectedClips({sm.getSelectedClip()});
        } else if (type == magda::SelectionType::MultiClip) {
            auto* clipInspector = dynamic_cast<ClipInspector*>(currentInspector_.get());
            if (clipInspector)
                clipInspector->setSelectedClips(sm.getSelectedClips());
        } else if (type == magda::SelectionType::Note) {
            auto* noteInspector = dynamic_cast<NoteInspector*>(currentInspector_.get());
            if (noteInspector)
                noteInspector->setSelectedNotes(sm.getNoteSelection());
        } else if (type == magda::SelectionType::ChainNode) {
            auto* deviceInspector = dynamic_cast<DeviceInspector*>(currentInspector_.get());
            if (deviceInspector)
                deviceInspector->setSelectedChainNode(sm.getSelectedChainNode());
        } else if (type == magda::SelectionType::AutomationPoint) {
            auto* inspector = dynamic_cast<AutomationPointInspector*>(currentInspector_.get());
            if (inspector)
                inspector->setSelectedPoints(sm.getAutomationPointSelection());
        } else if (type == magda::SelectionType::AutomationClip) {
            auto* inspector = dynamic_cast<AutomationClipInspector*>(currentInspector_.get());
            if (inspector)
                inspector->setSelectedClip(sm.getAutomationClipSelection());
        }
    }

    resized();
}

}  // namespace magda::daw::ui
