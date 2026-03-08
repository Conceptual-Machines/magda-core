#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "core/ClipManager.hpp"

namespace magda {

/// Custom clip slot button for SessionView grid.
/// Handles clicks, double-clicks, play button area, drag-and-drop, and group slots.
class ClipSlotButton : public juce::TextButton {
  public:
    static constexpr int PLAY_BUTTON_WIDTH = 22;

    std::function<void()> onSingleClick;
    std::function<void()> onDoubleClick;
    std::function<void()> onPlayButtonClick;
    std::function<void()> onCreateMidiClip;
    std::function<void()> onDeleteClip;
    std::function<void()> onCopyClip;
    std::function<void()> onCutClip;
    std::function<void()> onPasteClip;
    std::function<void()> onDuplicateClip;
    std::function<void()> onAddScene;
    std::function<void()> onRemoveScene;

    bool hasClip = false;
    bool clipIsPlaying = false;
    bool clipIsQueued = false;
    bool blinkOn = false;  // Toggled by SessionView timer for queued blink
    bool isSelected = false;
    bool isMidiClip = false;
    double clipLength = 0.0;           // Clip duration in seconds (for progress bar)
    double sessionPlayheadPos = -1.0;  // Looped playhead position in seconds

    // Group slot: shows play button for child clips, no clip creation
    bool isGroupSlot = false;
    bool hasChildClips = false;       // Any child track has a clip in this scene
    bool childClipIsPlaying = false;  // Any child clip in this scene is playing

    // Clip slot identity (for drag-and-drop)
    ClipId clipId = INVALID_CLIP_ID;
    TrackId trackId = INVALID_TRACK_ID;
    int sceneIndex = -1;

    void mouseDrag(const juce::MouseEvent& event) override {
        if (isGroupSlot || !hasClip || clipId == INVALID_CLIP_ID)
            return;

        if (event.getDistanceFromDragStart() < 5)
            return;

        auto* dragContainer = juce::DragAndDropContainer::findParentDragContainerFor(this);
        if (!dragContainer)
            return;

        // Build drag description
        auto* desc = new juce::DynamicObject();
        desc->setProperty("type", "sessionClip");
        desc->setProperty("clipId", static_cast<int>(clipId));
        desc->setProperty("trackId", static_cast<int>(trackId));
        desc->setProperty("sceneIndex", sceneIndex);

        // Create a snapshot image of this slot as drag ghost
        auto snapshot = createComponentSnapshot(getLocalBounds(), true, 1.0f);

        dragContainer->startDragging(juce::var(desc), this, juce::ScaledImage(snapshot), true);
    }

