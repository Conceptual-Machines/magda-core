#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

#include "core/DeviceInfo.hpp"
#include "core/LinkModeManager.hpp"
#include "core/ModInfo.hpp"
#include "core/ModulatorEngine.hpp"
#include "core/controllers/BindingRegistry.hpp"
#include "core/controllers/MidiLearnCoordinator.hpp"
#include "ui/components/chain/modulation/LFOCurveEditor.hpp"
#include "ui/components/chain/modulation/LFOCurveEditorWindow.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

class FollowerEditorPanel;

/**
 * @brief Animated waveform display component
 */
class WaveformDisplay : public juce::Component, private juce::Timer {
  public:
    WaveformDisplay() {
        startTimer(33);  // 30 FPS animation
    }

    ~WaveformDisplay() override {
        stopTimer();
    }

    // Pass both a snapshot pointer (used as a fallback) and a getter that
    // refetches the live ModInfo* on each paint. The raw pointer dangles
    // when the underlying mod vector reallocates (mods added/removed,
    // chain rebuild) — the getter gives us a stable lookup.
    void setModInfo(const magda::ModInfo* mod,
                    std::function<const magda::ModInfo*()> getter = nullptr) {
        mod_ = mod;
        modGetter_ = std::move(getter);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        // Always refetch through the getter when one is set — the raw mod_
        // pointer may dangle after a vector reallocation.
        const magda::ModInfo* mod = modGetter_ ? modGetter_() : mod_;
        if (!mod) {
            return;
        }

        auto bounds = getLocalBounds().toFloat();
        const float width = bounds.getWidth();
        const float height = bounds.getHeight();
        const float centerY = height * 0.5f;

        // Draw phase offset indicator line (vertical dashed line at offset position)
        if (mod->phaseOffset > 0.001f) {
            float offsetX = bounds.getX() + mod->phaseOffset * width;
            g.setColour(juce::Colours::orange.withAlpha(0.3f));
            // Draw dashed line
            const float dashLength = 3.0f;
            for (float y = bounds.getY(); y < bounds.getBottom(); y += dashLength * 2) {
                g.drawLine(offsetX, y, offsetX, juce::jmin(y + dashLength, bounds.getBottom()),
                           1.0f);
            }
        }

        // Draw waveform path (shifted by phase offset for visual representation)
        juce::Path waveformPath;
        const int numPoints = 100;

        for (int i = 0; i < numPoints; ++i) {
            float displayPhase = static_cast<float>(i) / static_cast<float>(numPoints - 1);
            // Apply phase offset to show how waveform is shifted
            float effectivePhase = std::fmod(displayPhase + mod->phaseOffset, 1.0f);
            float value = magda::ModulatorEngine::generateWaveformForMod(*mod, effectivePhase);

            // Invert value so high values are at top
            float y = centerY + (0.5f - value) * (height - 8.0f);
            float x = bounds.getX() + displayPhase * width;

            if (i == 0) {
                waveformPath.startNewSubPath(x, y);
            } else {
                waveformPath.lineTo(x, y);
            }
        }

        // Draw the waveform line
        g.setColour(juce::Colours::orange.withAlpha(0.7f));
        g.strokePath(waveformPath, juce::PathStrokeType(1.5f));

        // Draw current phase indicator (dot) - use actual phase position
        float displayX = bounds.getX() + mod->phase * width;
        float currentValue = mod->value;
        float currentY = centerY + (0.5f - currentValue) * (height - 8.0f);

        g.setColour(juce::Colours::orange);
        g.fillEllipse(displayX - 4.0f, currentY - 4.0f, 8.0f, 8.0f);

        // Draw trigger indicator dot in top-right corner
        const float triggerDotRadius = 3.0f;
        auto triggerDotBounds = juce::Rectangle<float>(
            bounds.getRight() - triggerDotRadius * 2 - 4.0f, bounds.getY() + 4.0f,
            triggerDotRadius * 2, triggerDotRadius * 2);

        // Use trigger counter to detect triggers across frame boundaries.
        // The triggered bool is only true for one 60fps tick — the 30fps paint
        // misses ~50% of them. The counter never misses.
        if (mod->triggerCount != lastSeenTriggerCount_) {
            lastSeenTriggerCount_ = mod->triggerCount;
            triggerHoldFrames_ = 4;  // Show for ~130ms at 30fps
        }

        if (triggerHoldFrames_ > 0) {
            g.setColour(juce::Colours::orange);
            g.fillEllipse(triggerDotBounds);
        } else {
            g.setColour(juce::Colours::orange.withAlpha(0.3f));
            g.drawEllipse(triggerDotBounds, 1.0f);
        }
    }

