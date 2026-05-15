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

    std::function<void(const juce::MouseEvent&)> onSingleClick;
    std::function<void()> onDoubleClick;
    std::function<void()> onPlayButtonClick;
    std::function<void()> onEmptySlotStopClick;    // strip click on empty slot, track not armed
    std::function<void()> onEmptySlotRecordClick;  // strip click on empty slot, track armed
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
    bool stopIsQueued = false;  // Empty slot blinks its stop icon while a row-stop is pending
    bool blinkOn = false;       // Toggled by SessionView timer for queued blink
    bool isSelected = false;
    bool trackIsRecordArmed = false;
    bool slotRecordArmed = false;
    bool slotIsRecording = false;
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

        const bool inStripArea = event.getPosition().getX() < PLAY_BUTTON_WIDTH;

        if (clicks >= 2) {
            if (!(hasClip && inStripArea) && onDoubleClick) {
                onDoubleClick();
            }
            return;
        }

        if (hasClip && inStripArea) {
            if (onPlayButtonClick) {
                onPlayButtonClick();
            }
            return;
        }

        if (!hasClip && inStripArea) {
            // Empty-slot strip is a row-level affordance: stop the active clip
            // on this track, or start recording if the track is armed.
            if (trackIsRecordArmed) {
                if (onEmptySlotRecordClick)
                    onEmptySlotRecordClick();
            } else {
                if (onEmptySlotStopClick)
                    onEmptySlotStopClick();
            }
            return;
        }

        if (onSingleClick) {
            onSingleClick(event);
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

        // Filled clips: paint the strip area as a solid background — black when
        // selected, mid-grey otherwise. The icon colour flips to keep contrast,
        // replacing the old white selection rectangle so it can't fight overlay
        // UI (e.g. controller scene-view rectangles). Top/bottom inset by 0.5px
        // to match SmallButtonLookAndFeel's rounded bg so the strip doesn't
        // poke past the slot's curved edges.
        if (hasClip) {
            auto stripArea =
                getLocalBounds().toFloat().reduced(0.0f, 0.5f).removeFromLeft(PLAY_BUTTON_WIDTH);
            constexpr float kStripCorner = 6.0f;
            juce::Path stripPath;
            stripPath.addRoundedRectangle(stripArea.getX(), stripArea.getY(), stripArea.getWidth(),
                                          stripArea.getHeight(), kStripCorner, kStripCorner,
                                          /*curveTopLeft*/ true, /*curveTopRight*/ false,
                                          /*curveBottomLeft*/ true, /*curveBottomRight*/ false);
            g.setColour(isSelected ? juce::Colours::black : juce::Colour(0xFFA0A0A0));
            g.fillPath(stripPath);
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
            // Filled slots always render play. Stop is now the empty-slot
            // affordance — the row's "stop whatever's playing here" button —
            // so the strip stays a pure "trigger this clip" target regardless
            // of state.
            auto playArea = getLocalBounds().removeFromLeft(PLAY_BUTTON_WIDTH);
            auto centre = playArea.getCentre().toFloat();

            // Cyan when selected OR when the clip is actually running —
            // launching from the scene button should light the track's play
            // icon the same way as a direct click. Idle non-selected slots
            // stay black against the grey strip for contrast.
            const auto iconColour = (isSelected || clipIsPlaying || clipIsQueued)
                                        ? DarkTheme::getColour(DarkTheme::ACCENT_CYAN)
                                        : juce::Colours::black;

            juce::Path triangle;
            float size = 6.0f;
            triangle.addTriangle(centre.getX() - size * 0.7f, centre.getY() - size,
                                 centre.getX() - size * 0.7f, centre.getY() + size,
                                 centre.getX() + size, centre.getY());
            auto playColour = iconColour;
            if (clipIsQueued && !blinkOn)
                playColour = playColour.withAlpha(0.15f);
            g.setColour(playColour);
            g.fillPath(triangle);

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

            // Draw clip name in content area, left-justified.
            // Left padding clears the strip separator; right padding mirrors it.
            auto textArea = contentArea.reduced(6, 0);
            g.setColour(findColour(juce::TextButton::textColourOffId));
            g.setFont(FontManager::getInstance().getUIFont(9.0f));
            g.drawText(getButtonText(), textArea, juce::Justification::centredLeft, true);
        } else {
            // Empty slot: show a record icon when the track is record-armed,
            // otherwise a stop icon (acts as a row-stop affordance). Visual only —
            // click handling is wired in a follow-up.
            auto playArea = getLocalBounds().removeFromLeft(PLAY_BUTTON_WIDTH);
            auto centre = playArea.getCentre().toFloat();

            if (trackIsRecordArmed) {
                float radius = 5.0f;
                auto recordColour = DarkTheme::getColour(DarkTheme::STATUS_DANGER);

                if (slotIsRecording) {
                    auto contentArea = getLocalBounds().withTrimmedLeft(PLAY_BUTTON_WIDTH);
                    g.setColour(recordColour.withAlpha(0.18f));
                    g.fillRoundedRectangle(contentArea.reduced(3).toFloat(), 4.0f);
                    g.setColour(recordColour.withAlpha(0.9f));
                    g.fillRect(getLocalBounds().removeFromLeft(PLAY_BUTTON_WIDTH).reduced(6, 4));
                    g.setColour(findColour(juce::TextButton::textColourOffId));
                    g.setFont(FontManager::getInstance().getUIFont(9.0f));
                    g.drawText("Recording", contentArea.reduced(6, 0),
                               juce::Justification::centredLeft, true);
                } else if (slotRecordArmed) {
                    g.setColour(recordColour.withAlpha(0.25f));
                    g.fillEllipse(centre.getX() - radius, centre.getY() - radius, radius * 2.0f,
                                  radius * 2.0f);
                    g.setColour(recordColour);
                    g.drawEllipse(centre.getX() - radius - 2.0f, centre.getY() - radius - 2.0f,
                                  (radius + 2.0f) * 2.0f, (radius + 2.0f) * 2.0f, 1.5f);
                } else {
                    g.setColour(recordColour.withAlpha(0.7f));
                    g.fillEllipse(centre.getX() - radius, centre.getY() - radius, radius * 2.0f,
                                  radius * 2.0f);
                }
            } else {
                // Stop square. While a quantized row-stop is in flight,
                // blink the icon on each beat — same affordance as the
                // play triangle's "queued" blink, just inverted intent.
                float size = 5.0f;
                auto stopColour = juce::Colours::white.withAlpha(0.4f);
                if (stopIsQueued && !blinkOn)
                    stopColour = stopColour.withAlpha(0.1f);
                g.setColour(stopColour);
                g.fillRect(juce::Rectangle<float>(centre.getX() - size, centre.getY() - size,
                                                  size * 2.0f, size * 2.0f));
            }
        }
    }
};

