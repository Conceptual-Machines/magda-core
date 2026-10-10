#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "../themes/ActiveTheme.hpp"
#include "../themes/FontManager.hpp"
#include "core/ClipManager.hpp"

namespace magda {

namespace session_paint {

constexpr float kCornerRadius = 6.0f;
constexpr float kGlyphX = 10.0f;  // left edge of a slot's or scene's launch glyph

inline void fillTriangle(juce::Graphics& g, float x, float centreY, float size) {
    juce::Path triangle;
    triangle.addTriangle(x, centreY - size * 0.5f, x, centreY + size * 0.5f, x + size * 0.85f,
                         centreY);
    g.fillPath(triangle);
}

inline void fillSquare(juce::Graphics& g, juce::Point<float> centre, float size) {
    g.fillRect(juce::Rectangle<float>(size, size).withCentre(centre));
}

/** @brief Mono caps with the v1 label tracking. */
inline juce::Font labelFont(float size) {
    return FontManager::getInstance().getMonoFont(size).withExtraKerningFactor(0.12f);
}

}  // namespace session_paint

/** @brief A session grid cell: the launch glyph on the left, the clip name after it. */
class ClipSlotButton : public juce::TextButton {
  public:
    static constexpr int PLAY_BUTTON_WIDTH = 28;

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
    bool clipHasLaunchIntent = false;  // Remembered active slot, including while transport stopped
    bool transportIsPlaying = false;   // Visual state supplied by SessionView
    bool stopIsQueued = false;         // The track has a quantised stop pending
    bool blinkOn = false;              // Toggled by SessionView timer on the beat
    bool isSelected = false;
    bool rowSelected = false;   // The slot's scene is selected
    bool isDropTarget = false;  // A file or clip drag hovers this slot
    bool trackIsRecordArmed = false;
    bool slotRecordArmed = false;
    bool slotIsRecording = false;
    juce::Colour clipColour;
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

        if (!hasClip && slotIsRecording) {
            if (onEmptySlotRecordClick)
                onEmptySlotRecordClick();
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
                     bool /*shouldDrawButtonAsDown*/) override {
        using namespace session_paint;
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        const float centreY = bounds.getCentreY();
        const auto slotFill = ActiveTheme::getColour(ActiveTheme::SESSION_SLOT);
        const auto green = ActiveTheme::getColour(ActiveTheme::SESSION_PLAY);
        const auto red = ActiveTheme::getColour(ActiveTheme::SESSION_RECORD);

        auto fill = shouldDrawButtonAsHighlighted && !hasClip
                        ? ActiveTheme::getColour(ActiveTheme::SESSION_SLOT_HOVER)
                        : slotFill;
        auto border = ActiveTheme::getColour(ActiveTheme::SESSION_SLOT_BORDER);

        const bool playing = hasClip && clipIsPlaying && transportIsPlaying;
        const bool recording = !hasClip && slotIsRecording;
        if (hasClip) {
            const auto swatch = deriveTrackSwatch(clipColour);
            fill = slotFill.interpolatedWith(swatch, playing ? 0.52f : 0.34f);
            border =
                playing ? deriveTrackAccent(clipColour) : slotFill.interpolatedWith(swatch, 0.55f);
            if (clipIsQueued && blinkOn)
                border = green;
        } else if (recording) {
            fill = ActiveTheme::getColour(ActiveTheme::SESSION_RECORD_FILL);
            border = ActiveTheme::getColour(ActiveTheme::SESSION_RECORD_BORDER);
        }

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, kCornerRadius);
        g.setColour(border);
        g.drawRoundedRectangle(bounds, kCornerRadius, 1.0f);

