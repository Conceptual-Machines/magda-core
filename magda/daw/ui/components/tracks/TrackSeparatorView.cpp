// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TrackSeparatorView.hpp"

#include "core/SelectionManager.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"
#include "ui/components/common/MagdaAlertWindow.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda {

TrackSeparatorView::TrackSeparatorView(TrackId trackId) : trackId_(trackId) {}

void TrackSeparatorView::paint(juce::Graphics& g) {
    const auto* track = TrackManager::getInstance().getTrack(trackId_);
    if (track == nullptr)
        return;

    const auto font = FontManager::getInstance().getUIFont(10.0f);
    const int textLength =
        track->name.isEmpty()
            ? 0
            : juce::jmin(getHeight(),
                         juce::GlyphArrangement::getStringWidthInt(font, track->name) + 12);

    // The rule starts where the name ends.
    g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
    g.fillRect(getWidth() / 2, textLength, 1, getHeight() - textLength);

    if (textLength > 0) {
        juce::Graphics::ScopedSaveState state(g);
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi)
                           .translated(0.0f, static_cast<float>(textLength)));
        g.setFont(font);
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        g.drawText(track->name, 0, 0, textLength, getWidth(), juce::Justification::centred, true);
    }
}

void TrackSeparatorView::mouseDown(const juce::MouseEvent& event) {
    if (event.mods.isPopupMenu()) {
        showTrackSeparatorMenu(trackId_);
        return;
    }
    if (onPress)
        onPress(event);
}

void TrackSeparatorView::mouseDrag(const juce::MouseEvent& event) {
    if (onDrag && !event.mods.isPopupMenu())
        onDrag(event);
}

void TrackSeparatorView::mouseUp(const juce::MouseEvent& event) {
    if (event.mods.isPopupMenu())
        return;
    if (!event.mouseWasDraggedSinceMouseDown())
        SelectionManager::getInstance().selectTrack(trackId_);

    // A drop rebuilds the view that owns this component, so nothing here can
    // outlive the call, the callback's own captures included.
    if (const auto release = onRelease)
        release(event);
}

void TrackSeparatorView::mouseDoubleClick(const juce::MouseEvent& /*event*/) {
    promptToRenameTrackSeparator(trackId_);
}

void showTrackSeparatorMenu(TrackId trackId) {
    juce::PopupMenu menu;
    menu.addItem(1, "Rename Separator...");
    menu.addItem(2, "Delete Separator");
    menu.showMenuAsync(juce::PopupMenu::Options(), [trackId](int result) {
        if (result == 1)
            promptToRenameTrackSeparator(trackId);
        else if (result == 2)
            deleteTracksFromMenu(trackId);
    });
}

void promptToRenameTrackSeparator(TrackId trackId) {
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (track == nullptr)
        return;

    auto* window = new MagdaAlertWindow("Rename Separator", "Leave empty for no name.",
                                        juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("name", track->name, "Name:");
    window->addButton("OK", 1);
    window->addButton("Cancel", 0);
    window->enterModalState(
        true, juce::ModalCallbackFunction::create([window, trackId](int result) {
            if (result == 1)
                UndoManager::getInstance().executeCommand(std::make_unique<SetTrackNameCommand>(
                    trackId, window->getTextEditorContents("name").trim()));
            delete window;
        }),
        true);
}

}  // namespace magda