/// Scene-launch button (right column of the session view). Mirrors
/// ClipSlotButton's left-strip styling so the master/scene column reads as
/// part of the same row visually — strip on the left, larger icon, content
/// area to the right.
class SceneButton : public juce::TextButton {
  public:
    static constexpr int STRIP_WIDTH = ClipSlotButton::PLAY_BUTTON_WIDTH;

    bool hasAnyClip = true;      // false = row empty → render stop glyph
    bool hasAnyPlaying = false;  // true → row has at least one playing/queued clip
    bool stopIsQueued = false;   // true → quantized row-stop in flight, blink the stop icon
    bool blinkOn = false;        // toggled by SessionView timer for stop-queued blink

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override {
        // Render the button's rounded BG (via SmallButtonLookAndFeel) so the
        // master column reads as a column of buttons, not floating glyphs.
        juce::TextButton::paintButton(g, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        // Icon on the left side. Cyan when something in the scene is
        // playing/queued (matches the track play strip), white when idle
        // but available to launch. Stop square for fully-empty rows.
        auto leftArea = getLocalBounds().toFloat().removeFromLeft(STRIP_WIDTH);
        auto centre = leftArea.getCentre();

        if (hasAnyClip) {
            juce::Path triangle;
            float size = 6.0f;
            triangle.addTriangle(centre.getX() - size * 0.7f, centre.getY() - size,
                                 centre.getX() - size * 0.7f, centre.getY() + size,
                                 centre.getX() + size, centre.getY());
            g.setColour(hasAnyPlaying ? DarkTheme::getColour(DarkTheme::ACCENT_CYAN)
                                      : juce::Colours::white);
            g.fillPath(triangle);
        } else {
            float size = 5.0f;
            auto stopColour = juce::Colour(0xFFA0A0A0);
            if (stopIsQueued && !blinkOn)
                stopColour = stopColour.withAlpha(0.25f);
            g.setColour(stopColour);
            g.fillRect(juce::Rectangle<float>(centre.getX() - size, centre.getY() - size,
                                              size * 2.0f, size * 2.0f));
        }
    }
};

/// Track header button with right-click context menu and drag support for SessionView.
class TrackHeaderButton : public juce::TextButton {
  public:
    std::function<void()> onDeleteTrack;
    std::function<void(const juce::MouseEvent&)> onHeaderMouseDown;
    std::function<void(const juce::MouseEvent&)> onHeaderMouseDrag;
    std::function<void(const juce::MouseEvent&)> onHeaderMouseUp;

