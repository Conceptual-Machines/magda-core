#include "MixerView.hpp"

#include <cmath>
#include <set>
#include <unordered_map>

#include "../../audio/AudioBridge.hpp"
#include "../../audio/MeteringBuffer.hpp"
#include "../../audio/MidiBridge.hpp"
#include "../../core/RackInfo.hpp"
#include "../../engine/AudioEngine.hpp"
#include "../../engine/PluginWindowManager.hpp"
#include "../../engine/TracktionEngineWrapper.hpp"
#include "../../profiling/PerformanceProfiler.hpp"
#include "../components/mixer/LevelMeter.hpp"
#include "../components/mixer/LevelMeterScale.hpp"
#include "../components/mixer/RoutingSyncHelper.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "../utils/SelectionPolicy.hpp"
#include "core/ChainNodePath.hpp"
#include "core/Config.hpp"
#include "core/SelectionManager.hpp"
#include "core/StringTable.hpp"
#include "core/TechnicalText.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

// "Add Send" row — "Add Send" label on the left, square "+" on the right
// aligned with the existing send rows' delete (x) button. Whole row is one
// button; click anywhere fires the destination picker.
class AddSendButton : public juce::Button {
  public:
    AddSendButton() : juce::Button("AddSend") {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) override {
        auto bounds = getLocalBounds();
        constexpr int plusWidth = 16;  // matches send row's delete button width
        auto plusRect = bounds.removeFromRight(plusWidth);

        auto textBg = DarkTheme::getColour(DarkTheme::BUTTON_NORMAL);
        if (isDown)
            textBg = textBg.darker(0.2f);
        else if (isHighlighted)
            textBg = textBg.brighter(0.1f);
        g.setColour(textBg);
        g.fillRect(bounds);

        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        g.drawText("Add Send", bounds.reduced(6, 0), juce::Justification::centredLeft);

        // Inverted from the Add Send row: text colour as background, button
        // background as the "+" glyph colour.
        auto plusBg = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);
        if (isDown)
            plusBg = plusBg.darker(0.2f);
        else if (isHighlighted)
            plusBg = plusBg.brighter(0.1f);
        g.setColour(plusBg);
        g.fillRect(plusRect);

        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.setColour(DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        g.drawText("+", plusRect, juce::Justification::centred);
    }
};

// dB conversion helpers
namespace {
constexpr float MIN_DB = level_meter_scale::minDb;
constexpr float MAX_DB = level_meter_scale::maxDb;
constexpr int DEFAULT_CHANNEL_WIDTH = 100;
constexpr int MIN_CHANNEL_WIDTH = 80;
constexpr int MAX_CHANNEL_WIDTH = 180;

// Convert linear gain (0-1) to dB
float gainToDb(float gain) {
    return level_meter_scale::gainToDb(gain);
}

// Convert dB to linear gain
float dbToGain(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

// Convert dB to normalized meter position (0-1) with power curve
// Used consistently for meters, labels, and faders across the app
float dbToMeterPos(float db) {
    return level_meter_scale::dbToMeterPos(db);
}

// Convert meter position back to dB (inverse of dbToMeterPos)
float meterPosToDb(float pos) {
    return level_meter_scale::meterPosToDb(pos);
}

// Multi-track edit fan-out: when a non-master strip is part of a
// multi-selection, every selected track receives the same edit. Otherwise
// only the clicked track is touched. Master is always single-track.
std::vector<TrackId> getMultiEditTargets(TrackId clickedId, bool isMaster) {
    auto& sel = SelectionManager::getInstance();
    if (!isMaster && sel.isTrackSelected(clickedId) && sel.getSelectedTrackCount() > 1) {
        const auto& set = sel.getSelectedTracks();
        return std::vector<TrackId>(set.begin(), set.end());
    }
    return {clickedId};
}

int effectiveMixerChannelWidth(TrackId trackId) {
    const auto& metrics = MixerMetrics::getInstance();
    if (auto* track = TrackManager::getInstance().getTrack(trackId)) {
        if (track->mixerChannelWidth > 0)
            return juce::jlimit(MIN_CHANNEL_WIDTH, MAX_CHANNEL_WIDTH, track->mixerChannelWidth);
    }
    return juce::jlimit(MIN_CHANNEL_WIDTH, MAX_CHANNEL_WIDTH,
                        metrics.channelWidth > 0 ? metrics.channelWidth : DEFAULT_CHANNEL_WIDTH);
}

int effectiveMixerFaderTopInset(TrackId trackId) {
    if (auto* track = TrackManager::getInstance().getTrack(trackId))
        return juce::jlimit(MixerMetrics::minFaderTopInset, MixerMetrics::maxFaderTopInset,
                            track->mixerFaderTopInset);
    return MixerMetrics::minFaderTopInset;
}

int storedMixerChannelWidth(TrackId trackId) {
    if (auto* track = TrackManager::getInstance().getTrack(trackId))
        return track->mixerChannelWidth;
    return 0;
}

int storedMixerFaderTopInset(TrackId trackId) {
    if (auto* track = TrackManager::getInstance().getTrack(trackId))
        return track->mixerFaderTopInset;
    return 0;
}

}  // namespace

// Use shared LevelMeter component (extracted to components/mixer/LevelMeter.hpp)
// MixerView::ChannelStrip::LevelMeter is declared as a forward in MixerView.hpp,
// so we alias it here to the shared component.
class MixerView::ChannelStrip::LevelMeter : public magda::LevelMeter {
  public:
    using magda::LevelMeter::LevelMeter;
};

// Send area resize handle (horizontal, between sends viewport and fader)
class MixerView::ChannelStrip::SendResizeHandle : public juce::Component {
  public:
    SendResizeHandle() {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void paint(juce::Graphics& g) override {
        // Single subtle line, highlights on hover
        g.setColour(isHovering_ ? DarkTheme::getColour(DarkTheme::ACCENT_BLUE)
                                : DarkTheme::getColour(DarkTheme::SEPARATOR));
        int y = getHeight() / 2;
        g.fillRect(4, y, getWidth() - 8, 2);
    }

    void mouseEnter(const juce::MouseEvent& /*event*/) override {
        isHovering_ = true;
        repaint();
    }

    void mouseExit(const juce::MouseEvent& /*event*/) override {
        isHovering_ = false;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override {
        isDragging_ = true;
        dragStartY_ = event.getScreenY();
        lastMods_ = event.mods;
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (!isDragging_ || !onResize)
            return;
        int deltaY = event.getScreenY() - dragStartY_;
        lastMods_ = event.mods;
        onResize(deltaY, event.mods);
    }

    void mouseUp(const juce::MouseEvent& event) override {
        isDragging_ = false;
        isHovering_ = false;
        if (onResizeEnd)
            onResizeEnd(event.mods);
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override {
        if (onReset)
            onReset(event.mods);
    }

    std::function<void(int deltaY, const juce::ModifierKeys& mods)> onResize;
    std::function<void(const juce::ModifierKeys& mods)> onResizeEnd;
    std::function<void(const juce::ModifierKeys& mods)> onReset;

  private:
    bool isHovering_ = false;
    bool isDragging_ = false;
    juce::ModifierKeys lastMods_;
    int dragStartY_ = 0;
};

// dB scale component — draws tick marks and dB labels, resizes with fader area
class MixerView::ChannelStrip::DbScale : public juce::Component {
  public:
    DbScale() {
        setInterceptsMouseClicks(false, false);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        if (bounds.isEmpty())
            return;

        const auto& metrics = MixerMetrics::getInstance();

        const float dbValues[] = {6.0f,   3.0f,   0.0f,   -3.0f,  -6.0f,
                                  -12.0f, -18.0f, -24.0f, -36.0f, -48.0f};

        // The scale sits to the right of the meter and spans the same vertical
        // travel — labels can overflow above/below by half a line.
        float paddingTop = metrics.labelTextHeight / 2.0f + 1.0f;
        float paddingBottom = metrics.labelTextHeight / 2.0f;
        float top = paddingTop;
        float height = static_cast<float>(bounds.getHeight()) - paddingTop - paddingBottom;
        float totalWidth = static_cast<float>(bounds.getWidth());

        const float tickShort = metrics.tickWidth();  // regular tick: ~5px
        const float tickLong = tickShort * 1.8f;      // 0 dB tick is noticeably longer
        const float labelLeftPad = tickLong + 2.0f;   // labels start past the longest tick
        const float labelWidth = totalWidth - labelLeftPad;

        const juce::Font baseFont = FontManager::getInstance().getUIFont(metrics.labelFontSize);
        const juce::Font boldFont = baseFont.boldened();

        float minSpacing = metrics.labelTextHeight + 2.0f;
        float lastDrawnY = -1000.0f;

        for (float db : dbValues) {
            float faderPos = dbToMeterPos(db);
            float y = top + height * (1.0f - faderPos);

            if (std::abs(y - lastDrawnY) < minSpacing)
                continue;
            lastDrawnY = y;

            const bool isZero = std::abs(db) < 0.01f;
            float tickHeight = metrics.tickHeight();
            float tickW = isZero ? tickLong : tickShort;

            g.setColour(DarkTheme::getColour(isZero ? DarkTheme::TEXT_PRIMARY : DarkTheme::BORDER));
            g.fillRect(0.0f, y - tickHeight / 2.0f, tickW, tickHeight);

            juce::String labelText;
            int dbInt = static_cast<int>(db);
            if (db <= MIN_DB) {
                labelText = juce::String::charToString(0x221E);
            } else {
                labelText = juce::String(std::abs(dbInt));
            }

            g.setFont(isZero ? boldFont : baseFont);
            g.setColour(
                DarkTheme::getColour(isZero ? DarkTheme::TEXT_PRIMARY : DarkTheme::TEXT_SECONDARY));

            float textHeight = metrics.labelTextHeight;
            float textY = y - textHeight / 2.0f;
            g.drawText(labelText, static_cast<int>(labelLeftPad), static_cast<int>(textY),
                       static_cast<int>(labelWidth), static_cast<int>(textHeight),
                       juce::Justification::centredLeft, false);
        }
    }
};

// Channel strip implementation
MixerView::ChannelStrip::ChannelStrip(const TrackInfo& track, AudioEngine* audioEngine,
                                      bool isMaster)
    : trackId_(track.id),
      trackType_(track.type),
      isMaster_(isMaster),
      isChildTrack_(track.hasParent()),
      trackColour_(track.colour),
      trackName_(track.name),
      audioEngine_(audioEngine) {
    setOpaque(true);
    setupControls();
    updateFromTrack(track, true);
}

MixerView::ChannelStrip::~ChannelStrip() = default;

int MixerView::ChannelStrip::preferredChannelWidth() const {
    return effectiveMixerChannelWidth(trackId_);
}

int MixerView::ChannelStrip::faderTopInset() const {
    return effectiveMixerFaderTopInset(trackId_);
}

void MixerView::ChannelStrip::updateFromTrack(const TrackInfo& track, bool syncMiniChain) {
    bool wasChild = isChildTrack_;
    isChildTrack_ = track.hasParent();
    bool colourChanged = trackColour_ != track.colour;
    trackColour_ = track.colour;
    trackName_ = track.name;
    if (isChildTrack_ != wasChild || colourChanged)
        repaint();

    if (trackLabel) {
        trackLabel->setText(isMaster_ ? magda::technicalText(magda::TechnicalTextToken::Master)
                                      : track.name,
                            juce::dontSendNotification);
    }
    if (volumeSlider && !volumeSlider->isBeingDragged()) {
        float db = gainToDb(track.volume);
        float faderPos = dbToMeterPos(db);
        volumeSlider->setValue(faderPos, juce::dontSendNotification);
    }
    if (panSlider && !panSlider->isBeingDragged()) {
        panSlider->setValue(track.pan, juce::dontSendNotification);
    }
    if (muteButton) {
        muteButton->setToggleState(track.muted, juce::dontSendNotification);
    }
    if (chordSpeakerButton) {
        chordSpeakerButton->refresh();
    }
    if (soloButton) {
        soloButton->setToggleState(track.soloed, juce::dontSendNotification);
    }
    if (recordButton) {
        recordButton->setToggleState(track.recordArmed, juce::dontSendNotification);
    }
    if (monitorButton) {
        monitorButton->refresh();
    }

    // Sync mini FX chain rows only when the device list may have changed. Mixer
    // property updates are frequent and should not rebuild chain signatures.
    if (syncMiniChain && !isMaster_)
        syncMiniChainRows(track);

    // Sync send slots
    if (!isMaster_) {
        bool sendsCountChanged = sendSlots_.size() != track.sends.size();
        if (sendsCountChanged) {
            rebuildSendSlots(track.sends);
        } else {
            // Update existing slots in-place
            for (size_t i = 0; i < sendSlots_.size(); ++i) {
                auto& slot = sendSlots_[i];
                const auto& send = track.sends[i];
                if (slot->levelSlider && !slot->levelSlider->isBeingDragged())
                    slot->levelSlider->setValue(send.level, juce::dontSendNotification);
                // Update dest name
                if (slot->nameLabel && send.destTrackId != INVALID_TRACK_ID) {
                    if (auto* destTrack = TrackManager::getInstance().getTrack(send.destTrackId))
                        slot->nameLabel->setText(destTrack->name, juce::dontSendNotification);
                }
            }
        }

        // Sync routing selectors from current track state
        if (audioEngine_ && audioInSelector && audioOutSelector && midiInSelector &&
            midiOutSelector) {
            auto* deviceManager = audioEngine_->getDeviceManager();
            auto* device = deviceManager ? deviceManager->getCurrentAudioDevice() : nullptr;
            auto* midiBridge = audioEngine_->getMidiBridge();
            juce::BigInteger enabledIn, enabledOut;
            std::map<int, juce::String> teInputDeviceNames;
            if (auto* bridge = audioEngine_->getAudioBridge()) {
                enabledIn = bridge->getEnabledInputChannels();
                enabledOut = bridge->getEnabledOutputChannels();
                teInputDeviceNames = bridge->getInputDeviceNamesByChannel();
            }
            RoutingSyncHelper::syncSelectorsFromTrack(
                track, audioInSelector.get(), midiInSelector.get(), audioOutSelector.get(),
                midiOutSelector.get(), midiBridge, device, trackId_, outputTrackMapping_,
                midiOutputTrackMapping_, &inputTrackMapping_, enabledIn, enabledOut, nullptr,
                teInputDeviceNames);
        }
    }

    repaint();
}

void MixerView::ChannelStrip::setupControls() {
    // Track label
    trackLabel = std::make_unique<juce::Label>();
    trackLabel->setText(isMaster_ ? magda::technicalText(magda::TechnicalTextToken::Master)
                                  : trackName_,
                        juce::dontSendNotification);
    trackLabel->setJustificationType(juce::Justification::centred);
    trackLabel->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    trackLabel->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    trackLabel->setFont(FontManager::getInstance().getUIFont(10.0f));
    trackLabel->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*trackLabel);

    // Pan slider (horizontal TextSlider)
    panSlider = std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Pan);
    panSlider->setOrientation(daw::ui::TextSlider::Orientation::Horizontal);
    panSlider->setRange(-1.0, 1.0, 0.01);
    panSlider->setValue(0.0, juce::dontSendNotification);
    panSlider->setFont(FontManager::getInstance().getUIFont(10.0f));
    panSlider->onValueChanged = [this](double val) {
        auto& sel = SelectionManager::getInstance();
        const bool multi =
            !isMaster_ && sel.isTrackSelected(trackId_) && sel.getSelectedTrackCount() > 1;
        if (multi) {
            if (multiTrackBasePans_.empty()) {
                auto& tm = TrackManager::getInstance();
                for (auto tid : sel.getSelectedTracks())
                    if (auto* t = tm.getTrack(tid))
                        multiTrackBasePans_[tid] = t->pan;
                multiTrackDragStartPan_ = val;
            }
            const double delta = val - multiTrackDragStartPan_;
            for (auto& [tid, basePan] : multiTrackBasePans_) {
                float newPan = juce::jlimit(-1.0f, 1.0f, static_cast<float>(basePan + delta));
                UndoManager::getInstance().executeCommand(
                    std::make_unique<SetTrackPanCommand>(tid, newPan));
            }
        } else {
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackPanCommand>(trackId_, static_cast<float>(val)));
        }
    };
    panSlider->onDragEnd = [this]() { multiTrackBasePans_.clear(); };
    if (!isMaster_) {
        AutomationTarget panTarget;
        panTarget.kind = ControlTarget::Kind::TrackPan;
        panTarget.devicePath = magda::ChainNodePath::trackLevel(trackId_);
        panSlider->setAutomationTarget(panTarget);
    }
    addAndMakeVisible(*panSlider);