  private:
    void timerCallback() override {
        if (triggerHoldFrames_ > 0)
            triggerHoldFrames_--;
        repaint();
    }

    const magda::ModInfo* mod_ = nullptr;
    std::function<const magda::ModInfo*()> modGetter_;
    mutable uint32_t lastSeenTriggerCount_ = 0;
    mutable int triggerHoldFrames_ = 0;
};

/**
 * @brief Animated ADSR envelope display.
 *
 * Draws the attack/decay/sustain/release shape from the mod's envelope fields
 * and a moving dot at the current value, with the active stage shown as a
 * label. Read-only (the A/D/S/R/curve sliders edit the values); the stage and
 * value are overlaid from the live TE modifier by the audio bridge.
 */
class EnvelopeDisplay : public juce::Component, private juce::Timer {
  public:
    EnvelopeDisplay() {
        startTimer(33);  // 30 FPS animation
    }

    ~EnvelopeDisplay() override {
        stopTimer();
    }

    void setModInfo(const magda::ModInfo* mod,
                    std::function<const magda::ModInfo*()> getter = nullptr) {
        mod_ = mod;
        modGetter_ = std::move(getter);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const magda::ModInfo* mod = modGetter_ ? modGetter_() : mod_;
        if (!mod)
            return;

        auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        const float w = bounds.getWidth();
        const float top = bounds.getY();
        const float bottom = bounds.getBottom();
        const float h = bottom - top;

        // Distribute the horizontal space: a fixed slice for the sustain hold,
        // the rest shared by attack/decay/release in proportion to their times
        // (with a floor so a zero-length stage is still visible).
        constexpr float kSustainFrac = 0.22f;
        const float timed = w * (1.0f - kSustainFrac);
        const float a = juce::jmax(mod->envAttackMs, 1.0f);
        const float d = juce::jmax(mod->envDecayMs, 1.0f);
        const float r = juce::jmax(mod->envReleaseMs, 1.0f);
        const float sum = a + d + r;
        const float aw = timed * (a / sum);
        const float dw = timed * (d / sum);
        const float rw = timed * (r / sum);
        const float sw = w * kSustainFrac;

        const float x0 = bounds.getX();
        const float xA = x0 + aw;  // end of attack (peak)
        const float xD = xA + dw;  // end of decay (sustain level)
        const float xS = xD + sw;  // end of sustain hold
        const float xR = xS + rw;  // end of release (zero)
        const float sustainY = bottom - mod->envSustain * h;

        auto yAt = [&](float v) { return bottom - juce::jlimit(0.0f, 1.0f, v) * h; };

        juce::Path path;
        path.startNewSubPath(x0, bottom);
        appendCurve(path, x0, bottom, xA, top, mod->envAttackCurve);
        appendCurve(path, xA, top, xD, sustainY, mod->envDecayCurve);
        path.lineTo(xS, sustainY);
        appendCurve(path, xS, sustainY, xR, bottom, mod->envReleaseCurve);

        g.setColour(juce::Colours::orange.withAlpha(0.7f));
        g.strokePath(path, juce::PathStrokeType(1.5f));

        // Stage breakpoint markers
        g.setColour(juce::Colours::orange.withAlpha(0.25f));
        for (float x : {xA, xD, xS}) {
            for (float y = top; y < bottom; y += 4.0f)
                g.drawLine(x, y, x, juce::jmin(y + 2.0f, bottom), 1.0f);
        }

        // Current-value dot, placed on the segment for the active stage.
        // Stage ordinals match te::ADSRModifier::Stage (idle/attack/decay/
        // sustain/release).
        const float v = mod->value;
        float dotX = x0;
        switch (mod->envStage) {
            case 1:
                dotX = x0 + aw * juce::jlimit(0.0f, 1.0f, v);
                break;
            case 2: {
                const float denom = juce::jmax(1.0f - mod->envSustain, 1e-3f);
                dotX = xA + dw * juce::jlimit(0.0f, 1.0f, (1.0f - v) / denom);
                break;
            }
            case 3:
                dotX = (xD + xS) * 0.5f;
                break;
            case 4: {
                const float denom = juce::jmax(mod->envSustain, 1e-3f);
                dotX = xS + rw * juce::jlimit(0.0f, 1.0f, (mod->envSustain - v) / denom);
                break;
            }
            default:
                dotX = x0;
                break;
        }
        if (mod->envStage != 0) {
            g.setColour(juce::Colours::orange);
            g.fillEllipse(dotX - 3.0f, yAt(v) - 3.0f, 6.0f, 6.0f);
        }

        // Active-stage label
        static const char* kStageNames[] = {"Idle", "Attack", "Decay", "Sustain", "Release"};
        const int s = juce::jlimit(0, 4, mod->envStage);
        g.setColour(juce::Colours::orange.withAlpha(0.6f));
        g.setFont(FontManager::getInstance().getUIFont(8.0f));
        g.drawText(kStageNames[s], bounds.toNearestInt().removeFromTop(12),
                   juce::Justification::topRight);
    }