        if (isGroupSlot) {
            if (hasChildClips) {
                g.setColour(transportIsPlaying && childClipIsPlaying
                                ? green
                                : ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
                fillTriangle(g, bounds.getCentreX() - 4.0f, centreY, 10.0f);
            }
            paintSelection(g, bounds);
            return;
        }

        const auto nameArea = bounds.withTrimmedLeft(static_cast<float>(PLAY_BUTTON_WIDTH) + 2.0f)
                                  .withTrimmedRight(10.0f);
        const juce::Point<float> glyphCentre{kGlyphX + 4.5f, centreY};

        if (hasClip) {
            const bool stopping = playing && stopIsQueued;
            auto glyph = ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY);
            if (stopping)
                glyph = ActiveTheme::getColour(ActiveTheme::SESSION_STOPPING);
            else if (playing || clipIsQueued)
                glyph = green;
            else if (clipHasLaunchIntent)
                glyph = green.withAlpha(0.45f);
            g.setColour(glyph);
            fillTriangle(g, kGlyphX, centreY, 10.0f);

            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
            g.setFont(FontManager::getInstance().getUIFontMedium(12.0f));
            g.drawText(getButtonText(), nameArea.toNearestInt(), juce::Justification::centredLeft,
                       true);

            if (playing && clipLength > 0.0 && sessionPlayheadPos >= 0.0)
                paintProgress(g, bounds, static_cast<float>(sessionPlayheadPos / clipLength),
                              deriveTrackAccent(clipColour));

            if (const auto* clip = ClipManager::getInstance().getClip(clipId);
                clip != nullptr && !clip->enabled) {
                // Disabled clip (#1736): dim the whole slot, mirroring the arrangement overlay.
                g.setColour(juce::Colours::black.withAlpha(0.55f));
                g.fillRoundedRectangle(bounds, kCornerRadius);
            }
        } else if (recording) {
            g.setColour(red);
            g.fillEllipse(juce::Rectangle<float>(10.0f, 10.0f).withCentre(glyphCentre));
            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
            g.setFont(FontManager::getInstance().getUIFontMedium(12.0f));
            g.drawText("Recording", nameArea.toNearestInt(), juce::Justification::centredLeft,
                       true);
        } else if (trackIsRecordArmed) {
            // A queued recording lights the ring and blinks it on the beat.
            const auto ring = slotRecordArmed
                                  ? red.withAlpha(blinkOn ? 1.0f : 0.35f)
                                  : ActiveTheme::getColour(ActiveTheme::SESSION_ARM_RING);
            g.setColour(ring);
            g.drawEllipse(juce::Rectangle<float>(10.0f, 10.0f).withCentre(glyphCentre), 1.2f);
        } else if (shouldDrawButtonAsHighlighted) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::SESSION_SLOT_GLYPH));
            fillSquare(g, glyphCentre, 9.0f);
        }

        paintSelection(g, bounds);
        if (isDropTarget) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY).withAlpha(0.5f));
            g.fillRoundedRectangle(bounds, kCornerRadius);
        }
    }

  private:
    void paintSelection(juce::Graphics& g, juce::Rectangle<float> bounds) const {
        if (!isSelected && !rowSelected)
            return;
        const auto blue = ActiveTheme::getColour(ActiveTheme::SESSION_SELECTION);
        g.setColour(blue.withAlpha(0.08f));
        g.fillRoundedRectangle(bounds, session_paint::kCornerRadius);
        g.setColour(blue);
        g.drawRoundedRectangle(bounds, session_paint::kCornerRadius, 1.0f);
    }

    static void paintProgress(juce::Graphics& g, juce::Rectangle<float> bounds, float progress,
                              juce::Colour colour) {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path outline;
        outline.addRoundedRectangle(bounds, session_paint::kCornerRadius);
        g.reduceClipRegion(outline);
        g.setColour(colour);
        g.fillRect(bounds.withTop(bounds.getBottom() - 3.0f)
                       .withWidth(bounds.getWidth() * juce::jlimit(0.0f, 1.0f, progress)));
    }
};

/** @brief Scene button in the scenes column: number, launch glyph, name. */
class SceneButton : public juce::TextButton {
  public:
    static constexpr int LAUNCH_ZONE_WIDTH = 40;  // number + triangle launch; the name selects

    int sceneNumber = 0;
    bool hasAnyClip = true;      // false = row empty → render stop glyph
    bool hasAnyPlaying = false;  // true → row has at least one playing/queued clip
    bool isSelected = false;
    bool stopIsQueued = false;  // true → quantized row-stop in flight, blink the stop icon
    bool blinkOn = false;       // toggled by SessionView timer for stop-queued blink

    std::function<void()> onLaunch;
    std::function<void()> onSelect;

    void clicked() override {}

    void mouseUp(const juce::MouseEvent& event) override {
        juce::TextButton::mouseUp(event);
        if (!event.mouseWasClicked() || event.mods.isPopupMenu())
            return;
        const auto& callback = event.x < LAUNCH_ZONE_WIDTH ? onLaunch : onSelect;
        if (callback)
            callback();
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                     bool /*shouldDrawButtonAsDown*/) override {
        using namespace session_paint;
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        const float centreY = bounds.getCentreY();

        auto fill = ActiveTheme::getColour(isSelected ? ActiveTheme::SESSION_SCENE_SELECTED
                                                      : ActiveTheme::SESSION_SCENE);
        if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter(0.08f);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, kCornerRadius);
        g.setColour(ActiveTheme::getColour(isSelected ? ActiveTheme::SESSION_SELECTION
                                           : hasAnyPlaying
                                               ? ActiveTheme::SESSION_SCENE_PLAYING_BORDER
                                               : ActiveTheme::SESSION_SCENE_BORDER));
        g.drawRoundedRectangle(bounds, kCornerRadius, 1.0f);