    void mouseDown(const juce::MouseEvent& event) override {
        if (event.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            menu.addItem(1, "Delete Track");
            auto safeThis = juce::Component::SafePointer<TrackHeaderButton>(this);
            menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result) {
                if (!safeThis)
                    return;
                if (result == 1 && safeThis->onDeleteTrack)
                    safeThis->onDeleteTrack();
            });
            return;
        }
        if (onHeaderMouseDown)
            onHeaderMouseDown(event);
        juce::TextButton::mouseDown(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (onHeaderMouseDrag)
            onHeaderMouseDrag(event);
    }

    void mouseUp(const juce::MouseEvent& event) override {
        if (onHeaderMouseUp)
            onHeaderMouseUp(event);
        juce::TextButton::mouseUp(event);
    }
};

/// Compact dB scale labels for session view mini strips.
/// Uses linear-in-dB mapping to match the TextSlider's range (-60..+6).
class MiniDbScale : public juce::Component {
  public:
    MiniDbScale() {
        setInterceptsMouseClicks(false, false);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        if (bounds.isEmpty())
            return;

        static constexpr float dbValues[] = {6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f};
        static constexpr float DB_MIN = -60.0f;
        static constexpr float DB_MAX = 6.0f;

        static constexpr float PADDING = 4.0f;
        float height = static_cast<float>(bounds.getHeight()) - 2.0f * PADDING;
        float width = static_cast<float>(bounds.getWidth());

        if (height <= 0.0f)
            return;

        g.setFont(FontManager::getInstance().getUIFont(8.0f));

        constexpr float labelH = 9.0f;
        float lastDrawnY = -1000.0f;

        for (float db : dbValues) {
            float norm = (db - DB_MIN) / (DB_MAX - DB_MIN);
            float y = PADDING + height * (1.0f - norm);

            if (std::abs(y - lastDrawnY) < labelH + 1.0f)
                continue;
            lastDrawnY = y;

            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.fillRect(0.0f, y - 0.5f, 2.0f, 1.0f);
            g.fillRect(width - 2.0f, y - 0.5f, 2.0f, 1.0f);

            int dbInt = static_cast<int>(db);
            juce::String text = juce::String(std::abs(dbInt));

            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            g.drawText(text, 0, static_cast<int>(y - labelH / 2.0f), static_cast<int>(width),
                       static_cast<int>(labelH), juce::Justification::centred, false);
        }
    }
};

}  // namespace magda