    void mouseDown(const juce::MouseEvent& event) override {
        if (event.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            if (!isGroupSlot) {
                if (!hasClip)
                    menu.addItem(1, "Create MIDI Clip");
                if (hasClip) {
                    menu.addItem(5, "Copy");
                    menu.addItem(6, "Cut");
                    menu.addItem(8, "Duplicate");
                }
                bool hasClipboard = ClipManager::getInstance().hasClipsInClipboard();
                menu.addItem(7, "Paste", hasClipboard);
                menu.addSeparator();
                if (hasClip)
                    menu.addItem(4, "Delete Clip");
                menu.addSeparator();
            }
            menu.addItem(2, "Add Scene");
            menu.addItem(3, "Remove Scene");
            auto safeThis = juce::Component::SafePointer<ClipSlotButton>(this);
            menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result) {
                if (!safeThis)
                    return;
                if (result == 1 && safeThis->onCreateMidiClip)
                    safeThis->onCreateMidiClip();
                else if (result == 2 && safeThis->onAddScene)
                    safeThis->onAddScene();
                else if (result == 3 && safeThis->onRemoveScene)
                    safeThis->onRemoveScene();
                else if (result == 4 && safeThis->onDeleteClip)
                    safeThis->onDeleteClip();
                else if (result == 5 && safeThis->onCopyClip)
                    safeThis->onCopyClip();
                else if (result == 6 && safeThis->onCutClip)
                    safeThis->onCutClip();
                else if (result == 7 && safeThis->onPasteClip)
                    safeThis->onPasteClip();
                else if (result == 8 && safeThis->onDuplicateClip)
                    safeThis->onDuplicateClip();
            });
            return;
        }
        juce::TextButton::mouseDown(event);
    }

    void mouseUp(const juce::MouseEvent& event) override {
        if (!event.mouseWasClicked())
            return;

        const int clicks = event.getNumberOfClicks();

        if (isGroupSlot) {
            // Group slots: single click triggers/stops child clips
            if (clicks == 1 && hasChildClips && onPlayButtonClick)
                onPlayButtonClick();
            return;
        }

        const bool inPlayArea = hasClip && event.getPosition().getX() < PLAY_BUTTON_WIDTH;

        if (clicks >= 2) {
            if (!inPlayArea && onDoubleClick) {
                onDoubleClick();
            }
            return;
        }

        if (inPlayArea) {
            if (onPlayButtonClick) {
                onPlayButtonClick();
            }
        } else {
            if (onSingleClick) {
                onSingleClick();
            }
        }
    }

    void clicked() override {
        // Handled by mouseUp instead
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override {
        // Draw base button (background, border via LookAndFeel)
        // Temporarily clear text so base class doesn't draw it centered
        auto savedText = getButtonText();
        if (hasClip || isGroupSlot)
            setButtonText("");
        juce::TextButton::paintButton(g, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        if (hasClip || isGroupSlot)
            setButtonText(savedText);

        // Draw selection highlight border
        if (isSelected) {
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawRect(getLocalBounds(), 2);
        }

        // Group slot: draw centered play/stop button when child clips exist
        if (isGroupSlot) {
            if (hasChildClips) {
                auto centre = getLocalBounds().getCentre().toFloat();
                if (childClipIsPlaying) {
                    // Stop square
                    float size = 5.0f;
                    g.setColour(juce::Colours::white.withAlpha(0.9f));
                    g.fillRect(juce::Rectangle<float>(centre.getX() - size, centre.getY() - size,
                                                      size * 2.0f, size * 2.0f));
                } else {
                    // Play triangle
                    juce::Path triangle;
                    float size = 6.0f;
                    triangle.addTriangle(centre.getX() - size * 0.7f, centre.getY() - size,
                                         centre.getX() - size * 0.7f, centre.getY() + size,
                                         centre.getX() + size, centre.getY());
                    g.setColour(juce::Colours::white.withAlpha(0.7f));
                    g.fillPath(triangle);
                }
            }
            return;
        }

        if (hasClip) {
            // Draw play/stop icon in the left area
            auto playArea = getLocalBounds().removeFromLeft(PLAY_BUTTON_WIDTH);
            auto centre = playArea.getCentre().toFloat();

            if (clipIsPlaying) {
                // Draw stop square when playing
                float size = 5.0f;
                g.setColour(juce::Colours::white.withAlpha(0.9f));
                g.fillRect(juce::Rectangle<float>(centre.getX() - size, centre.getY() - size,
                                                  size * 2.0f, size * 2.0f));
            } else {
                // Draw play triangle — blink when queued
                juce::Path triangle;
                float size = 6.0f;
                triangle.addTriangle(centre.getX() - size * 0.7f, centre.getY() - size,
                                     centre.getX() - size * 0.7f, centre.getY() + size,
                                     centre.getX() + size, centre.getY());
                float alpha = (clipIsQueued && !blinkOn) ? 0.15f : 0.7f;
                g.setColour(juce::Colours::white.withAlpha(alpha));
                g.fillPath(triangle);
            }

            // Content area (right of play button)
            auto contentArea = getLocalBounds();
            contentArea.removeFromLeft(PLAY_BUTTON_WIDTH);

            // Draw progress bar for playing clips
            if (clipIsPlaying && clipLength > 0.0 && sessionPlayheadPos >= 0.0) {
                float progress = static_cast<float>(sessionPlayheadPos / clipLength);
                progress = juce::jlimit(0.0f, 1.0f, progress);

                auto progressBar = contentArea.toFloat();
                progressBar.setWidth(progressBar.getWidth() * progress);
                g.setColour(juce::Colours::white.withAlpha(0.15f));
                g.fillRect(progressBar);

                // Draw playhead line at current position
                float lineX = contentArea.getX() + contentArea.getWidth() * progress;
                g.setColour(juce::Colours::white.withAlpha(0.6f));
                g.drawVerticalLine(static_cast<int>(lineX), static_cast<float>(contentArea.getY()),
                                   static_cast<float>(contentArea.getBottom()));
            }

            // Draw clip name in content area, left-justified
            auto textArea = contentArea.reduced(2, 0);
            if (isMidiClip)
                textArea.removeFromRight(16);  // Reserve space for M badge
            g.setColour(findColour(juce::TextButton::textColourOffId));
            g.setFont(FontManager::getInstance().getUIFont(9.0f));
            g.drawText(getButtonText(), textArea, juce::Justification::centredLeft, true);

            // Draw "M" badge for MIDI clips
            if (isMidiClip) {
                auto badgeArea = getLocalBounds().removeFromRight(16).removeFromTop(14);
                g.setColour(juce::Colours::white.withAlpha(0.8f));
                g.setFont(FontManager::getInstance().getUIFontBold(10.0f));
                g.drawText("M", badgeArea, juce::Justification::centred, false);
            }
        }
    }
};

}  // namespace magda