    // Level meter
    levelMeter = std::make_unique<LevelMeter>();
    addAndMakeVisible(*levelMeter);

    // dB scale (ticks + labels between fader and meter)
    dbScale_ = std::make_unique<DbScale>();
    addAndMakeVisible(*dbScale_);

    // Peak / fader-value readout above the fader (replaces the old "-inf"
    // slot). Mono font keeps the digits in a tabular grid so they don't
    // shift sideways as the value changes.
    peakLabel = std::make_unique<ClickableLabel>();
    peakLabel->setText("-inf", juce::dontSendNotification);
    peakLabel->setJustificationType(juce::Justification::centred);
    peakLabel->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    peakLabel->setFont(FontManager::getInstance().getMonoFont(10.0f));
    peakLabel->setTooltip("Click to reset peak");
    peakLabel->onClick = [this]() { resetPeak(); };
    addAndMakeVisible(*peakLabel);

    // Volume slider (vertical TextSlider, 0-1 range with power curve mapping)
    volumeSlider = std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decibels);
    volumeSlider->setOrientation(daw::ui::TextSlider::Orientation::Vertical);
    volumeSlider->setRange(0.0, 1.0, 0.001);
    volumeSlider->setValue(dbToMeterPos(0.0f), juce::dontSendNotification);  // 0 dB
    volumeSlider->setFont(FontManager::getInstance().getUIFont(9.0f));
    // Display dB text via custom formatter
    volumeSlider->setValueFormatter([](double pos) -> juce::String {
        float db = meterPosToDb(static_cast<float>(pos));
        if (db <= MIN_DB)
            return "-inf";
        if (std::abs(db) < 0.05f)
            db = 0.0f;
        return juce::String(db, 1);
    });
    // Parse typed dB input
    volumeSlider->setValueParser([](const juce::String& text) -> double {
        auto t = text.trim();
        if (t.endsWithIgnoreCase("db"))
            t = t.dropLastCharacters(2).trim();
        if (t.equalsIgnoreCase("-inf") || t.equalsIgnoreCase("inf"))
            return 0.0;
        float db = t.getFloatValue();
        return static_cast<double>(dbToMeterPos(db));
    });
    volumeSlider->onValueChanged = [this](double pos) {
        const float currentDb = meterPosToDb(static_cast<float>(pos));
        const float currentGain = dbToGain(currentDb);

        auto& sel = SelectionManager::getInstance();
        const bool multi =
            !isMaster_ && sel.isTrackSelected(trackId_) && sel.getSelectedTrackCount() > 1;
        if (multi) {
            if (multiTrackBaseVolumes_.empty()) {
                auto& tm = TrackManager::getInstance();
                for (auto tid : sel.getSelectedTracks())
                    if (auto* t = tm.getTrack(tid))
                        multiTrackBaseVolumes_[tid] = t->volume;
                multiTrackDragStartDb_ = currentDb;
            }
            const double deltaDb = currentDb - multiTrackDragStartDb_;
            for (auto& [tid, baseVol] : multiTrackBaseVolumes_) {
                float baseDb = gainToDb(baseVol);
                float newDb = juce::jlimit(MIN_DB, MAX_DB, static_cast<float>(baseDb + deltaDb));
                float newGain = dbToGain(newDb);
                UndoManager::getInstance().executeCommand(
                    std::make_unique<SetTrackVolumeCommand>(tid, newGain));
            }
        } else {
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackVolumeCommand>(trackId_, currentGain));
        }
    };
    volumeSlider->onDragEnd = [this]() { multiTrackBaseVolumes_.clear(); };
    // Suppress the inner dB text — the readout is shown above the fader by
    // peakLabel, and the slider itself only paints the thumb on top of the
    // LevelMeter that shares its bounds.
    volumeSlider->setShowText(false);
    if (!isMaster_) {
        AutomationTarget volTarget;
        volTarget.kind = ControlTarget::Kind::TrackVolume;
        volTarget.devicePath = magda::ChainNodePath::trackLevel(trackId_);
        volumeSlider->setAutomationTarget(volTarget);
    }
    addAndMakeVisible(*volumeSlider);

    // Mute speaker toggle, matching the track header and inspector controls.
    muteButton = std::make_unique<magda::SvgButton>(
        "mute", BinaryData::master_on_svg, BinaryData::master_on_svgSize,
        BinaryData::master_off_svg, BinaryData::master_off_svgSize);
    muteButton->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    muteButton->setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));
    muteButton->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::STATUS_WARNING));
    muteButton->setIconPadding(3.5f);
    muteButton->setTooltip(tr("tracks.mute.tooltip"));
    muteButton->setClickingTogglesState(true);
    muteButton->onClick = [this]() {
        const bool newState = muteButton->getToggleState();
        for (auto tid : getMultiEditTargets(trackId_, isMaster_))
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackMuteCommand>(tid, newState));
    };
    addAndMakeVisible(*muteButton);

    // Chord-track audition: the same 3-state control (Silent / Audible / Solo) as
    // the chord track header, folding mute / solo / monitor into one chord glyph.
    chordSpeakerButton = std::make_unique<magda::ChordAuditionControl>();
    chordSpeakerButton->getTrackId = [this]() { return trackId_; };
    addChildComponent(*chordSpeakerButton);

    // Solo target toggle, matching the track header.
    soloButton = std::make_unique<magda::SvgButton>(
        "solo", BinaryData::solo_off_svg, BinaryData::solo_off_svgSize, BinaryData::solo_on_svg,
        BinaryData::solo_on_svgSize);
    soloButton->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    soloButton->setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));
    soloButton->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    soloButton->setIconPadding(5.0f);
    soloButton->setTooltip(tr("tracks.solo.tooltip"));
    soloButton->setClickingTogglesState(true);
    soloButton->onClick = [this]() {
        const bool newState = soloButton->getToggleState();
        for (auto tid : getMultiEditTargets(trackId_, isMaster_))
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackSoloCommand>(tid, newState));
    };
    addAndMakeVisible(*soloButton);

    // Record arm button (not on master)
    if (!isMaster_) {
        recordButton = std::make_unique<magda::SvgButton>(
            "record", BinaryData::track_record_off_svg, BinaryData::track_record_off_svgSize,
            BinaryData::track_record_on_svg, BinaryData::track_record_on_svgSize);
        recordButton->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
        recordButton->setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));
        recordButton->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::STATUS_ERROR));
        recordButton->setIconPadding(5.0f);
        recordButton->setTooltip(tr("tracks.record.tooltip"));
        recordButton->setClickingTogglesState(true);
        recordButton->onClick = [this]() {
            const bool armed = recordButton->getToggleState();
            for (auto tid : getMultiEditTargets(trackId_, isMaster_))
                TrackManager::getInstance().setTrackRecordArmed(tid, armed);
        };
        addAndMakeVisible(*recordButton);

        // Input-monitor: 3-state control (Off / In / Auto). Off = grey glyph, In =
        // green chip, Auto = blue chip. Left-click cycles, right-click opens a menu.
        // A change applies to the whole multi-track edit set.
        monitorButton = std::make_unique<magda::MonitorControl>();
        monitorButton->getTrackId = [this]() { return trackId_; };
        monitorButton->getTargets = [this]() { return getMultiEditTargets(trackId_, isMaster_); };
        addAndMakeVisible(*monitorButton);

        // Send viewport (scrollable container for send slots)
        sendContainer_ = std::make_unique<juce::Component>();
        sendViewport_ = std::make_unique<juce::Viewport>();
        sendViewport_->setViewedComponent(sendContainer_.get(), false);
        sendViewport_->setScrollBarsShown(false, false, true, false);  // hidden but scrollable
        addAndMakeVisible(*sendViewport_);

        // "+ Add Send" button — visible whenever the Sends pane is on, sits
        // below any existing send rows. Shares the destination picker with
        // the strip's right-click "Add Send" submenu.
        addSendButton_ = std::make_unique<AddSendButton>();
        addSendButton_->onClick = [this]() { showAddSendMenu(); };
        addSendButton_->setVisible(false);
        addAndMakeVisible(*addSendButton_);

        // Mini Oscilloscope / Spectrum — rendered in compact mode, pointer to
        // the live plugin is wired by refreshMiniAnalyzers(). The chevron reveals
        // their controls stacked beneath; the strip relayouts taller to fit.
        auto relayoutOnExpand = [this]() {
            if (auto* parent = findParentComponentOfClass<MixerView>())
                parent->relayoutAllStrips();
        };
        miniOscilloscopeUI_ = std::make_unique<daw::ui::OscilloscopeUI>();
        miniOscilloscopeUI_->setCompact(true);
        miniOscilloscopeUI_->setPersistGlobalDefaults(false);
        miniOscilloscopeUI_->setVisible(false);
        miniOscilloscopeUI_->onControlsExpandedChanged = relayoutOnExpand;
        addAndMakeVisible(*miniOscilloscopeUI_);

        miniSpectrumUI_ = std::make_unique<daw::ui::SpectrumAnalyzerUI>();
        miniSpectrumUI_->setCompact(true);
        miniSpectrumUI_->setTrackId(trackId_);  // masking overlay (shown when popped out)
        miniSpectrumUI_->setPersistGlobalDefaults(false);
        miniSpectrumUI_->setVisible(false);
        miniSpectrumUI_->onControlsExpandedChanged = relayoutOnExpand;
        addAndMakeVisible(*miniSpectrumUI_);

        // Resize handle (thin horizontal bar above the fader). Controls
        // faderTopInset — drag down shrinks the fader, drag up grows it. Sends
        // auto-size to their slot count and are not user-resizable.
        sendResizeHandle_ = std::make_unique<SendResizeHandle>();
        sendResizeHandle_->onResize = [this](int deltaY, const juce::ModifierKeys& mods) {
            auto& metrics = MixerMetrics::getInstance();
            // Max inset: clamp so the fader region keeps at least 120px.
            int fixedHeight = 38                        // colour bar + label
                              + metrics.controlSpacing  // spacing after label
                              + 2                       // gap before handle
                              + 6                       // resize handle
                              + 120                     // minimum fader region
                              + 24                      // pan + gaps
                              + metrics.buttonSize      // M/S row
                              +
                              (Config::getInstance().getMixerShowMonitor() ? metrics.buttonSize + 2
                                                                           : 0)         // R/Mon row
                              + (Config::getInstance().getMixerShowRouting() ? 40 : 0)  // routing
                              + metrics.channelPadding * 2;  // top+bottom padding
            // Sends viewport also eats vertical space when visible.
            int sendsHeight = 0;
            if (Config::getInstance().getMixerShowSends()) {
                for (size_t i = 0; i < sendSlots_.size(); ++i)
                    sendsHeight += 18 + 1;
            }
            int maxInset =
                juce::jmax(MixerMetrics::minFaderTopInset, getHeight() - fixedHeight - sendsHeight);
            const int limit = juce::jmin(maxInset, MixerMetrics::maxFaderTopInset);
            const bool sameValue = mods.isAltDown();
            if (faderHeightResizeTargets_.empty()) {
                if (sameValue) {
                    if (auto* parent = findParentComponentOfClass<MixerView>())
                        faderHeightResizeTargets_ = parent->getLayoutEditTargets(trackId_, true);
                    else
                        faderHeightResizeTargets_ = {trackId_};
                } else {
                    faderHeightResizeTargets_ = getMultiEditTargets(trackId_, isMaster_);
                }

                faderHeightResizeClickedStartInset_ = faderTopInset();
                faderHeightResizeStartValues_.clear();
                faderHeightResizeStartEffective_.clear();
                for (auto tid : faderHeightResizeTargets_) {
                    faderHeightResizeStartValues_[tid] = storedMixerFaderTopInset(tid);
                    faderHeightResizeStartEffective_[tid] = effectiveMixerFaderTopInset(tid);
                }
            }

            const int sameInset = juce::jlimit(MixerMetrics::minFaderTopInset, limit,
                                               faderHeightResizeClickedStartInset_ + deltaY);
            auto& tm = TrackManager::getInstance();
            for (auto tid : faderHeightResizeTargets_) {
                const int baseInset = sameValue ? faderHeightResizeClickedStartInset_
                                                : faderHeightResizeStartEffective_[tid];
                const int newInset =
                    juce::jlimit(MixerMetrics::minFaderTopInset, limit, baseInset + deltaY);
                tm.setTrackMixerFaderTopInset(tid, sameValue ? sameInset : newInset);
            }
            if (!faderHeightResizeTargets_.empty()) {
                if (onSendAreaResized)
                    onSendAreaResized();
            }
        };
        sendResizeHandle_->onResizeEnd = [this](const juce::ModifierKeys&) {
            std::vector<std::unique_ptr<UndoableCommand>> commands;
            for (auto tid : faderHeightResizeTargets_) {
                const auto oldIt = faderHeightResizeStartValues_.find(tid);
                if (oldIt == faderHeightResizeStartValues_.end())
                    continue;
                const int oldInset = oldIt->second;
                const int newInset = storedMixerFaderTopInset(tid);
                if (oldInset != newInset) {
                    commands.push_back(std::make_unique<SetTrackMixerFaderTopInsetCommand>(
                        tid, oldInset, newInset));
                }
            }

            faderHeightResizeTargets_.clear();
            faderHeightResizeStartValues_.clear();
            faderHeightResizeStartEffective_.clear();

            if (auto* parent = findParentComponentOfClass<MixerView>())
                parent->executeMixerLayoutCommands("Resize Mixer Fader Height",
                                                   std::move(commands));
        };
        sendResizeHandle_->onReset = [this](const juce::ModifierKeys& mods) {
            auto targets = mods.isAltDown() ? std::vector<TrackId>{}
                                            : getMultiEditTargets(trackId_, isMaster_);
            if (mods.isAltDown()) {
                if (auto* parent = findParentComponentOfClass<MixerView>())
                    targets = parent->getLayoutEditTargets(trackId_, true);
                else
                    targets = {trackId_};
            }
            std::vector<std::unique_ptr<UndoableCommand>> commands;
            for (auto tid : targets) {
                const int oldInset = storedMixerFaderTopInset(tid);
                if (oldInset != 0) {
                    commands.push_back(
                        std::make_unique<SetTrackMixerFaderTopInsetCommand>(tid, oldInset, 0));
                }
            }
            if (auto* parent = findParentComponentOfClass<MixerView>())
                parent->executeMixerLayoutCommands("Reset Mixer Fader Height", std::move(commands));
            if (onSendAreaResized)
                onSendAreaResized();
        };
        addAndMakeVisible(*sendResizeHandle_);

        // Audio/MIDI routing selectors (toggle + dropdown, not on master)
        audioInSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::AudioIn);
        addAndMakeVisible(*audioInSelector);

        audioOutSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::AudioOut);
        addAndMakeVisible(*audioOutSelector);

        midiInSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::MidiIn);
        addAndMakeVisible(*midiInSelector);

        midiOutSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::MidiOut);
        addAndMakeVisible(*midiOutSelector);

        // Populate routing options from real data and wire callbacks
        if (audioEngine_) {
            auto* deviceManager = audioEngine_->getDeviceManager();
            auto* device = deviceManager ? deviceManager->getCurrentAudioDevice() : nullptr;
            auto* midiBridge = audioEngine_->getMidiBridge();

            juce::BigInteger enabledInputChannels, enabledOutputChannels;
            std::map<int, juce::String> teInputDeviceNames;
            if (auto* bridge = audioEngine_->getAudioBridge()) {
                enabledInputChannels = bridge->getEnabledInputChannels();
                enabledOutputChannels = bridge->getEnabledOutputChannels();
                teInputDeviceNames = bridge->getInputDeviceNamesByChannel();
            }

            RoutingSyncHelper::populateAudioInputOptions(audioInSelector.get(), device, trackId_,
                                                         &inputTrackMapping_, enabledInputChannels,
                                                         nullptr, teInputDeviceNames);
            RoutingSyncHelper::populateAudioOutputOptions(audioOutSelector.get(), trackId_, device,
                                                          outputTrackMapping_,
                                                          enabledOutputChannels);
            RoutingSyncHelper::populateMidiInputOptions(midiInSelector.get(), midiBridge);
            RoutingSyncHelper::populateMidiOutputOptions(midiOutSelector.get(), midiBridge,
                                                         midiOutputTrackMapping_);
        }

        setupRoutingCallbacks();
    }

    // Listen recursively to all child clicks so Cmd/Shift-click anywhere on
    // the strip — including on the fader, mute button, etc. — reaches the
    // strip's mouseDown and can be routed to multi-selection. Without this,
    // most of the strip's surface is covered by interactive children that
    // would swallow the modifier click. The child's own mouseDown still
    // fires (so cmd-clicking mute still toggles mute); accepted trade-off.
    addMouseListener(this, true);
}