  private:
    // Append a quadratic segment whose bow is controlled by `curve` (-0.5..0.5,
    // matching the TE convention). Zero curve draws a straight line.
    static void appendCurve(juce::Path& p, float x1, float y1, float x2, float y2, float curve) {
        if (std::abs(curve) < 1e-3f) {
            p.lineTo(x2, y2);
            return;
        }
        const float cx = (x1 + x2) * 0.5f - (x2 - x1) * curve;
        const float cy = (y1 + y2) * 0.5f + (y2 - y1) * curve;
        p.quadraticTo(cx, cy, x2, y2);
    }

    void timerCallback() override {
        repaint();
    }

    const magda::ModInfo* mod_ = nullptr;
    std::function<const magda::ModInfo*()> modGetter_;
};

/**
 * @brief Scrolling history display for the Random modulator's live output.
 *
 * The random output has no deterministic waveform to draw, so we scroll a
 * short ring buffer of recent output values left-to-right (oldest -> newest),
 * fed from ModInfo::value (overlaid from te::RandomModifier on the audio
 * thread) on each 30fps tick.
 */
class RandomDisplay : public juce::Component, private juce::Timer {
  public:
    RandomDisplay() {
        history_.fill(0.0f);
        startTimer(33);  // 30 FPS
    }

    ~RandomDisplay() override {
        stopTimer();
    }