        g.setColour(ActiveTheme::getColour(ActiveTheme::SESSION_LABEL));
        g.setFont(FontManager::getInstance().getMonoFont(10.0f));
        g.drawText(juce::String(sceneNumber),
                   juce::Rectangle<float>(kGlyphX, 0.0f, 14.0f, bounds.getHeight()),
                   juce::Justification::centredLeft, false);

        constexpr float glyphX = 26.0f;
        if (hasAnyClip) {
            g.setColour(ActiveTheme::getColour(hasAnyPlaying ? ActiveTheme::SESSION_PLAY
                                                             : ActiveTheme::TEXT_SECONDARY));
            fillTriangle(g, glyphX, centreY, 10.0f);
        } else {
            g.setColour(ActiveTheme::getColour(ActiveTheme::SESSION_STOP_IDLE)
                            .withAlpha(stopIsQueued && !blinkOn ? 0.25f : 1.0f));
            fillSquare(g, {glyphX + 4.5f, centreY}, 9.0f);
        }

        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        g.setFont(FontManager::getInstance().getUIFontMedium(12.0f));
        g.drawText(getButtonText(),
                   bounds.withTrimmedLeft(static_cast<float>(LAUNCH_ZONE_WIDTH) + 4.0f)
                       .withTrimmedRight(8.0f)
                       .toNearestInt(),
                   juce::Justification::centredLeft, true);
    }
};

/** @brief Track header button with right-click context menu and drag support for SessionView. */
class TrackHeaderButton : public juce::TextButton {
  public:
    std::function<void()> onDeleteTrack;
    std::function<void()> onGroupSelectedTracks;
    std::function<void()> onUngroupTracks;
    std::function<bool()> canGroupSelectedTracks;
    std::function<bool()> canUngroupTracks;
    std::function<void(const juce::MouseEvent&)> onHeaderMouseDown;
    std::function<void(const juce::MouseEvent&)> onHeaderMouseDrag;
    std::function<void(const juce::MouseEvent&)> onHeaderMouseUp;