void MixerView::ChannelStrip::setupRoutingCallbacks() {
    if (!audioInSelector || !audioOutSelector || !midiInSelector || !midiOutSelector)
        return;

    auto* midiBridge = audioEngine_ ? audioEngine_->getMidiBridge() : nullptr;

    // Audio input selector callbacks (mutually exclusive with MIDI input)
    audioInSelector->onEnabledChanged = [this](bool enabled) {
        if (enabled) {
            midiInSelector->setEnabled(false);
            TrackManager::getInstance().setTrackMidiInput(trackId_, "");
            auto* trackInfo = TrackManager::getInstance().getTrack(trackId_);
            if (trackInfo && trackInfo->audioInputDevice.startsWith("track:"))
                TrackManager::getInstance().setTrackAudioInput(trackId_,
                                                               trackInfo->audioInputDevice);
            else
                TrackManager::getInstance().setTrackAudioInput(trackId_, "default");
        } else {
            TrackManager::getInstance().setTrackAudioInput(trackId_, "");
        }
    };

    audioInSelector->onSelectionChanged = [this](int selectedId) {
        if (selectedId == 1) {
            TrackManager::getInstance().setTrackAudioInput(trackId_, "");
        } else if (selectedId >= 200) {
            auto it = inputTrackMapping_.find(selectedId);
            if (it != inputTrackMapping_.end()) {
                TrackManager::getInstance().setTrackAudioInput(trackId_,
                                                               "track:" + juce::String(it->second));
            }
        } else if (selectedId >= 10) {
            TrackManager::getInstance().setTrackAudioInput(trackId_, "default");
        }
    };

    // MIDI input selector callbacks (mutually exclusive with audio input)
    midiInSelector->onEnabledChanged = [this, midiBridge](bool enabled) {
        if (enabled) {
            audioInSelector->setEnabled(false);
            TrackManager::getInstance().setTrackAudioInput(trackId_, "");
            int selectedId = midiInSelector->getSelectedId();
            if (selectedId == 1) {
                TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
            } else if (selectedId >= 10 && midiBridge) {
                auto midiInputs = midiBridge->getAvailableMidiInputs();
                int deviceIndex = selectedId - 10;
                if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size())) {
                    TrackManager::getInstance().setTrackMidiInput(trackId_,
                                                                  midiInputs[deviceIndex].id);
                } else {
                    TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
                }
            } else {
                TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
            }
        } else {
            TrackManager::getInstance().setTrackMidiInput(trackId_, "");
        }
    };

    midiInSelector->onSelectionChanged = [this, midiBridge](int selectedId) {
        if (selectedId == 2) {
            TrackManager::getInstance().setTrackMidiInput(trackId_, "");
        } else if (selectedId == 1) {
            TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
        } else if (selectedId >= 10 && midiBridge) {
            auto midiInputs = midiBridge->getAvailableMidiInputs();
            int deviceIndex = selectedId - 10;
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size())) {
                TrackManager::getInstance().setTrackMidiInput(trackId_, midiInputs[deviceIndex].id);
            }
        }
    };

    // Output selector callbacks
    audioOutSelector->onEnabledChanged = [this](bool enabled) {
        if (enabled) {
            TrackManager::getInstance().setTrackAudioOutput(trackId_, "master");
        } else {
            TrackManager::getInstance().setTrackAudioOutput(trackId_, "");
        }
    };

    audioOutSelector->onSelectionChanged = [this](int selectedId) {
        if (selectedId == 1) {
            TrackManager::getInstance().setTrackAudioOutput(trackId_, "master");
        } else if (selectedId == 2) {
            TrackManager::getInstance().setTrackAudioOutput(trackId_, "");
        } else if (selectedId >= 200) {
            auto it = outputTrackMapping_.find(selectedId);
            if (it != outputTrackMapping_.end()) {
                TrackManager::getInstance().setTrackAudioOutput(
                    trackId_, "track:" + juce::String(it->second));
            }
        } else if (selectedId >= 10) {
            TrackManager::getInstance().setTrackAudioOutput(trackId_, "master");
        }
    };

    // MIDI output selector callbacks
    midiOutSelector->onEnabledChanged = [this](bool enabled) {
        if (!enabled) {
            TrackManager::getInstance().setTrackMidiOutput(trackId_, "");
        }
    };

    midiOutSelector->onSelectionChanged = [this, midiBridge](int selectedId) {
        if (selectedId == 1) {
            TrackManager::getInstance().setTrackMidiOutput(trackId_, "");
        } else if (selectedId >= 200) {
            auto it = midiOutputTrackMapping_.find(selectedId);
            if (it != midiOutputTrackMapping_.end()) {
                TrackManager::getInstance().setTrackMidiOutput(trackId_,
                                                               "track:" + juce::String(it->second));
            }
        } else if (selectedId >= 10 && midiBridge) {
            auto midiOutputs = midiBridge->getAvailableMidiOutputs();
            int deviceIndex = selectedId - 10;
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiOutputs.size())) {
                TrackManager::getInstance().setTrackMidiOutput(trackId_,
                                                               midiOutputs[deviceIndex].id);
            }
        }
    };
}

std::vector<MixerView::ChannelStrip::MiniChainRowSignatureEntry>
MixerView::ChannelStrip::buildMiniChainSignature(const TrackInfo& track) const {
    std::vector<MiniChainRowSignatureEntry> signature;
    signature.reserve(track.chain.fxChainElements.size());
    for (const auto& element : track.chain.fxChainElements) {
        if (isDevice(element)) {
            const auto& device = getDevice(element);
            signature.push_back({false, device.id, device.name, device.miniMixerParameters});
        } else if (isRack(element)) {
            const auto& rack = getRack(element);
            signature.push_back({true, rack.id, rack.name, {}});
        }
    }
    return signature;
}

void MixerView::ChannelStrip::syncMiniChainRows(const TrackInfo& track) {
    auto signature = buildMiniChainSignature(track);
    if (signature == miniChainSignature_)
        return;

    rebuildMiniChainRows(track, std::move(signature));
}

void MixerView::ChannelStrip::rebuildMiniChainRows(
    const TrackInfo& track, std::vector<MiniChainRowSignatureEntry> signature) {
    miniChainRows_.clear();
    miniChainSignature_ = std::move(signature);
    for (const auto& element : track.chain.fxChainElements) {
        if (isDevice(element)) {
            const auto& device = getDevice(element);
            auto row = std::make_unique<MiniChainRow>();
            row->setDevice(ChainNodePath::topLevelDevice(trackId_, device.id), audioEngine_,
                           device.name, device.bypassed);
            row->onExpandChanged = [this]() {
                if (auto* parent = findParentComponentOfClass<MixerView>())
                    parent->relayoutAllStrips();
            };
            addAndMakeVisible(*row);
            miniChainRows_.push_back(std::move(row));
        } else if (isRack(element)) {
            const auto& rack = getRack(element);
            auto row = std::make_unique<MiniChainRow>();
            row->setDevice({}, audioEngine_, rack.name, false);
            addAndMakeVisible(*row);
            miniChainRows_.push_back(std::move(row));
        }
    }
}

void MixerView::ChannelStrip::syncMiniChainRowState(DeviceId deviceId, bool bypassed) {
    if (deviceId == INVALID_DEVICE_ID)
        return;
    for (auto& row : miniChainRows_) {
        if (row->deviceId() == deviceId) {
            row->setBypassedState(bypassed);
            return;
        }
    }
}

void MixerView::ChannelStrip::syncMiniChainPluginWindow(DeviceId deviceId, bool isOpen) {
    if (deviceId == INVALID_DEVICE_ID)
        return;
    for (auto& row : miniChainRows_) {
        if (row->deviceId() == deviceId) {
            row->setPluginEditorOpen(isOpen);
            return;
        }
    }
}

void MixerView::ChannelStrip::refreshMiniAnalyzers() {
    auto& tm = TrackManager::getInstance();
    auto* bridge = audioEngine_ ? audioEngine_->getAudioBridge() : nullptr;

    if (miniOscilloscopeUI_) {
        daw::audio::OscilloscopePlugin* osc = nullptr;
        DeviceId id = INVALID_DEVICE_ID;
        if (bridge) {
            id = tm.findMixerAnalysisDevice(trackId_, "oscilloscope");
            if (id != INVALID_DEVICE_ID) {
                auto pluginPtr =
                    bridge->getPlugin(ChainNodePath::mixerAnalysisDevice(trackId_, id));
                osc = dynamic_cast<daw::audio::OscilloscopePlugin*>(pluginPtr.get());
            }
        }
        miniOscilloscopeUI_->setPlugin(osc);
    }

    if (miniSpectrumUI_) {
        daw::audio::SpectrumAnalyzerPlugin* spec = nullptr;
        DeviceId id = INVALID_DEVICE_ID;
        if (bridge) {
            id = tm.findMixerAnalysisDevice(trackId_, "spectrumanalyzer");
            if (id != INVALID_DEVICE_ID) {
                auto pluginPtr =
                    bridge->getPlugin(ChainNodePath::mixerAnalysisDevice(trackId_, id));
                spec = dynamic_cast<daw::audio::SpectrumAnalyzerPlugin*>(pluginPtr.get());
            }
        }
        miniSpectrumUI_->setPlugin(spec);
    }
}