    void setModInfo(const magda::ModInfo* mod,
                    std::function<const magda::ModInfo*()> getter = nullptr) {
        mod_ = mod;
        modGetter_ = std::move(getter);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const magda::ModInfo* mod = modGetter_ ? modGetter_() : mod_;
        if (!mod)
            return;

        auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        const float w = bounds.getWidth();
        const float h = bounds.getHeight();
        const float x0 = bounds.getX();
        const float bottom = bounds.getBottom();

        // Plot the value history oldest -> newest, newest at the right edge.
        juce::Path path;
        const int n = static_cast<int>(history_.size());
        for (int i = 0; i < n; ++i) {
            const int idx = (writePos_ + i) % n;  // writePos_ is the oldest slot
            const float v = juce::jlimit(0.0f, 1.0f, history_[static_cast<size_t>(idx)]);
            const float x = x0 + (static_cast<float>(i) / static_cast<float>(n - 1)) * w;
            const float y = bottom - v * h;
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        g.setColour(juce::Colours::orange.withAlpha(0.7f));
        g.strokePath(path, juce::PathStrokeType(1.5f));

        // Current value dot on the right edge.
        const float v = juce::jlimit(0.0f, 1.0f, mod->value);
        g.setColour(juce::Colours::orange);
        g.fillEllipse(bounds.getRight() - 4.0f, bottom - v * h - 3.0f, 6.0f, 6.0f);
    }

  private:
    void timerCallback() override {
        const magda::ModInfo* mod = modGetter_ ? modGetter_() : mod_;
        if (mod) {
            history_[static_cast<size_t>(writePos_)] = mod->value;
            writePos_ = (writePos_ + 1) % static_cast<int>(history_.size());
        }
        repaint();
    }

    const magda::ModInfo* mod_ = nullptr;
    std::function<const magda::ModInfo*()> modGetter_;
    std::array<float, 96> history_{};
    int writePos_ = 0;
};

/**
 * @brief Scrollable content component for the mod matrix
 *
 * Displays all parameter links for the selected mod.
 * Each row: param_name | bipolar toggle | amount | delete button
 */
class ModMatrixContent : public juce::Component {
  public:
    static constexpr int ROW_HEIGHT = 18;

    struct LinkRow {
        magda::ControlTarget target;
        juce::String paramName;
        float amount = 0.0f;
        bool bipolar = false;
        bool enabled = true;
    };

    void setLinks(const std::vector<LinkRow>& links);
    bool isDragging() const {
        return draggingRow_ >= 0;
    }
    bool updateLinkState(magda::ControlTarget target, float amount, bool bipolar, bool enabled);

