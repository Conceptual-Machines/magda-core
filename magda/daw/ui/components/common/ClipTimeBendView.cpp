// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/common/ClipTimeBendView.hpp"

#include "core/MidiNoteCommands.hpp"
#include "music/GrooveLibrary.hpp"
#include "project/ProjectManager.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kButtonRow = 20;
constexpr int kGap = 4;

double beatsPerBar() {
    return magda::ProjectManager::getInstance().getCurrentProjectInfo().beatsPerBar();
}

}  // namespace

ClipTimeBendView::ClipTimeBendView() {
    editor_.onPreview = [this](const auto& bend) { edit_.preview(clipIds_, bend); };
    editor_.onCommit = [this](const auto&, const auto& after) { edit_.commit(clipIds_, after); };
    addAndMakeVisible(editor_);

    grooveButton_.setTooltip("Load a groove at full strength; AMOUNT sets how much is heard");
    grooveButton_.onClick = [this] { showGrooveMenu(); };
    bakeButton_.setTooltip("Write what the bend plays into the notes, and turn it off");
    bakeButton_.onClick = [this] {
        magda::CompoundOperationScope scope("Bake Time Bend");
        for (const auto id : clipIds_)
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::BakeClipTimeBendCommand>(id, beatsPerBar()));
    };
    for (auto* button : {&grooveButton_, &bakeButton_}) {
        button->setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
        button->setColour(juce::TextButton::buttonColourId,
                          ActiveTheme::getColour(ActiveTheme::SURFACE));
        button->setColour(juce::TextButton::textColourOffId, ActiveTheme::getSecondaryTextColour());
        addAndMakeVisible(*button);
    }

    magda::ClipManager::getInstance().addListener(this);
}

ClipTimeBendView::~ClipTimeBendView() {
    magda::ClipManager::getInstance().removeListener(this);
    grooveButton_.setLookAndFeel(nullptr);
    bakeButton_.setLookAndFeel(nullptr);
}

void ClipTimeBendView::setClips(std::vector<magda::ClipId> clipIds) {
    clipIds_ = std::move(clipIds);
    reload();
}

void ClipTimeBendView::reload() {
    if (edit_.previewing() || clipIds_.empty())
        return;
    const auto* clip = magda::ClipManager::getInstance().getClip(clipIds_.front());
    if (clip == nullptr || !clip->isMidi())
        return;
    editor_.setBend(clip->timeBend);
    editor_.setCycle(clip->timeBend.cycleBeats(beatsPerBar()), beatsPerBar());
    bakeButton_.setEnabled(clip->timeBend.isActive());
}

void ClipTimeBendView::clipsChanged() {
    std::erase_if(clipIds_, [](magda::ClipId id) {
        return magda::ClipManager::getInstance().getClip(id) == nullptr;
    });
    reload();
}

void ClipTimeBendView::clipPropertyChanged(magda::ClipId clipId) {
    if (!clipIds_.empty() && clipId == clipIds_.front())
        reload();
}

void ClipTimeBendView::showGrooveMenu() {
    TimeBendEditor::showGrooveMenu(grooveButton_,
                                   [this](const juce::String& name) { loadGroove(name); });
}

void ClipTimeBendView::loadGroove(const juce::String& name) {
    auto bend = editor_.getBend();
    if (name.isEmpty()) {
        bend.on = false;
    } else if (auto loaded = magda::GrooveLibrary::getInstance().bend(name, beatsPerBar())) {
        bend = *loaded;
    } else {
        return;
    }
    edit_.commit(clipIds_, bend);
}

void ClipTimeBendView::resized() {
    auto area = getLocalBounds();
    auto buttons = area.removeFromBottom(kButtonRow);
    area.removeFromBottom(kGap);
    grooveButton_.setBounds(buttons.removeFromLeft(64));
    bakeButton_.setBounds(buttons.removeFromRight(52));
    editor_.setBounds(area);
}

}  // namespace magda::daw::ui