void MixerView::ChannelStrip::showAddSendMenu() {
    if (isMaster_)
        return;
    juce::PopupMenu menu;
    const auto& tracks = TrackManager::getInstance().getTracks();
    std::set<TrackId> existingSendDests;
    if (auto* thisTrack = TrackManager::getInstance().getTrack(trackId_)) {
        for (const auto& send : thisTrack->sends)
            existingSendDests.insert(send.destTrackId);
    }
    for (const auto& t : tracks) {
        if (t.id != trackId_ && t.type != TrackType::Master &&
            existingSendDests.find(t.id) == existingSendDests.end()) {
            menu.addItem(t.id, t.name);
        }
    }
    if (menu.getNumItems() == 0)
        menu.addItem(-1, "(No tracks available)", false);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(addSendButton_.get()), [this](int result) {
            if (result > 0)
                UndoManager::getInstance().executeCommand(
                    std::make_unique<AddSendCommand>(trackId_, static_cast<TrackId>(result)));
        });
}

void MixerView::ChannelStrip::rebuildSendSlots(const std::vector<SendInfo>& sends) {
    // Remove old slots from send container
    for (auto& slot : sendSlots_) {
        if (sendContainer_) {
            sendContainer_->removeChildComponent(slot->nameLabel.get());
            sendContainer_->removeChildComponent(slot->levelSlider.get());
            sendContainer_->removeChildComponent(slot->removeButton.get());
        }
    }
    sendSlots_.clear();

    for (const auto& send : sends) {
        auto slot = std::make_unique<SendSlot>();
        slot->busIndex = send.busIndex;

        // Destination name label
        slot->nameLabel = std::make_unique<juce::Label>();
        juce::String destName = "Bus " + juce::String(send.busIndex);
        if (send.destTrackId != INVALID_TRACK_ID) {
            if (auto* destTrack = TrackManager::getInstance().getTrack(send.destTrackId))
                destName = destTrack->name;
        }
        slot->nameLabel->setText(destName, juce::dontSendNotification);
        slot->nameLabel->setFont(FontManager::getInstance().getUIFont(9.0f));
        slot->nameLabel->setColour(juce::Label::textColourId,
                                   DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        slot->nameLabel->setJustificationType(juce::Justification::centredLeft);
        sendContainer_->addAndMakeVisible(*slot->nameLabel);

        // Level slider (horizontal, 0-1)
        slot->levelSlider =
            std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decimal);
        slot->levelSlider->setOrientation(daw::ui::TextSlider::Orientation::Horizontal);
        slot->levelSlider->setRange(0.0, 1.0, 0.01);
        slot->levelSlider->setValue(send.level, juce::dontSendNotification);
        slot->levelSlider->setFont(FontManager::getInstance().getUIFont(9.0f));
        int busIdx = send.busIndex;
        slot->levelSlider->onValueChanged = [this, busIdx](double val) {
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetSendLevelCommand>(trackId_, busIdx, static_cast<float>(val)));
        };
        sendContainer_->addAndMakeVisible(*slot->levelSlider);

        // Remove button
        slot->removeButton = std::make_unique<juce::TextButton>("x");
        slot->removeButton->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        slot->removeButton->setColour(juce::TextButton::buttonColourId,
                                      DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        slot->removeButton->setColour(juce::TextButton::textColourOffId,
                                      DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        slot->removeButton->onClick = [this, busIdx]() {
            UndoManager::getInstance().executeCommand(
                std::make_unique<RemoveSendCommand>(trackId_, busIdx));
        };
        sendContainer_->addAndMakeVisible(*slot->removeButton);

        sendSlots_.push_back(std::move(slot));
    }

    resized();
}

void MixerView::ChannelStrip::paint(juce::Graphics& g) {
    auto fullBounds = getLocalBounds();
    bool hasGroupChildren = !groupChildren_.empty();

    // The group's own controls column (leftmost channelWidth when group has children)
    auto ownBounds = hasGroupChildren ? fullBounds.withWidth(preferredChannelWidth()) : fullBounds;

    // Background
    if (selected) {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    }
    g.fillRect(ownBounds);

    // Separator on right side of own column
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
    g.fillRect(ownBounds.getRight() - 1, 0, 1, ownBounds.getHeight());

    // Channel color indicator at top — skip for group parents with children (group header provides
    // colouring)
    if (!hasGroupChildren) {
        const int stripHeight = 4;
        const int labelRowBottom = stripHeight + 26;
        if (selected) {
            // Selected: black strip + black label background
            g.setColour(juce::Colours::black);
            g.fillRect(0, 0, ownBounds.getWidth() - 1, labelRowBottom);
        } else {
            // Thin colour bar only — label sits on the regular panel background
            g.setColour(trackColour_);
            g.fillRect(0, 0, ownBounds.getWidth() - 1, stripHeight);
        }
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        g.fillRect(0, labelRowBottom, ownBounds.getWidth() - 1, 1);
    }

    // Divider at the bottom of the sends region
    if (sendsRegionBottomY_ >= 0) {
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        g.fillRect(0, sendsRegionBottomY_, ownBounds.getWidth() - 1, 1);
    }

    // dB ticks and labels are drawn by the DbScale component

    // Group envelope: header banner + border around the entire group area
    if (hasGroupChildren) {
        const int groupHeaderHeight = 4 + 4 + 24 + MixerMetrics::getInstance().controlSpacing;

        if (selected) {
            // Selected: black header like regular channels
            g.setColour(juce::Colours::black);
            g.fillRect(0, 0, fullBounds.getWidth(), groupHeaderHeight);
        } else {
            // Plain panel background, with just a thin colour bar on top (like a
            // regular channel header) — not the full-width colour flood.
            g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
            g.fillRect(0, 0, fullBounds.getWidth(), groupHeaderHeight);

            g.setColour(trackColour_);
            g.fillRect(2, 2, fullBounds.getWidth() - 4, 4);
        }

        // Horizontal separator below header (neutral, not the track colour)
        g.setColour(DarkTheme::getColour(selected ? DarkTheme::BORDER : DarkTheme::SEPARATOR));
        g.fillRect(0, groupHeaderHeight, fullBounds.getWidth(), 1);
    }
}

void MixerView::ChannelStrip::paintOverChildren(juce::Graphics& g) {
    // The group is indicated by its coloured header banner only; the full-height
    // coloured envelope border around the children is intentionally omitted so
    // the colour stays on the top strip rather than the whole group.

    // Skip overlay for child tracks nested inside a group envelope
    if (!isChildTrack_)
        return;

    // Check if this strip is a child component of a group strip
    if (auto* parentStrip = dynamic_cast<ChannelStrip*>(getParentComponent())) {
        if (!parentStrip->groupChildren_.empty())
            return;  // Nested inside group — no overlay needed
    }

    // Semi-transparent overlay to dim child tracks (fallback for non-group children)
    g.setColour(juce::Colour(0x30000000));
    g.fillRect(getLocalBounds());

    // Left-edge bracket bar showing group membership
    g.setColour(trackColour_.withAlpha(0.8f));
    g.fillRect(0, 0, 3, getHeight());
}

void MixerView::ChannelStrip::resized() {
    const auto& metrics = MixerMetrics::getInstance();
    bool hasGroupChildren = !groupChildren_.empty();

    // Group envelope header height: colour bar + padding + label + spacing
    const int groupHeaderHeight = 4 + 4 + 24 + metrics.controlSpacing;

    // If this is a group strip with children, lay out the shared header banner
    // across the full width, then position children below it
    if (hasGroupChildren) {
        int channelWidth = preferredChannelWidth();
        const int borderWidth = 2;
        int childTop = groupHeaderHeight + 1;                    // below separator line
        int childHeight = getHeight() - childTop - borderWidth;  // above bottom border

        int childX = channelWidth;
        for (size_t i = 0; i < groupChildren_.size(); ++i) {
            bool isLast = (i == groupChildren_.size() - 1);
            int childWidth = channelWidth;
            if (auto* childStrip = dynamic_cast<ChannelStrip*>(groupChildren_[i]))
                childWidth = childStrip->preferredChannelWidth();
            int w = isLast ? childWidth - borderWidth : childWidth;
            groupChildren_[i]->setBounds(childX, childTop, w, childHeight);
            childX += childWidth;
        }
    }

    // For a group with children: own controls in the leftmost column, below the shared header
    // For everything else: full width, full height
    int ownWidth = hasGroupChildren ? preferredChannelWidth() : getWidth();
    int ownTop = hasGroupChildren ? groupHeaderHeight : 0;
    int ownHeight = getHeight() - ownTop;

    auto bounds =
        juce::Rectangle<int>(0, ownTop, ownWidth, ownHeight).reduced(metrics.channelPadding);

    if (hasGroupChildren) {
        // Group: label in the header, left-aligned next to expand toggle
        auto headerBounds =
            juce::Rectangle<int>(0, 0, ownWidth, groupHeaderHeight).reduced(metrics.channelPadding);
        headerBounds.removeFromTop(6);  // colour bar space
        auto titleRow = headerBounds.removeFromTop(24);
        if (expandToggle_) {
            expandToggle_->setBounds(titleRow.removeFromLeft(20).withSizeKeepingCentre(18, 18));
            titleRow.removeFromLeft(2);
            expandToggle_->toFront(false);
        }
        trackLabel->setJustificationType(juce::Justification::centredLeft);
        trackLabel->setBounds(titleRow);
        trackLabel->toFront(false);
    } else {
        // Non-group: colour bar space + label at top of own bounds
        bounds.removeFromTop(6);
        auto titleRow = bounds.removeFromTop(24);
        if (expandToggle_) {
            expandToggle_->setBounds(titleRow.removeFromLeft(20).withSizeKeepingCentre(18, 18));
            titleRow.removeFromLeft(2);
        }
        trackLabel->setBounds(titleRow);
    }
    bounds.removeFromTop(metrics.controlSpacing);

    bool isMultiOut = trackType_ == TrackType::MultiOut;

    const auto& cfg = Config::getInstance();

    // Mini analyzers (Oscilloscope / Spectrum) sit below the header.
    // Each takes a fixed compact height when its rail toggle is on.
    constexpr int miniAnalyzerHeight = 64;
    // The chord track emits no audio yet, so it never shows the osc / spectrum.
    // When they're globally enabled it still reserves the same empty space at
    // the top so its fader stays aligned with the audio channels.
    const bool isChord = trackType_ == TrackType::Chord;
    const bool oscEnabled = cfg.getMixerShowOscilloscope();
    const bool specEnabled = cfg.getMixerShowSpectrum();
    if (oscEnabled && miniOscilloscopeUI_) {
        if (isChord) {
            bounds.removeFromTop(miniAnalyzerHeight + 2);  // empty spacer
            miniOscilloscopeUI_->setVisible(false);
        } else {
            const int h = miniAnalyzerHeight + miniOscilloscopeUI_->compactExtraHeight();
            miniOscilloscopeUI_->setBounds(bounds.removeFromTop(h));
            miniOscilloscopeUI_->setVisible(true);
            bounds.removeFromTop(2);
        }
    } else if (miniOscilloscopeUI_) {
        miniOscilloscopeUI_->setVisible(false);
    }
    if (specEnabled && miniSpectrumUI_) {
        if (isChord) {
            bounds.removeFromTop(miniAnalyzerHeight + 2);  // empty spacer
            miniSpectrumUI_->setVisible(false);
        } else {
            const int h = miniAnalyzerHeight + miniSpectrumUI_->compactExtraHeight();
            miniSpectrumUI_->setBounds(bounds.removeFromTop(h));
            miniSpectrumUI_->setVisible(true);
            bounds.removeFromTop(2);
        }
    } else if (miniSpectrumUI_) {
        miniSpectrumUI_->setVisible(false);
    }

    // Sends auto-size to their slot count (no user resize). The horizontal
    // handle sits just above the fader and controls faderTopInset — that is
    // the only thing the user can drag.
    if (!isMaster_ && sendViewport_) {
        const bool sendsVisible = Config::getInstance().getMixerShowSends();
        const int sendSlotHeight = 18;
        int containerWidth = bounds.getWidth();
        int totalContentHeight = 0;
        for (auto& slot : sendSlots_) {
            auto row = juce::Rectangle<int>(0, totalContentHeight, containerWidth, sendSlotHeight);
            slot->nameLabel->setBounds(row.removeFromLeft(row.getWidth() * 40 / 100));
            auto removeArea = row.removeFromRight(16);
            slot->removeButton->setBounds(removeArea);
            slot->levelSlider->setBounds(row);
            totalContentHeight += sendSlotHeight + 1;  // 1px gap
        }

        sendContainer_->setBounds(0, 0, containerWidth, totalContentHeight);

        bounds.removeFromTop(2);  // Gap between track header and sends/handle

        if (sendsVisible) {
            // Reserve uniform height across all strips so faders line up.
            // Height = Add Send row + max-sends-on-any-track slot rows.
            size_t maxSends = 0;
            for (const auto& t : TrackManager::getInstance().getTracks())
                maxSends = std::max(maxSends, t.sends.size());
            int uniformSendsRegion = sendSlotHeight;  // Add Send row itself
            if (maxSends > 0)
                uniformSendsRegion += 1 + static_cast<int>(maxSends) * (sendSlotHeight + 1) - 1;
            auto sendsRegion = bounds.removeFromTop(uniformSendsRegion);
            sendsRegionBottomY_ = sendsRegion.getBottom();

            if (addSendButton_) {
                addSendButton_->setBounds(sendsRegion.removeFromTop(sendSlotHeight));
                addSendButton_->setVisible(true);
            }
            if (totalContentHeight > 0) {
                sendsRegion.removeFromTop(1);  // 1px gap matching inter-slot spacing
                sendViewport_->setBounds(sendsRegion.removeFromTop(totalContentHeight));
                sendViewport_->setVisible(true);
            } else {
                sendViewport_->setVisible(false);
            }
        } else {
            sendViewport_->setVisible(false);
            if (addSendButton_)
                addSendButton_->setVisible(false);
            sendsRegionBottomY_ = -1;
        }

        // Breathing room between the sends area and the resize handle.
        if (sendsVisible)
            bounds.removeFromTop(6);

        // Mini FX chain: shown between sends and the fader's top inset.
        // Faders line up across strips by reserving the max total chain
        // block height (sum of preferred heights, accounting for expanded
        // rows) of any visible strip.
        if (cfg.getMixerShowFxChain()) {
            auto chainBlockHeight = [](const ChannelStrip& s) {
                int h = 0;
                for (const auto& r : s.miniChainRows_)
                    h += r->preferredHeight() + 1;
                return h;
            };
            int maxChainHeight = 0;
            if (auto* parent = findParentComponentOfClass<MixerView>()) {
                for (const auto& s : parent->channelStrips)
                    maxChainHeight = std::max(maxChainHeight, chainBlockHeight(*s));
            }
            int myHeight = 0;
            for (auto& row : miniChainRows_) {
                const int h = row->preferredHeight();
                row->setBounds(bounds.removeFromTop(h));
                row->setVisible(true);
                bounds.removeFromTop(1);
                myHeight += h + 1;
            }
            const int pad = maxChainHeight - myHeight;
            if (pad > 0)
                bounds.removeFromTop(pad);
            if (maxChainHeight > 0)
                bounds.removeFromTop(4);
        } else {
            for (auto& row : miniChainRows_)
                row->setVisible(false);
        }

        bounds.removeFromTop(faderTopInset());

        if (sendResizeHandle_) {
            sendResizeHandle_->setVisible(true);
            sendResizeHandle_->setBounds(bounds.removeFromTop(6));
            sendResizeHandle_->setAlwaysOnTop(true);
        }
    }

    // Bottom section (removeFromBottom, so bottommost first):
    // Omidi | Oaudio
    // Imidi | Iaudio
    // R     | M
    // M     | S

    // Routing selectors (bottommost)
    if (audioInSelector && audioOutSelector && midiInSelector && midiOutSelector) {
        if (isChord) {
            // Chord track: MIDI in/out only (no audio routing).
            audioInSelector->setVisible(false);
            audioOutSelector->setVisible(false);
            const bool showMidi = Config::getInstance().getMixerShowRouting() && !isMultiOut;
            midiInSelector->setVisible(showMidi);
            midiOutSelector->setVisible(showMidi);
            if (showMidi) {
                bounds.removeFromBottom(2);
                midiOutSelector->setBounds(bounds.removeFromBottom(16));
                bounds.removeFromBottom(2);
                midiInSelector->setBounds(bounds.removeFromBottom(16));
            }
        } else if (Config::getInstance().getMixerShowRouting()) {
            bool showInputs = !isMultiOut;
            bool showMidi = !isMultiOut;

            audioOutSelector->setVisible(true);
            audioInSelector->setVisible(showInputs);
            midiInSelector->setVisible(showMidi);
            midiOutSelector->setVisible(showMidi);

            bounds.removeFromBottom(2);

            // Output row: Omidi | Oaudio
            auto outRow = bounds.removeFromBottom(16);
            if (showMidi) {
                int halfWidth = (outRow.getWidth() - 2) / 2;
                midiOutSelector->setBounds(outRow.removeFromLeft(halfWidth));
                outRow.removeFromLeft(2);
                audioOutSelector->setBounds(outRow);
            } else {
                audioOutSelector->setBounds(outRow);
            }

            bounds.removeFromBottom(2);

            // Input row: Imidi | Iaudio
            auto inRow = bounds.removeFromBottom(16);
            if (showInputs && showMidi) {
                int halfWidth = (inRow.getWidth() - 2) / 2;
                midiInSelector->setBounds(inRow.removeFromLeft(halfWidth));
                inRow.removeFromLeft(2);
                audioInSelector->setBounds(inRow);
            } else if (showInputs) {
                audioInSelector->setBounds(inRow);
            } else {
                // Multi-out: no inputs
            }
        } else {
            audioInSelector->setVisible(false);
            audioOutSelector->setVisible(false);
            midiInSelector->setVisible(false);
            midiOutSelector->setVisible(false);
        }
    }

    // M/S/R/M buttons — two rows of two above routing
    {
        bounds.removeFromBottom(2);

        if (chordSpeakerButton)
            chordSpeakerButton->setVisible(isChord);

        if (isChord) {
            // Chord track: a single 3-state audition control folds in mute / solo
            // / monitor. No mute "M" / record / standalone solo or monitor button.
            muteButton->setVisible(false);
            if (recordButton)
                recordButton->setVisible(false);
            if (monitorButton)
                monitorButton->setVisible(false);
            soloButton->setVisible(false);

            auto row = bounds.removeFromBottom(metrics.buttonSize);
            chordSpeakerButton->setBounds(row);
        } else if (isMaster_) {
            muteButton->setVisible(false);
            soloButton->setVisible(false);
            if (recordButton)
                recordButton->setVisible(false);
            if (monitorButton)
                monitorButton->setVisible(false);
        } else if (isMultiOut || !recordButton) {
            // M/S only — single row
            auto row = bounds.removeFromBottom(metrics.buttonSize);
            int halfWidth = (row.getWidth() - 2) / 2;
            muteButton->setBounds(row.removeFromLeft(halfWidth));
            row.removeFromLeft(2);
            soloButton->setBounds(row);
            soloButton->setVisible(true);
            if (recordButton)
                recordButton->setVisible(false);
            if (monitorButton)
                monitorButton->setVisible(false);
        } else {
            if (Config::getInstance().getMixerShowMonitor()) {
                // Bottom row: R | M(onitor)
                auto row2 = bounds.removeFromBottom(metrics.buttonSize);
                int halfWidth = (row2.getWidth() - 2) / 2;
                recordButton->setBounds(row2.removeFromLeft(halfWidth));
                recordButton->setVisible(true);
                row2.removeFromLeft(2);
                if (monitorButton) {
                    monitorButton->setBounds(row2);
                    monitorButton->setVisible(true);
                }

                bounds.removeFromBottom(2);

                // Top row: M | S
                auto row1 = bounds.removeFromBottom(metrics.buttonSize);
                halfWidth = (row1.getWidth() - 2) / 2;
                muteButton->setBounds(row1.removeFromLeft(halfWidth));
                row1.removeFromLeft(2);
                soloButton->setBounds(row1);
                soloButton->setVisible(true);
            } else {
                // Single row: M | S (R and monitor hidden)
                auto row = bounds.removeFromBottom(metrics.buttonSize);
                int halfWidth = (row.getWidth() - 2) / 2;
                muteButton->setBounds(row.removeFromLeft(halfWidth));
                row.removeFromLeft(2);
                soloButton->setBounds(row);
                soloButton->setVisible(true);
                if (recordButton)
                    recordButton->setVisible(false);
                if (monitorButton)
                    monitorButton->setVisible(false);
            }
        }
    }

    // Pan slider — above M/S/R/M (hidden for master and the chord track)
    if (isMaster_ || isChord) {
        panSlider->setVisible(false);
    } else {
        panSlider->setVisible(true);
        bounds.removeFromBottom(2);
        panSlider->setBounds(bounds.removeFromBottom(20));
        bounds.removeFromBottom(2);
    }

    // Readout sits just below the fader. A small breathing strip is left
    // beneath it so the number doesn't kiss the pan slider above.
    const int labelHeight = 12;
    const int bottomStrip = 4;
    bounds.removeFromBottom(bottomStrip);
    peakLabel->setBounds(bounds.removeFromBottom(labelHeight));
    peakLabel->setVisible(!isChord);  // no peak readout without a meter

    // Reserve room above the meter so the top dB label (+6) isn't clipped by
    // the always-on-top resize handle: the scale draws half a line above the
    // meter top, and that overhang must land in clear space, not under chrome.
    bounds.removeFromTop(static_cast<int>(metrics.labelTextHeight / 2.0f + 2.0f));

    // New layout: meter fills the column on the left, slider overlays it, dB
    // scale (tick + label) sits on the right. The slider has no body of its
    // own — only the thumb is drawn (see TextSlider's vertical paint).
    faderRegion_ = bounds;
    auto layoutArea = bounds;

    const int scaleWidth = 28;  // wider column: narrower fader, bigger numbers
    const int scaleGap = metrics.tickToMeterGap;

    auto scaleColumn = layoutArea.removeFromRight(scaleWidth);
    layoutArea.removeFromRight(scaleGap);

    meterArea_ = layoutArea;
    faderArea_ = layoutArea;
    levelMeter->setBounds(meterArea_);
    // Chord track emits no audio yet: just the fader, no meter underlaid.
    levelMeter->setVisible(!isChord);
    volumeSlider->setBounds(faderArea_);
    volumeSlider->toFront(false);  // thumb sits on top of the meter

    if (dbScale_) {
        const int labelPad = static_cast<int>(metrics.labelTextHeight / 2.0f + 1.0f);
        dbScale_->setBounds(scaleColumn.getX(), meterArea_.getY() - labelPad,
                            scaleColumn.getWidth(), meterArea_.getHeight() + labelPad * 2);
    }
}

void MixerView::ChannelStrip::setMeterLevel(float level) {
    setMeterLevels(level, level);
}

void MixerView::ChannelStrip::setMeterLevels(float leftLevel, float rightLevel) {
    meterLevel = std::max(leftLevel, rightLevel);
    if (levelMeter) {
        levelMeter->setLevels(leftLevel, rightLevel);
    }

    // Update peak value
    float maxLevel = std::max(leftLevel, rightLevel);
    if (maxLevel > peakValue_) {
        peakValue_ = maxLevel;
        if (peakLabel) {
            float db = gainToDb(peakValue_);
            if (std::abs(db) < 0.05f)
                db = 0.0f;
            juce::String peakText;
            if (db <= MIN_DB) {
                peakText = "-inf";
            } else {
                peakText = juce::String(db, 1);
            }
            peakLabel->setText(peakText, juce::dontSendNotification);
        }
    }
}

void MixerView::ChannelStrip::resetPeak() {
    peakValue_ = 0.0f;
    if (peakLabel)
        peakLabel->setText("-inf", juce::dontSendNotification);
}

void MixerView::ChannelStrip::setSelected(bool shouldBeSelected) {
    if (selected != shouldBeSelected) {
        selected = shouldBeSelected;
        trackLabel->setColour(juce::Label::textColourId,
                              selected ? juce::Colours::white
                                       : DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        repaint();
    }
}

void MixerView::ChannelStrip::mouseDown(const juce::MouseEvent& event) {
    // addMouseListener(this, true) makes clicks that land directly on the strip
    // arrive twice: once via the normal virtual and once via the self-listener.
    // Toggle selection is self-inverting, so the duplicate delivery used to undo
    // it instantly (Cmd+click multi-select never stuck). Drop the second
    // delivery of the same physical event (identical event time).
    if (event.eventTime == lastMouseDownEventTime_)
        return;
    lastMouseDownEventTime_ = event.eventTime;

    auto& selection = SelectionManager::getInstance();
    const bool fromChild = event.originalComponent != this;
    // A group parent strip listens recursively to its nested child strips, so a
    // click inside a child also fires the parent's handler. Only the strip that
    // actually owns the clicked component should act — otherwise shift/cmd
    // selection on a nested strip (e.g. a grouped multi-out channel) resolves to
    // the parent's track id and the child never gets selected.
    for (juce::Component* c = event.originalComponent; c != nullptr && c != this;
         c = c->getParentComponent()) {
        if (dynamic_cast<ChannelStrip*>(c) != nullptr) {
            return;  // a nested child strip handles its own click
        }
    }

    // Clicks on children are forwarded to us via addMouseListener so that
    // Cmd/Shift-click anywhere on the strip can drive multi-selection. A
    // plain click on a child must NOT also single-select the track (otherwise
    // clicking mute on track B would steal selection from track A).

    if (event.mods.isPopupMenu()) {
        if (fromChild) {
            return;  // children manage their own right-click behaviour
        }
        if (isMaster_) {
            return;  // master strip has nothing to offer here
        }

        const int ungroupTracksId = -103;
        const int groupSelectedTracksId = -102;
        const int deleteTrackId = -101;
        juce::PopupMenu menu;
        const auto* track = TrackManager::getInstance().getTrack(trackId_);
        const bool canUngroupTracks =
            track != nullptr && track->isGroup() && !track->childIds.empty();
        if (canUngroupTracks) {
            menu.addItem(ungroupTracksId, "Ungroup tracks");
            menu.addSeparator();
        }

        auto& sel = SelectionManager::getInstance();
        const bool canGroupSelectedTracks =
            sel.getSelectedTrackCount() >= 2 && sel.isTrackSelected(trackId_);
        if (canGroupSelectedTracks) {
            menu.addItem(groupSelectedTracksId, "Group tracks");
            menu.addSeparator();
        }
        menu.addItem(deleteTrackId, "Delete Track");

        menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
            if (result == -103) {
                auto cmd = std::make_unique<UngroupTrackCommand>(trackId_);
                auto childIds = cmd->getChildren();
                UndoManager::getInstance().executeCommand(std::move(cmd));
                if (!childIds.empty()) {
                    std::unordered_set<TrackId> selectedChildren(childIds.begin(), childIds.end());
                    SelectionManager::getInstance().selectTracks(selectedChildren);
                }
            } else if (result == -102) {
                auto& selection = SelectionManager::getInstance();
                std::vector<TrackId> selectedTracks(selection.getSelectedTracks().begin(),
                                                    selection.getSelectedTracks().end());
                auto cmd = std::make_unique<GroupTracksCommand>(selectedTracks, "Group");
                auto* cmdPtr = cmd.get();
                UndoManager::getInstance().executeCommand(std::move(cmd));
                TrackId groupId = cmdPtr->getCreatedGroupId();
                if (groupId != INVALID_TRACK_ID)
                    selection.selectTrack(groupId);
            } else if (result == -101) {
                UndoManager::getInstance().executeCommand(
                    std::make_unique<DeleteTrackCommand>(trackId_));
            }
        });
    } else if (magda::isToggleSelectClick(event.mods)) {
        // Cmd+click: toggle this strip in the multi-selection
        selection.toggleTrackSelection(trackId_);
    } else if (!isMaster_ && magda::isRangeSelectClick(event.mods)) {
        // Shift+click: range-select from the anchor track to this one (using
        // the visible track order from TrackManager).
        auto& sel = SelectionManager::getInstance();
        TrackId anchor = sel.getAnchorTrack();
        const auto& tracks = TrackManager::getInstance().getTracks();
        int anchorIdx = -1, clickedIdx = -1;
        for (size_t i = 0; i < tracks.size(); ++i) {
            if (tracks[i].id == anchor)
                anchorIdx = static_cast<int>(i);
            if (tracks[i].id == trackId_)
                clickedIdx = static_cast<int>(i);
        }
        if (anchorIdx >= 0 && clickedIdx >= 0) {
            int lo = std::min(anchorIdx, clickedIdx);
            int hi = std::max(anchorIdx, clickedIdx);
            std::unordered_set<TrackId> rangeIds;
            for (int k = lo; k <= hi; ++k)
                rangeIds.insert(tracks[k].id);
            sel.selectTracks(rangeIds);
        } else if (onClicked) {
            onClicked(trackId_, isMaster_);
        }
    } else if (!fromChild && onClicked) {
        onClicked(trackId_, isMaster_);
    }
}