    // Callbacks
    std::function<void(magda::ControlTarget target)> onDeleteLink;
    std::function<void(magda::ControlTarget target, bool bipolar)> onToggleBipolar;
    std::function<void(magda::ControlTarget target, bool enabled)> onToggleEnabled;
    std::function<void(magda::ControlTarget target, float amount)> onAmountChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    std::vector<LinkRow> links_;
    int draggingRow_ = -1;
    float dragStartAmount_ = 0.0f;
    int dragStartX_ = 0;
};

/**
 * @brief Panel for editing modulator settings
 *
 * Shows when a mod is selected from the mods panel.
 * Displays type selector, rate control, and target info.
 *
 * Layout:
 * +------------------+
 * |    MOD NAME      |  <- Header with mod name
 * +------------------+
 * | Type: [LFO   v]  |  <- Type selector
 * +------------------+
 * |   Rate: 1.0 Hz   |  <- Rate slider
 * +------------------+
 * | Target: Device   |  <- Target info
 * |   Param Name     |
 * +------------------+
 */
class ModulatorEditorPanel : public juce::Component,
                             public magda::LinkModeManagerListener,
                             public magda::BindingRegistryListener,
                             public magda::MidiLearnCoordinatorListener,
                             private juce::Timer {
  public:
    ModulatorEditorPanel();
    ~ModulatorEditorPanel() override;

    // Set the mod to edit. liveModGetter provides a safe way to fetch the live mod
    // pointer on demand (avoids dangling pointers when the mod vector reallocates).
    void setModInfo(const magda::ModInfo& mod, const magda::ModInfo* liveMod = nullptr,
                    std::function<const magda::ModInfo*()> liveModGetter = nullptr);

    // Identify the mod's owning scope so the rate slider can register an
    // AutomationTarget with AutomationManager — drives the standard purple
    // highlight + touch-suppression that plugin-param sliders already use.
    void setOwnerPath(magda::TrackId trackId, const magda::ChainNodePath& devicePath);

    // Set a resolver for getting parameter names from device/param IDs
    void setParamNameResolver(std::function<juce::String(magda::DeviceId, int)> resolver) {
        paramNameResolver_ = std::move(resolver);
    }

    // Set the selected mod index (-1 for none)
    void setSelectedModIndex(int index);
    int getSelectedModIndex() const {
        return selectedModIndex_;
    }

    // Callbacks
    std::function<void(float rate)> onRateChanged;
    std::function<void(juce::String name)> onNameChanged;
    std::function<void(magda::LFOWaveform waveform)> onWaveformChanged;
    std::function<void(bool tempoSync)> onTempoSyncChanged;
    std::function<void(magda::SyncDivision division)> onSyncDivisionChanged;
    std::function<void(magda::LFOTriggerMode mode)> onTriggerModeChanged;
    std::function<void()> onCurveChanged;  // Fires when curve points are edited
    std::function<void()> onAdvancedClicked;
    std::function<void(float ms)> onAudioAttackChanged;
    std::function<void(float ms)> onAudioReleaseChanged;
    // Fires when any ADSR envelope control changes; the passed ModInfo carries
    // the updated env* fields (the rest mirrors the current mod).
    std::function<void(const magda::ModInfo& mod)> onEnvelopeChanged;
    // Fires when any Random control changes; the passed ModInfo carries the
    // updated random* fields (the rest mirrors the current mod).
    std::function<void(const magda::ModInfo& mod)> onRandomChanged;
    // Fires when any envelope follower control changes. The follower has its own
    // editor (FollowerEditorPanel) embedded here; this just forwards its change.
    std::function<void(const magda::ModInfo& mod)> onFollowerChanged;
    std::function<void(int modIndex, magda::ControlTarget target)> onModLinkDeleted;
    std::function<void(int modIndex, magda::ControlTarget target, bool bipolar)>
        onModLinkBipolarChanged;
    std::function<void(int modIndex, magda::ControlTarget target, bool enabled)>
        onModLinkEnabledChanged;
    std::function<void(int modIndex, magda::ControlTarget target, float amount)>
        onModLinkAmountChanged;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // LinkModeManagerListener — drives the link-mode click-to-link flow on
    // the rate slider, mirroring how ParamSlotComponent participates in
    // macro / mod link mode for device parameters.
    void macroLinkModeChanged(bool active, const magda::MacroSelection& sel) override;
    void modLinkModeChanged(bool active, const magda::ModSelection& sel) override;

    // BindingRegistry / MIDI Learn — repaint the rate-slider dot indicator
    // when a Learn'd binding is added, removed, or mid-learn.
    void bindingRegistryChanged(magda::BindingScope scope) override;
    void midiLearnStateChanged(const magda::ChainNodePath& path, int paramIndex,
                               magda::ControlTarget::Kind owner, bool learning) override;
    void midiLearnCompleted(const magda::ChainNodePath& path, int paramIndex,
                            magda::ControlTarget::Kind owner, const magda::Binding&) override;
    void midiLearnCleared(const magda::ChainNodePath& path, int paramIndex,
                          magda::ControlTarget::Kind owner, int numRemoved) override;

    // Preferred width for this panel
    static constexpr int PREFERRED_WIDTH = 150;

  private:
    int selectedModIndex_ = -1;
    magda::ModInfo currentMod_;
    magda::TrackId ownerTrackId_ = magda::INVALID_TRACK_ID;
    magda::ChainNodePath ownerDevicePath_;
    void updateRateAutomationTarget();
    void showRateSliderContextMenu();

    // True when there's any binding keyed to (ownerDevicePath_, currentMod_.id,
    // modParamIndex 0). Drives the small mapped-binding dot painted over the
    // rate slider, plus the learn-mode pulse.
    void refreshRateMidiBindingState();
    bool hasRateMidiBinding_ = false;
    bool isRateInMidiLearnMode_ = false;

    // Apply / clear link-mode click handling on the rate sliders. When a
    // link-mode source (macro or mod) is active, clicking the visible rate
    // slider creates a ModParam-kind link from the source to this mod's rate.
    void applyLinkModeToRateSliders();
    // Push a new amount for the link from the active source to this mod's
    // rate — creates the link on first call (with the given amount), updates
    // it on subsequent calls. Mirrors ParamSlotComponent's link-mode-drag.
    void writeLinkAmountFromActiveSource(float amount);
    bool linkModeActiveAndInScope_ = false;
    bool isLinkModeDrag_ = false;
    float linkModeDragStartAmount_ = 0.0f;
    float linkModeDragCurrentAmount_ = 0.0f;
    int linkModeDragStartY_ = 0;
    juce::Label linkModeAmountLabel_;
    const magda::ModInfo* liveModPtr_ = nullptr;  // Pointer to live mod for waveform animation
    std::function<const magda::ModInfo*()> liveModGetter_;  // Safe getter for timer callback

    // UI Components
    juce::Label nameLabel_;
    juce::ComboBox waveformCombo_;  // LFO shape selector (Sine, Triangle, etc.)
    WaveformDisplay waveformDisplay_;
    magda::LFOCurveEditor curveEditor_;                        // Custom waveform editor
    std::unique_ptr<magda::SvgButton> curveEditorButton_;      // Button to open external editor
    std::unique_ptr<LFOCurveEditorWindow> curveEditorWindow_;  // External editor window
    bool isCurveMode_ = false;                                 // True when waveform is Custom
    juce::ComboBox curvePresetCombo_;                          // Preset selector for curve mode
    std::unique_ptr<magda::SvgButton> savePresetButton_;       // Save preset button
    juce::TextButton syncToggle_;
    TextSlider syncDivisionSlider_{TextSlider::Format::Decimal};
    TextSlider rateSlider_{TextSlider::Format::Decimal};
    juce::ComboBox triggerModeCombo_;
    std::unique_ptr<magda::SvgButton> advancedButton_;
    TextSlider audioAttackSlider_{TextSlider::Format::Decimal};
    TextSlider audioReleaseSlider_{TextSlider::Format::Decimal};

    // ADSR envelope controls (shown only when currentMod_.type == Envelope)
    EnvelopeDisplay envelopeDisplay_;
    TextSlider envAttackSlider_{TextSlider::Format::Decimal};
    TextSlider envDecaySlider_{TextSlider::Format::Decimal};
    TextSlider envSustainSlider_{TextSlider::Format::Decimal};
    TextSlider envReleaseSlider_{TextSlider::Format::Decimal};
    TextSlider envAttackCurveSlider_{TextSlider::Format::Decimal};
    TextSlider envDecayCurveSlider_{TextSlider::Format::Decimal};
    TextSlider envReleaseCurveSlider_{TextSlider::Format::Decimal};
    bool isEnvelopeMode_ = false;
    // Helper: push the current env fields out via onEnvelopeChanged.
    void fireEnvelopeChanged();

    // Random modulator controls (shown only when currentMod_.type == Random)
    RandomDisplay randomDisplay_;
    juce::ComboBox randomTypeCombo_;  // Random / Noise distribution
    TextSlider randomShapeSlider_{TextSlider::Format::Decimal};
    TextSlider randomSmoothSlider_{TextSlider::Format::Decimal};
    TextSlider randomStepDepthSlider_{TextSlider::Format::Decimal};
    bool isRandomMode_ = false;
    // Helper: push the current random fields out via onRandomChanged.
    void fireRandomChanged();

    void updateFromMod();
    void onNameLabelEdited();
    void updateModMatrix();
    void timerCallback() override;

    // Mod matrix
    juce::Viewport modMatrixViewport_;
    ModMatrixContent modMatrixContent_;

    // The envelope follower is different enough (continuous audio tracking, no
    // waveform/rate/trigger/MIDI) that it gets its own editor, embedded here
    // and shown on top in follower mode. updateFromMod()/resized() delegate to
    // it and hide the generator controls.
    bool isFollowerMode_ = false;
    std::unique_ptr<FollowerEditorPanel> followerEditorPanel_;
    void setGeneratorControlsVisible(bool visible);
    std::function<juce::String(magda::DeviceId, int)> paramNameResolver_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulatorEditorPanel)
};

}  // namespace magda::daw::ui