    void mouseDown(const juce::MouseEvent& event) override {
        if (event.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            const bool canGroup = canGroupSelectedTracks && canGroupSelectedTracks();
            const bool canUngroup = canUngroupTracks && canUngroupTracks();
            if (canUngroup) {
                menu.addItem(3, "Ungroup tracks");
                menu.addSeparator();
            }
            if (canGroup) {
                menu.addItem(1, "Group tracks");
                menu.addSeparator();
            }
            menu.addItem(2, "Delete Track");
            auto safeThis = juce::Component::SafePointer<TrackHeaderButton>(this);
            menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result) {
                if (!safeThis)
                    return;
                if (result == 1 && safeThis->onGroupSelectedTracks)
                    safeThis->onGroupSelectedTracks();
                else if (result == 2 && safeThis->onDeleteTrack)
                    safeThis->onDeleteTrack();
                else if (result == 3 && safeThis->onUngroupTracks)
                    safeThis->onUngroupTracks();
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

    void setTrackColour(juce::Colour c) {
        if (trackColour_ != c) {
            trackColour_ = c;
            repaint();
        }
    }

    /** @brief Follows the Track color preference: a full-bar fill, or a spine on the left. */
    void setFullBar(bool fullBar) {
        if (fullBar_ != fullBar) {
            fullBar_ = fullBar;
            repaint();
        }
    }

    void setSelected(bool selected) {
        if (selected_ != selected) {
            selected_ = selected;
            repaint();
        }
    }

    void setMidiActivity(float level) {
        level = juce::jlimit(0.0f, 1.0f, level);
        if (std::abs(level - midiActivity_) > 0.01f) {
            midiActivity_ = level;
            repaint();
        }
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                     bool /*shouldDrawButtonAsDown*/) override {
        constexpr int kSpineWidth = 5;
        const auto bounds = getLocalBounds().toFloat();
        const bool coloured = trackColour_ != juce::Colour(0xFF444444);
        const bool filled = fullBar_ && coloured;
        const auto swatch = deriveTrackSwatch(trackColour_);

        juce::Graphics::ScopedSaveState state(g);
        juce::Path outline;
        outline.addRoundedRectangle(bounds, session_paint::kCornerRadius);
        g.reduceClipRegion(outline);

        // Selection as the arrangement draws it: Spine lifts the head, Full bar keeps the
        // colour and adds a light line under it.
        auto fill = filled      ? swatch
                    : selected_ ? ActiveTheme::getColour(ActiveTheme::DEVICE_LINE2)
                                : ActiveTheme::getColour(ActiveTheme::SURFACE);
        if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter(0.06f);
        g.fillAll(fill);

        auto content = getLocalBounds().reduced(14, 0);
        if (filled && selected_) {
            g.setColour(swatch.brighter(0.6f));
            g.fillRect(bounds.withTop(bounds.getBottom() - 2.0f));
        } else if (!fullBar_ && coloured) {
            g.setColour(swatch);
            g.fillRect(bounds.withWidth(static_cast<float>(kSpineWidth)));
            content.removeFromLeft(kSpineWidth);
        }

        constexpr float kDotSize = 9.0f;
        const auto dot = juce::Rectangle<float>(kDotSize, kDotSize)
                             .withCentre({static_cast<float>(content.getRight()) - kDotSize * 0.5f,
                                          bounds.getCentreY()});
        g.setColour(filled ? juce::Colours::white.withAlpha(0.4f)
                           : ActiveTheme::getColour(ActiveTheme::TEXT_DIM).withAlpha(0.6f));
        g.fillEllipse(dot);
        if (midiActivity_ > 0.01f) {
            g.setColour(juce::Colour(0xFF00FFFF).withAlpha(midiActivity_));
            g.fillEllipse(dot);
        }
        content.removeFromRight(static_cast<int>(kDotSize) + 8);

        g.setColour(filled ? juce::Colours::white
                           : ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        g.setFont(FontManager::getInstance().getHeadingFont(14.0f));
        g.drawText(getButtonText(), content, juce::Justification::centredLeft, true);
    }

  private:
    juce::Colour trackColour_;
    bool fullBar_ = false;
    bool selected_ = false;
    float midiActivity_ = 0.0f;
};

/** @brief A track's cell in the stop row, or STOP ALL in the scenes column. */
class SessionStopButton : public juce::Button {
  public:
    explicit SessionStopButton(juce::String label = {})
        : juce::Button("Stop"), label_(std::move(label)) {
        setTooltip(label_.isEmpty() ? "Stop the track's clip" : "Stop all clips");
    }

    bool live = false;          // the track has a clip playing or queued
    bool stopIsQueued = false;  // blink until the quantised stop lands
    bool blinkOn = false;

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                     bool /*shouldDrawButtonAsDown*/) override {
        using namespace session_paint;
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        auto fill = ActiveTheme::getColour(ActiveTheme::SESSION_STOP_ROW);
        if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter(0.08f);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, kCornerRadius);
        g.setColour(ActiveTheme::getColour(ActiveTheme::SESSION_CONTROL_BORDER));
        g.drawRoundedRectangle(bounds, kCornerRadius, 1.0f);

        const auto square = ActiveTheme::getColour(live ? ActiveTheme::SESSION_STOP_LIVE
                                                        : ActiveTheme::SESSION_STOP_IDLE)
                                .withAlpha(stopIsQueued && !blinkOn ? 0.25f : 1.0f);
        constexpr float kSquare = 9.0f;
        if (label_.isEmpty()) {
            g.setColour(square);
            fillSquare(g, bounds.getCentre(), kSquare);
            return;
        }

        const auto font = labelFont(9.5f);
        const float textWidth = juce::GlyphArrangement::getStringWidth(font, label_);
        const float left = bounds.getCentreX() - (kSquare + 8.0f + textWidth) * 0.5f;
        g.setColour(square);
        fillSquare(g, {left + kSquare * 0.5f, bounds.getCentreY()}, kSquare);
        g.setColour(ActiveTheme::getColour(ActiveTheme::SESSION_STOP_IDLE));
        g.setFont(font);
        g.drawText(label_,
                   juce::Rectangle<float>(left + kSquare + 8.0f, bounds.getY(), textWidth + 2.0f,
                                          bounds.getHeight()),
                   juce::Justification::centredLeft, false);
    }

  private:
    juce::String label_;
};

/** @brief The dashed "+" cell below the last scene; adds a scene. */
class AddSceneCell : public juce::Button {
  public:
    AddSceneCell() : juce::Button("Add Scene") {
        setTooltip("Add scene");
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                     bool /*shouldDrawButtonAsDown*/) override {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        juce::Path outline;
        outline.addRoundedRectangle(bounds, session_paint::kCornerRadius);
        juce::Path dashed;
        const float dashes[] = {3.0f, 3.0f};
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(ActiveTheme::getColour(shouldDrawButtonAsHighlighted
                                               ? ActiveTheme::SESSION_CONTROL_BORDER
                                               : ActiveTheme::SESSION_SLOT_BORDER));
        g.fillPath(dashed);

        const auto centre = bounds.getCentre();
        g.setColour(ActiveTheme::getColour(ActiveTheme::SESSION_LABEL));
        g.fillRect(juce::Rectangle<float>(10.0f, 1.0f).withCentre(centre));
        g.fillRect(juce::Rectangle<float>(1.0f, 10.0f).withCentre(centre));
    }
};

}  // namespace magda