// MixerView implementation
MixerView::MixerView(AudioEngine* audioEngine) : audioEngine_(audioEngine) {
    // Get current view mode
    currentViewMode_ = ViewModeController::getInstance().getViewMode();

    // Keep the mini-chain "open editor" icons in sync with the actual plugin
    // window state. PluginWindowManager fires this on open AND on close (incl.
    // the window's own X), so the icon un-engages when the window is closed.
    if (auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(audioEngine_)) {
        if (auto* pwm = teWrapper->getPluginWindowManager()) {
            juce::Component::SafePointer<MixerView> safeThis(this);
            pwm->onWindowStateChanged = [safeThis](DeviceId deviceId, bool isOpen) {
                auto* self = safeThis.getComponent();
                if (self == nullptr)
                    return;
                for (auto& strip : self->channelStrips)
                    strip->syncMiniChainPluginWindow(deviceId, isOpen);
                for (auto& strip : self->auxChannelStrips)
                    strip->syncMiniChainPluginWindow(deviceId, isOpen);
            };
        }
    }

    // Create channel container
    channelContainer = std::make_unique<juce::Component>();
    channelContainer->setPaintingIsUnclipped(true);

    // Create viewport for scrollable channels
    channelViewport = std::make_unique<juce::Viewport>();
    channelViewport->setViewedComponent(channelContainer.get(), false);
    channelViewport->setScrollBarsShown(false, true);  // Horizontal scroll only
    addAndMakeVisible(*channelViewport);

    // Create aux container (fixed, between channels and master)
    auxContainer = std::make_unique<juce::Component>();
    addAndMakeVisible(*auxContainer);

    // Create master strip (uses shared MasterChannelStrip component)
    masterStrip = std::make_unique<MasterChannelStrip>(MasterChannelStrip::Orientation::Vertical);
    masterStrip->onSendAreaResized = [this]() { relayoutAllStrips(); };
    masterStrip->allVisibleLayoutTargetsProvider = [this]() {
        return getLayoutEditTargets(MASTER_TRACK_ID, true);
    };
    addAndMakeVisible(*masterStrip);

    // Channel resize handles are created lazily, one per top-level strip, in
    // layoutChannelResizeHandles().

    // Left-edge toggle rail
    toggleRail_ = std::make_unique<MixerToggleRail>();
    toggleRail_->onToggleChanged = [this]() {
        reconcileAnalysisDevices();
        resized();
        relayoutAllStrips();
    };
    addAndMakeVisible(*toggleRail_);

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Register as SelectionManager listener so multi-selected strips light up
    SelectionManager::getInstance().addListener(this);

    // Register as ViewModeController listener
    ViewModeController::getInstance().addListener(this);

    // Build channel strips from TrackManager
    rebuildChannelStrips();

    // Debug panel disabled - remove F12 toggle
    // debugPanel_ = std::make_unique<MixerDebugPanel>();
    // debugPanel_->setVisible(false);
    // debugPanel_->onMetricsChanged = [this]() { rebuildChannelStrips(); };
    // addAndMakeVisible(*debugPanel_);

    // Listen for MIDI device list changes
    if (audioEngine_) {
        if (auto* mb = audioEngine_->getMidiBridge())
            mb->addMidiDeviceListListener(this);
    }

    // Start timer for meter animation (30fps)
    startTimer(33);
}

void MixerView::midiDeviceListChanged() {
    juce::MessageManager::callAsync([this]() { tracksChanged(); });
}

MixerView::~MixerView() {
    if (auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(audioEngine_)) {
        if (auto* pwm = teWrapper->getPluginWindowManager())
            pwm->onWindowStateChanged = nullptr;
    }
    if (audioEngine_) {
        if (auto* mb = audioEngine_->getMidiBridge())
            mb->removeMidiDeviceListListener(this);
    }
    stopTimer();
    TrackManager::getInstance().removeListener(this);
    SelectionManager::getInstance().removeListener(this);
    ViewModeController::getInstance().removeListener(this);

    // Explicitly clear all UI components before automatic member destruction
    // This ensures components release their LookAndFeel references before
    // mixerLookAndFeel_ is destroyed (member destruction happens in reverse order)
    for (auto& strip : channelStrips)
        strip->groupChildren_.clear();
    orderedStrips_.clear();
    channelStrips.clear();
    auxChannelStrips.clear();
    masterStrip.reset();
    debugPanel_.reset();
    auxContainer.reset();
    channelContainer.reset();
    channelViewport.reset();
    channelResizeHandles_.clear();
    toggleRail_.reset();
}

void MixerView::rebuildChannelStrips() {
    // Clear group children references before destroying strips
    for (auto& strip : channelStrips)
        strip->groupChildren_.clear();

    // Clear existing strips
    orderedStrips_.clear();
    channelStrips.clear();

    const auto& tracks = TrackManager::getInstance().getTracks();

    for (const auto& track : tracks) {
        // Only show tracks visible in the current view mode
        if (!track.isVisibleIn(currentViewMode_)) {
            continue;
        }

        if (track.type == TrackType::Aux)
            continue;  // Aux strips handled separately

        // Skip children of collapsed group tracks
        if (track.hasParent()) {
            if (auto* parent = TrackManager::getInstance().getTrack(track.parentId)) {
                if ((parent->isGroup() || parent->hasChildren()) &&
                    parent->isCollapsedIn(currentViewMode_))
                    continue;
            }
        }

        auto strip = std::make_unique<ChannelStrip>(track, audioEngine_, false);
        strip->onClicked = [this](int trackId, bool isMaster) {
            // Find the index of this track in the visible strips
            for (size_t i = 0; i < channelStrips.size(); ++i) {
                if (channelStrips[i]->getTrackId() == trackId) {
                    selectChannel(static_cast<int>(i), isMaster);
                    break;
                }
            }
        };

        // Wire up send area resize callback (coalesced relayout of all strips)
        strip->onSendAreaResized = [this]() { relayoutAllStrips(); };

        // Add expand/collapse toggle for tracks with children (groups, DrumGrid, etc.)
        if (track.hasChildren()) {
            bool isCollapsed = track.isCollapsedIn(currentViewMode_);
            TrackId trackId = track.id;
            strip->expandToggle_ = std::make_unique<juce::TextButton>(
                isCollapsed ? juce::String::charToString(0x25B6)    // ▶
                            : juce::String::charToString(0x25BC));  // ▼
            strip->expandToggle_->setConnectedEdges(
                juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
            strip->expandToggle_->setColour(juce::TextButton::buttonColourId,
                                            DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
            strip->expandToggle_->setColour(juce::TextButton::textColourOffId,
                                            DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            strip->expandToggle_->onClick = [this, trackId]() {
                auto* t = TrackManager::getInstance().getTrack(trackId);
                if (t) {
                    bool collapsed = t->isCollapsedIn(currentViewMode_);
                    t->viewSettings.setCollapsed(currentViewMode_, !collapsed);
                }
                rebuildChannelStrips();
            };
            strip->addAndMakeVisible(*strip->expandToggle_);
        }

        channelStrips.push_back(std::move(strip));
    }

    // Second pass: build orderedStrips_ and wire up parent-child hierarchy.
    // Only real group children are nested for envelope rendering. Multi-out
    // tracks are routing-linked siblings, not child tracks, so they stay as
    // plain mixer strips like they do in arrangement/session.
    // Use addChildComponent (not addAndMakeVisible) to avoid intermediate layouts.

    std::unordered_map<int, ChannelStrip*> stripByTrackId;
    for (auto& strip : channelStrips)
        stripByTrackId[strip->getTrackId()] = strip.get();

    for (auto& strip : channelStrips) {
        int trackId = strip->getTrackId();
        const auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        if (!trackInfo)
            continue;

        // --- Nest inside parent group tracks ---
        if (trackInfo->hasParent()) {
            if (auto* parentTrack = TrackManager::getInstance().getTrack(trackInfo->parentId)) {
                if (parentTrack->isGroup() || parentTrack->hasChildren()) {
                    auto it = stripByTrackId.find(trackInfo->parentId);
                    if (it != stripByTrackId.end()) {
                        it->second->addChildComponent(*strip);
                        it->second->groupChildren_.push_back(strip.get());
                        continue;
                    }
                }
            }
        }

        // --- Top-level strip ---
        channelContainer->addChildComponent(*strip);
        orderedStrips_.push_back(strip.get());
    }

    // Now make everything visible.
    // Group parent strips must not be opaque — they need to paint the envelope
    // border around/behind their children.
    for (auto* strip : orderedStrips_)
        strip->setVisible(true);
    for (auto& strip : channelStrips) {
        if (!strip->groupChildren_.empty())
            strip->setOpaque(false);
        for (auto* child : strip->groupChildren_)
            child->setVisible(true);
    }

    // Build aux channel strips separately
    auxChannelStrips.clear();
    for (const auto& track : tracks) {
        if (track.type != TrackType::Aux || !track.isVisibleIn(currentViewMode_))
            continue;
        auto strip = std::make_unique<ChannelStrip>(track, audioEngine_, false);
        strip->onClicked = [this](int trackId, bool isMaster) {
            for (size_t i = 0; i < channelStrips.size(); ++i) {
                if (channelStrips[i]->getTrackId() == trackId) {
                    selectChannel(static_cast<int>(i), isMaster);
                    return;
                }
            }
            // Check aux strips — use negative index offset for identification
            for (size_t i = 0; i < auxChannelStrips.size(); ++i) {
                if (auxChannelStrips[i]->getTrackId() == trackId) {
                    // Select via TrackManager directly
                    SelectionManager::getInstance().selectTrack(trackId);
                    return;
                }
            }
        };
        auxContainer->addAndMakeVisible(*strip);
        auxChannelStrips.push_back(std::move(strip));
    }

    // Update master strip visibility
    const auto& master = TrackManager::getInstance().getMasterChannel();
    bool masterVisible = master.isVisibleIn(currentViewMode_);
    masterStrip->setVisible(masterVisible);

    // Sync selection with TrackManager's current selection
    trackSelectionChanged(TrackManager::getInstance().getSelectedTrack());

    // New tracks may need analyzer devices added (e.g. saved-on toggle).
    reconcileAnalysisDevices();

    // Newly built strips need their analyzer plugin pointers resolved. The
    // immediate refresh handles the steady-state case; the deferred one
    // catches project load, where PluginManagerSync may not yet have attached
    // the actual TE plugins to AudioBridge by the time we get here.
    for (auto& strip : channelStrips)
        strip->refreshMiniAnalyzers();
    for (auto& strip : auxChannelStrips)
        strip->refreshMiniAnalyzers();
    if (masterStrip)
        masterStrip->refreshMiniAnalyzers();
    juce::Component::SafePointer<MixerView> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (auto* self = safeThis.getComponent()) {
            for (auto& strip : self->channelStrips)
                strip->refreshMiniAnalyzers();
            for (auto& strip : self->auxChannelStrips)
                strip->refreshMiniAnalyzers();
            if (self->masterStrip)
                self->masterStrip->refreshMiniAnalyzers();
        }
    });

    resized();
}

void MixerView::tracksChanged() {
    // Rebuild all channel strips when tracks are added/removed/reordered
    rebuildChannelStrips();
}

void MixerView::trackPropertyChanged(int trackId) {
    // Update the specific channel strip - find it by track ID since indices may differ
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (!track)
        return;

    if (trackId == MASTER_TRACK_ID) {
        if (masterStrip) {
            masterStrip->resized();
            masterStrip->repaint();
            resized();
        }
        return;
    }

    for (auto& strip : channelStrips) {
        if (strip->getTrackId() == trackId) {
            strip->updateFromTrack(*track);
            return;
        }
    }

    // Check aux strips
    for (auto& strip : auxChannelStrips) {
        if (strip->getTrackId() == trackId) {
            strip->updateFromTrack(*track);
            return;
        }
    }
}

void MixerView::trackDevicesChanged(TrackId trackId) {
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (track) {
        for (auto& strip : channelStrips) {
            if (strip->getTrackId() == trackId) {
                strip->updateFromTrack(*track, true);
                break;
            }
        }
        for (auto& strip : auxChannelStrips) {
            if (strip->getTrackId() == trackId) {
                strip->updateFromTrack(*track, true);
                break;
            }
        }
    }
    // Re-resolve the changed strip's mini analyzer plugin pointers
    if (trackId == MASTER_TRACK_ID) {
        if (masterStrip)
            masterStrip->refreshMiniAnalyzers();
    } else {
        for (auto& strip : channelStrips) {
            if (strip->getTrackId() == trackId) {
                strip->refreshMiniAnalyzers();
                break;
            }
        }
    }
    // Sends region is uniform across strips (max-sends-on-any-track), so a
    // change on one strip forces a relayout on every strip and the master.
    relayoutAllStrips();
}

void MixerView::devicePropertyChanged(const ChainNodePath& devicePath) {
    // A device's bypass/gain/label changed. Sync the matching mini-chain row's
    // bypass dot in place. We deliberately do NOT rebuild rows here: this fires
    // synchronously from a row's own bypass toggle, so a rebuild would destroy
    // the row mid-callback. setBypassedState only repaints, which is re-entrant-safe.
    const TrackId trackId = devicePath.trackId;
    const DeviceId deviceId = devicePath.getDeviceId();
    if (trackId == INVALID_TRACK_ID || deviceId == INVALID_DEVICE_ID)
        return;

    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(devicePath);
    if (device == nullptr)
        return;

    for (auto& strip : channelStrips) {
        if (strip->getTrackId() == trackId) {
            strip->syncMiniChainRowState(deviceId, device->bypassed);
            return;
        }
    }
    for (auto& strip : auxChannelStrips) {
        if (strip->getTrackId() == trackId) {
            strip->syncMiniChainRowState(deviceId, device->bypassed);
            return;
        }
    }
}

void MixerView::viewModeChanged(ViewMode mode, const AudioEngineProfile& /*profile*/) {
    currentViewMode_ = mode;
    rebuildChannelStrips();
}

void MixerView::masterChannelChanged() {
    // Update master strip visibility
    const auto& master = TrackManager::getInstance().getMasterChannel();
    bool masterVisible = master.isVisibleIn(currentViewMode_);
    masterStrip->setVisible(masterVisible);
    resized();
}

void MixerView::paint(juce::Graphics& g) {
    MAGDA_MONITOR_SCOPE("UIFrame");
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

    // Left border (visible when side panel is collapsed)
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
    g.fillRect(0, 0, 1, getHeight());

    // Plugin drag overlay
    if (showPluginDropOverlay_) {
        if (dropTargetStripIndex_ >= 0 &&
            dropTargetStripIndex_ < static_cast<int>(orderedStrips_.size())) {
            // Highlight the specific strip being hovered
            auto* strip = orderedStrips_[dropTargetStripIndex_];
            auto stripBounds = getLocalArea(strip, strip->getLocalBounds());
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.25f));
            g.fillRect(stripBounds);
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.6f));
            g.drawRect(stripBounds, 2);
        } else {
            // Hovering empty area — show "new track" indicator at right edge of channel area
            auto vpBounds = channelViewport->getBounds();
            int indicatorWidth = DEFAULT_CHANNEL_WIDTH;
            int indicatorX = vpBounds.getRight() - indicatorWidth;
            // Clamp to viewport area
            if (indicatorX < vpBounds.getX())
                indicatorX = vpBounds.getX();
            auto indicatorBounds = juce::Rectangle<int>(indicatorX, vpBounds.getY(), indicatorWidth,
                                                        vpBounds.getHeight());
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.15f));
            g.fillRect(indicatorBounds);
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.4f));
            g.drawRect(indicatorBounds, 2);

            // Draw "+" icon
            auto centre = indicatorBounds.getCentre().toFloat();
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.7f));
            g.drawLine(centre.getX() - 10, centre.getY(), centre.getX() + 10, centre.getY(), 2.0f);
            g.drawLine(centre.getX(), centre.getY() - 10, centre.getX(), centre.getY() + 10, 2.0f);
        }
    }
}

int MixerView::getTopLevelStripWidth(const ChannelStrip& strip) const {
    int width = strip.preferredChannelWidth();
    for (auto* child : strip.groupChildren_) {
        if (auto* childStrip = dynamic_cast<ChannelStrip*>(child))
            width += childStrip->preferredChannelWidth();
        else
            width += DEFAULT_CHANNEL_WIDTH;
    }
    return width;
}

std::vector<TrackId> MixerView::getLayoutEditTargets(TrackId clickedId, bool allVisible) const {
    std::vector<TrackId> targets;
    auto& selection = SelectionManager::getInstance();
    if (!allVisible && selection.isTrackSelected(clickedId) &&
        selection.getSelectedTrackCount() > 1) {
        const auto& selected = selection.getSelectedTracks();
        targets.assign(selected.begin(), selected.end());
        return targets;
    }

    if (allVisible) {
        for (const auto& track : TrackManager::getInstance().getTracks()) {
            if (track.isVisibleIn(currentViewMode_))
                targets.push_back(track.id);
        }
        if (TrackManager::getInstance().getMasterChannel().isVisibleIn(currentViewMode_))
            targets.push_back(MASTER_TRACK_ID);
        return targets;
    }

    targets.push_back(clickedId);
    return targets;
}

void MixerView::applyChannelWidthDelta(TrackId clickedId, int deltaX,
                                       const juce::ModifierKeys& mods) {
    const bool sameValue = mods.isAltDown();
    if (channelWidthResizeTargets_.empty()) {
        channelWidthResizeTargets_ = getLayoutEditTargets(clickedId, sameValue);
        channelWidthResizeClickedStartWidth_ = effectiveMixerChannelWidth(clickedId);
        channelWidthResizeStartValues_.clear();
        channelWidthResizeStartEffective_.clear();
        for (auto tid : channelWidthResizeTargets_) {
            channelWidthResizeStartValues_[tid] = storedMixerChannelWidth(tid);
            channelWidthResizeStartEffective_[tid] = effectiveMixerChannelWidth(tid);
        }
    }

    const int sameWidth = juce::jlimit(MIN_CHANNEL_WIDTH, MAX_CHANNEL_WIDTH,
                                       channelWidthResizeClickedStartWidth_ + deltaX);
    auto& tm = TrackManager::getInstance();

    for (auto tid : channelWidthResizeTargets_) {
        const int baseWidth = sameValue ? channelWidthResizeClickedStartWidth_
                                        : channelWidthResizeStartEffective_[tid];
        const int newWidth = juce::jlimit(MIN_CHANNEL_WIDTH, MAX_CHANNEL_WIDTH, baseWidth + deltaX);
        tm.setTrackMixerChannelWidth(tid, sameValue ? sameWidth : newWidth);
    }

    isResizeDragging_ = true;
    if (!pendingResizeUpdate_) {
        pendingResizeUpdate_ = true;
        juce::Component::SafePointer<MixerView> safeThis(this);
        juce::MessageManager::callAsync([safeThis]() {
            if (auto* self = safeThis.getComponent()) {
                if (self->pendingResizeUpdate_) {
                    self->pendingResizeUpdate_ = false;
                    self->updateStripWidths();
                }
            }
        });
    }
}

void MixerView::finishChannelWidthResize(const juce::ModifierKeys& /*mods*/) {
    isResizeDragging_ = false;
    pendingResizeUpdate_ = false;
    commitChannelWidthResize();
    updateStripWidths();
}

void MixerView::resetChannelWidths(TrackId clickedId, const juce::ModifierKeys& mods) {
    std::vector<std::unique_ptr<UndoableCommand>> commands;
    for (auto tid : getLayoutEditTargets(clickedId, mods.isAltDown())) {
        const int oldWidth = storedMixerChannelWidth(tid);
        if (oldWidth != 0)
            commands.push_back(
                std::make_unique<SetTrackMixerChannelWidthCommand>(tid, oldWidth, 0));
    }
    executeMixerLayoutCommands("Reset Mixer Channel Width", std::move(commands));
    updateStripWidths();
}

void MixerView::commitChannelWidthResize() {
    std::vector<std::unique_ptr<UndoableCommand>> commands;
    for (auto tid : channelWidthResizeTargets_) {
        const auto oldIt = channelWidthResizeStartValues_.find(tid);
        if (oldIt == channelWidthResizeStartValues_.end())
            continue;
        const int oldWidth = oldIt->second;
        const int newWidth = storedMixerChannelWidth(tid);
        if (oldWidth != newWidth) {
            commands.push_back(
                std::make_unique<SetTrackMixerChannelWidthCommand>(tid, oldWidth, newWidth));
        }
    }

    channelWidthResizeTargets_.clear();
    channelWidthResizeStartValues_.clear();
    channelWidthResizeStartEffective_.clear();
    executeMixerLayoutCommands("Resize Mixer Channel Width", std::move(commands));
}

void MixerView::executeMixerLayoutCommands(const juce::String& description,
                                           std::vector<std::unique_ptr<UndoableCommand>> commands) {
    if (commands.empty())
        return;

    auto& undo = UndoManager::getInstance();
    CompoundOperationScope scope(description);
    for (auto& command : commands)
        undo.executeCommand(std::move(command));
}

void MixerView::resized() {
    auto bounds = getLocalBounds();

    // Left-edge toggle rail (always visible)
    if (toggleRail_) {
        toggleRail_->setBounds(bounds.removeFromLeft(MixerToggleRail::RAIL_WIDTH));
    }

    // Master strip on the right (only if visible)
    if (masterStrip->isVisible()) {
        masterStrip->setBounds(bounds.removeFromRight(effectiveMixerChannelWidth(MASTER_TRACK_ID)));
    }

    // Aux channel strips between regular channels and master
    int numAux = static_cast<int>(auxChannelStrips.size());
    int auxWidth = 0;
    for (const auto& strip : auxChannelStrips)
        auxWidth += strip->preferredChannelWidth();
    if (auxWidth > 0) {
        auto auxArea = bounds.removeFromRight(auxWidth);
        auxContainer->setBounds(auxArea);
        int auxX = 0;
        for (int i = 0; i < numAux; ++i) {
            const int stripWidth = auxChannelStrips[i]->preferredChannelWidth();
            auxChannelStrips[i]->setBounds(auxX, 0, stripWidth, auxArea.getHeight());
            auxX += stripWidth;
        }
    } else {
        auxContainer->setBounds(0, 0, 0, 0);
    }

    // 1px left border padding (visible when side panel is collapsed)
    bounds.removeFromLeft(1);

    // Channel viewport takes remaining space
    channelViewport->setBounds(bounds);

    // If orderedStrips_ hasn't been populated yet, fall back to channelStrips
    if (orderedStrips_.empty() && !channelStrips.empty()) {
        for (auto& s : channelStrips)
            orderedStrips_.push_back(s.get());
    }

    // Size the channel container — group strips may be wider than channelWidth
    int containerHeight = bounds.getHeight();
    int containerWidth = 0;
    for (auto* strip : orderedStrips_) {
        auto* cs = dynamic_cast<ChannelStrip*>(strip);
        int stripWidth = cs ? getTopLevelStripWidth(*cs) : DEFAULT_CHANNEL_WIDTH;
        containerWidth += stripWidth;
    }
    channelContainer->setSize(containerWidth, containerHeight);

    // Position all strips with cumulative x (group strips span multiple columns)
    int xPos = 0;
    for (auto* strip : orderedStrips_) {
        auto* cs = dynamic_cast<ChannelStrip*>(strip);
        int stripWidth = cs ? getTopLevelStripWidth(*cs) : DEFAULT_CHANNEL_WIDTH;
        strip->setBounds(xPos, 0, stripWidth, containerHeight);
        xPos += stripWidth;
    }

    layoutChannelResizeHandles(containerHeight);
}

void MixerView::wireChannelResizeHandle(ChannelResizeHandle& handle) {
    handle.onResize = [this, &handle](int deltaX, const juce::ModifierKeys& mods) {
        const TrackId trackId = handle.targetTrackId();
        if (trackId != INVALID_TRACK_ID)
            applyChannelWidthDelta(trackId, deltaX, mods);
    };
    handle.onResizeEnd = [this](const juce::ModifierKeys& mods) { finishChannelWidthResize(mods); };
    handle.onReset = [this, &handle](const juce::ModifierKeys& mods) {
        const TrackId trackId = handle.targetTrackId();
        if (trackId != INVALID_TRACK_ID)
            resetChannelWidths(trackId, mods);
    };
}

void MixerView::layoutChannelResizeHandles(int containerHeight) {
    // Keep one handle per top-level strip (grow/shrink the pool as strips
    // change). Each sits on its strip's right edge, so there's a grab point
    // between every pair of headers — and they all resize the shared width.
    while (channelResizeHandles_.size() < orderedStrips_.size()) {
        auto handle = std::make_unique<ChannelResizeHandle>();
        wireChannelResizeHandle(*handle);
        channelContainer->addAndMakeVisible(*handle);
        channelResizeHandles_.push_back(std::move(handle));
    }
    while (channelResizeHandles_.size() > orderedStrips_.size())
        channelResizeHandles_.pop_back();

    constexpr int handleWidth = 8;
    int xPos = 0;
    for (size_t i = 0; i < orderedStrips_.size(); ++i) {
        auto* cs = dynamic_cast<ChannelStrip*>(orderedStrips_[i]);
        int stripWidth = cs ? getTopLevelStripWidth(*cs) : DEFAULT_CHANNEL_WIDTH;
        xPos += stripWidth;
        channelResizeHandles_[i]->setTargetTrackId(cs ? cs->getTrackId() : INVALID_TRACK_ID);
        channelResizeHandles_[i]->setBounds(xPos - handleWidth / 2, 0, handleWidth,
                                            containerHeight);
        channelResizeHandles_[i]->toFront(false);
    }
}

void MixerView::updateStripWidths() {
    int containerHeight = channelContainer->getHeight();

    // Compute total container width with variable-width group strips
    int containerWidth = 0;
    for (auto* strip : orderedStrips_) {
        auto* cs = dynamic_cast<ChannelStrip*>(strip);
        int stripWidth = cs ? getTopLevelStripWidth(*cs) : DEFAULT_CHANNEL_WIDTH;
        containerWidth += stripWidth;
    }
    channelContainer->setSize(containerWidth, containerHeight);

    // Position strips with cumulative x
    int xPos = 0;
    for (auto* strip : orderedStrips_) {
        auto* cs = dynamic_cast<ChannelStrip*>(strip);
        int stripWidth = cs ? getTopLevelStripWidth(*cs) : DEFAULT_CHANNEL_WIDTH;
        strip->setBounds(xPos, 0, stripWidth, containerHeight);
        xPos += stripWidth;
    }

    layoutChannelResizeHandles(containerHeight);

    // Update aux strips
    int numAux = static_cast<int>(auxChannelStrips.size());
    int auxWidth = 0;
    for (const auto& strip : auxChannelStrips)
        auxWidth += strip->preferredChannelWidth();
    if (auxWidth > 0) {
        int auxX = 0;
        for (int i = 0; i < numAux; ++i) {
            const int stripWidth = auxChannelStrips[i]->preferredChannelWidth();
            auxChannelStrips[i]->setBounds(auxX, 0, stripWidth, auxContainer->getHeight());
            auxX += stripWidth;
        }
    }
}

void MixerView::reconcileAnalysisDevices() {
    auto& tm = TrackManager::getInstance();
    const auto& cfg = Config::getInstance();
    const bool wantOsc = cfg.getMixerShowOscilloscope();
    const bool wantSpec = cfg.getMixerShowSpectrum();

    auto reconcileOne = [&](TrackId tid, const juce::String& pluginId,
                            const juce::String& displayName, bool want) {
        DeviceId existing = tm.findMixerAnalysisDevice(tid, pluginId);
        const bool has = (existing != INVALID_DEVICE_ID);
        if (want && !has) {
            DeviceInfo device;
            device.name = displayName;
            device.manufacturer = "MAGDA";
            device.pluginId = pluginId;
            device.deviceType = DeviceType::Analysis;
            device.format = PluginFormat::Internal;
            tm.addDeviceToMixerAnalysis(tid, device);
        }
    };

    for (const auto& t : tm.getTracks()) {
        if (t.type == TrackType::Master)
            continue;
        reconcileOne(t.id, "oscilloscope", "Oscilloscope", wantOsc);
        reconcileOne(t.id, "spectrumanalyzer", "Spectrum Analyzer", wantSpec);
    }
    reconcileOne(MASTER_TRACK_ID, "oscilloscope", "Oscilloscope", wantOsc);
    reconcileOne(MASTER_TRACK_ID, "spectrumanalyzer", "Spectrum Analyzer", wantSpec);
}

void MixerView::relayoutAllStrips() {
    if (!pendingSendResizeUpdate_) {
        pendingSendResizeUpdate_ = true;
        juce::Component::SafePointer<MixerView> safeThis(this);
        juce::MessageManager::callAsync([safeThis]() {
            if (auto* self = safeThis.getComponent()) {
                if (self->pendingSendResizeUpdate_) {
                    self->pendingSendResizeUpdate_ = false;
                    for (auto& strip : self->channelStrips) {
                        strip->resized();
                        strip->repaint();
                    }
                    for (auto& strip : self->auxChannelStrips) {
                        strip->resized();
                        strip->repaint();
                    }
                    if (self->masterStrip) {
                        self->masterStrip->resized();
                        self->masterStrip->repaint();
                    }
                }
            }
        });
    }
}

void MixerView::timerCallback() {
    // Skip meter updates during resize drag to avoid repaints
    if (isResizeDragging_)
        return;

    // Read metering data from AudioBridge
    if (!audioEngine_)
        return;

    auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(audioEngine_);
    if (!teWrapper)
        return;

    auto* bridge = teWrapper->getAudioBridge();
    if (!bridge)
        return;

    auto& meteringBuffer = bridge->getMeteringBuffer();

    // Auto-clear held peaks on the rising edge of playback so the readouts
    // reflect the current take rather than the loudest-ever value. Clicking a
    // peak label resets it manually at any time (see ClickableLabel wiring).
    const bool isPlaying = bridge->isTransportPlaying();
    if (isPlaying && !wasPlaying_) {
        for (auto& strip : channelStrips)
            strip->resetPeak();
        for (auto& strip : auxChannelStrips)
            strip->resetPeak();
        if (masterStrip)
            masterStrip->resetPeak();
    }
    wasPlaying_ = isPlaying;

    // Update channel strip meters
    for (auto& strip : channelStrips) {
        int trackId = strip->getTrackId();
        MeterData data;
        if (meteringBuffer.popLevels(trackId, data)) {
            strip->setMeterLevels(data.peakL, data.peakR);
        }
    }

    // Update aux channel strip meters
    for (auto& strip : auxChannelStrips) {
        int trackId = strip->getTrackId();
        MeterData data;
        if (meteringBuffer.popLevels(trackId, data)) {
            strip->setMeterLevels(data.peakL, data.peakR);
        }
    }

    // Update master strip meters
    if (masterStrip) {
        float masterPeakL = bridge->getMasterPeakL();
        float masterPeakR = bridge->getMasterPeakR();
        masterStrip->setPeakLevels(masterPeakL, masterPeakR);
    }
}

bool MixerView::keyPressed(const juce::KeyPress& /*key*/) {
    // Debug panel disabled
    return false;
}

bool MixerView::isInChannelResizeZone(const juce::Point<int>& /*pos*/) const {
    // Not used anymore - resize handle component handles this
    return false;
}

void MixerView::mouseMove(const juce::MouseEvent& /*event*/) {
    // Resize handled by ChannelResizeHandle
}

void MixerView::mouseDown(const juce::MouseEvent& /*event*/) {
    // Resize handled by ChannelResizeHandle
}

void MixerView::mouseDrag(const juce::MouseEvent& /*event*/) {
    // Resize handled by ChannelResizeHandle
}

void MixerView::mouseUp(const juce::MouseEvent& /*event*/) {
    // Resize handled by ChannelResizeHandle
}

// ChannelResizeHandle implementation
MixerView::ChannelResizeHandle::ChannelResizeHandle() = default;

void MixerView::ChannelResizeHandle::paint(juce::Graphics& /*g*/) {
    // Invisible — cursor change on hover is the only affordance
}

void MixerView::ChannelResizeHandle::mouseEnter(const juce::MouseEvent& /*event*/) {
    isHovering_ = true;
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    repaint();
}

void MixerView::ChannelResizeHandle::mouseExit(const juce::MouseEvent& /*event*/) {
    isHovering_ = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void MixerView::ChannelResizeHandle::mouseDown(const juce::MouseEvent& event) {
    isDragging_ = true;
    hasConfirmedHorizontalDrag_ = false;
    dragStartX_ = event.getScreenX();
    lastMods_ = event.mods;
}

void MixerView::ChannelResizeHandle::mouseDrag(const juce::MouseEvent& event) {
    if (!isDragging_ || !onResize)
        return;

    if (!hasConfirmedHorizontalDrag_) {
        int dx = std::abs(event.getDistanceFromDragStartX());
        int dy = std::abs(event.getDistanceFromDragStartY());
        if (dx < 4 && dy < 4)
            return;
        if (dy > dx) {
            isDragging_ = false;
            return;
        }
        hasConfirmedHorizontalDrag_ = true;
    }

    int deltaX = event.getScreenX() - dragStartX_;
    lastMods_ = event.mods;
    onResize(deltaX, event.mods);
}

void MixerView::ChannelResizeHandle::mouseUp(const juce::MouseEvent& event) {
    isDragging_ = false;
    if (onResizeEnd)
        onResizeEnd(event.mods);
    repaint();
}

void MixerView::ChannelResizeHandle::mouseDoubleClick(const juce::MouseEvent& event) {
    if (onReset)
        onReset(event.mods);
}

void MixerView::selectChannel(int index, bool isMaster) {
    // Deselect all channel strips (including aux)
    for (auto& strip : channelStrips) {
        strip->setSelected(false);
    }
    for (auto& strip : auxChannelStrips) {
        strip->setSelected(false);
    }

    // Select the clicked channel
    if (isMaster) {
        selectedChannelIndex = -1;
        selectedIsMaster = true;
        SelectionManager::getInstance().selectTrack(MASTER_TRACK_ID);
    } else {
        if (index >= 0 && index < static_cast<int>(channelStrips.size())) {
            channelStrips[index]->setSelected(true);
            // Notify SelectionManager of selection (which syncs with TrackManager)
            SelectionManager::getInstance().selectTrack(channelStrips[index]->getTrackId());
        }
        selectedChannelIndex = index;
        selectedIsMaster = false;
    }

    // Notify listener
    if (onChannelSelected) {
        onChannelSelected(selectedChannelIndex, selectedIsMaster);
    }
}

void MixerView::trackSelectionChanged(TrackId /*trackId*/) {
    syncSelectionVisuals();
}

void MixerView::selectionTypeChanged(SelectionType /*newType*/) {
    syncSelectionVisuals();
}

void MixerView::multiTrackSelectionChanged(const std::unordered_set<TrackId>& /*trackIds*/) {
    syncSelectionVisuals();
}

void MixerView::syncSelectionVisuals() {
    // Drive every strip's highlight from SelectionManager. This is one path
    // for both single- and multi-track selection: each strip lights up iff
    // its track is in the current selection set. Master is master-track-id.
    auto& sel = SelectionManager::getInstance();
    const TrackId primary = sel.getSelectedTrack();
    for (auto& strip : channelStrips) {
        strip->setSelected(sel.isTrackSelected(strip->getTrackId()));
    }
    for (auto& strip : auxChannelStrips) {
        strip->setSelected(sel.isTrackSelected(strip->getTrackId()));
    }
    if (masterStrip)
        masterStrip->setSelected(sel.isTrackSelected(MASTER_TRACK_ID));

    selectedIsMaster = (primary == MASTER_TRACK_ID);
    selectedChannelIndex = -1;
    for (size_t i = 0; i < channelStrips.size(); ++i) {
        if (channelStrips[i]->getTrackId() == primary) {
            selectedChannelIndex = static_cast<int>(i);
            break;
        }
    }
    if (onChannelSelected)
        onChannelSelected(selectedChannelIndex, selectedIsMaster);
}

// ============================================================================
// DragAndDropTarget implementation (plugin drops from browser)
// ============================================================================

bool MixerView::isInterestedInDragSource(const SourceDetails& details) {
    if (auto* obj = details.description.getDynamicObject()) {
        return obj->getProperty("type").toString() == "plugin";
    }
    return false;
}

void MixerView::itemDragEnter(const SourceDetails& details) {
    showPluginDropOverlay_ = true;
    // Determine which strip is being hovered
    auto localPos = details.localPosition;
    dropTargetStripIndex_ = -1;

    // Hit-test against channel strips in the viewport
    auto viewportPos = channelViewport->getLocalPoint(this, localPos);
    int scrollX = channelViewport->getViewPositionX();
    int hitX = viewportPos.getX() + scrollX;

    int cumX = 0;
    for (int i = 0; i < static_cast<int>(orderedStrips_.size()); ++i) {
        int stripWidth = orderedStrips_[i]->getWidth();
        if (stripWidth <= 0)
            if (auto* strip = dynamic_cast<ChannelStrip*>(orderedStrips_[i]))
                stripWidth = getTopLevelStripWidth(*strip);
        if (stripWidth <= 0)
            stripWidth = DEFAULT_CHANNEL_WIDTH;
        if (hitX >= cumX && hitX < cumX + stripWidth) {
            dropTargetStripIndex_ = i;
            break;
        }
        cumX += stripWidth;
    }

    repaint();
}

void MixerView::itemDragMove(const SourceDetails& details) {
    auto localPos = details.localPosition;
    int oldIndex = dropTargetStripIndex_;
    dropTargetStripIndex_ = -1;

    // Hit-test against channel strips in the viewport
    auto viewportPos = channelViewport->getLocalPoint(this, localPos);
    int scrollX = channelViewport->getViewPositionX();
    int hitX = viewportPos.getX() + scrollX;

    int cumX = 0;
    for (int i = 0; i < static_cast<int>(orderedStrips_.size()); ++i) {
        int stripWidth = orderedStrips_[i]->getWidth();
        if (stripWidth <= 0)
            if (auto* strip = dynamic_cast<ChannelStrip*>(orderedStrips_[i]))
                stripWidth = getTopLevelStripWidth(*strip);
        if (stripWidth <= 0)
            stripWidth = DEFAULT_CHANNEL_WIDTH;
        if (hitX >= cumX && hitX < cumX + stripWidth) {
            dropTargetStripIndex_ = i;
            break;
        }
        cumX += stripWidth;
    }

    if (dropTargetStripIndex_ != oldIndex)
        repaint();
}

void MixerView::itemDragExit(const SourceDetails& /*details*/) {
    showPluginDropOverlay_ = false;
    dropTargetStripIndex_ = -1;
    repaint();
}

void MixerView::itemDropped(const SourceDetails& details) {
    showPluginDropOverlay_ = false;
    int targetStrip = dropTargetStripIndex_;
    dropTargetStripIndex_ = -1;
    repaint();

    auto* obj = details.description.getDynamicObject();
    if (!obj)
        return;

    auto device = TrackManager::deviceInfoFromPluginObject(*obj);

    if (targetStrip >= 0 && targetStrip < static_cast<int>(orderedStrips_.size())) {
        // Drop on existing strip — add plugin to that track's chain
        if (auto* cs = dynamic_cast<ChannelStrip*>(orderedStrips_[targetStrip])) {
            TrackId trackId = cs->getTrackId();
            TrackManager::getInstance().addDeviceToTrack(trackId, device);
        }
    } else {
        // Drop on empty area — create new track with plugin
        TrackType trackType = TrackType::Audio;
        juce::String pluginName = obj->getProperty("name").toString();
        auto cmd = std::make_unique<CreateTrackWithDeviceCommand>(pluginName, trackType, device);
        UndoManager::getInstance().executeCommand(std::move(cmd));
    }
}

}  // namespace magda
