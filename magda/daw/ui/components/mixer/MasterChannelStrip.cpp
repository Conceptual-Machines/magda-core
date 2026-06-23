#include "MasterChannelStrip.hpp"

#include <cmath>

#include "../../../audio/AudioBridge.hpp"
#include "../../../audio/plugins/OscilloscopePlugin.hpp"
#include "../../../audio/plugins/SpectrumAnalyzerPlugin.hpp"
#include "../../../engine/AudioEngine.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/MixerMetrics.hpp"
#include "../../utils/SelectionPolicy.hpp"
#include "BinaryData.h"
#include "LevelMeterScale.hpp"
#include "core/ChainNodePath.hpp"
#include "core/Config.hpp"
#include "core/SelectionManager.hpp"
#include "core/StringTable.hpp"
#include "core/TechnicalText.hpp"
#include "core/TrackManager.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda {

// dB conversion helpers
namespace {
constexpr float MIN_DB = level_meter_scale::minDb;

int effectiveMixerFaderTopInset(TrackId trackId) {
    if (auto* track = TrackManager::getInstance().getTrack(trackId))
        return juce::jlimit(MixerMetrics::minFaderTopInset, MixerMetrics::maxFaderTopInset,
                            track->mixerFaderTopInset);
    return MixerMetrics::minFaderTopInset;
}

int storedMixerFaderTopInset(TrackId trackId) {
    if (auto* track = TrackManager::getInstance().getTrack(trackId))
        return track->mixerFaderTopInset;
    return 0;
}

float gainToDb(float gain) {
    return level_meter_scale::gainToDb(gain);
}

float dbToGain(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

// Convert dB to normalized meter position (0-1) with power curve
float dbToMeterPos(float db) {
    return level_meter_scale::dbToMeterPos(db);
}

// Convert meter position back to dB (inverse of dbToMeterPos)
float meterPosToDb(float pos) {
    return level_meter_scale::meterPosToDb(pos);
}
}  // namespace

// Resize handle for the send area boundary (matches channel strip's SendResizeHandle)
class MasterChannelStrip::ResizeHandle : public juce::Component {
  public:
    ResizeHandle() {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void paint(juce::Graphics& g) override {
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
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (!isDragging_ || !onResize)
            return;
        int deltaY = event.getScreenY() - dragStartY_;
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
    int dragStartY_ = 0;
};

// dB scale component — draws tick marks and dB labels, resizes with fader area
class MasterChannelStrip::DbScale : public juce::Component {
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

        float paddingTop = metrics.labelTextHeight / 2.0f + 1.0f;
        float paddingBottom = metrics.labelTextHeight / 2.0f;
        float top = paddingTop;
        float height = static_cast<float>(bounds.getHeight()) - paddingTop - paddingBottom;
        float totalWidth = static_cast<float>(bounds.getWidth());

        const float tickShort = metrics.tickWidth();
        const float tickLong = tickShort * 1.8f;
        const float labelLeftPad = tickLong + 2.0f;
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

MasterChannelStrip::MasterChannelStrip(Orientation orientation) : orientation_(orientation) {
    setupControls();

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Load initial state
    updateFromMasterState();
}

MasterChannelStrip::~MasterChannelStrip() {
    TrackManager::getInstance().removeListener(this);
}

void MasterChannelStrip::refreshMiniAnalyzers() {
    auto& tm = TrackManager::getInstance();
    auto* engine = tm.getAudioEngine();
    auto* bridge = engine ? engine->getAudioBridge() : nullptr;

    if (miniOscilloscopeUI_) {
        daw::audio::OscilloscopePlugin* osc = nullptr;
        DeviceId id = INVALID_DEVICE_ID;
        if (bridge) {
            id = tm.findMixerAnalysisDevice(MASTER_TRACK_ID, "oscilloscope");
            if (id != INVALID_DEVICE_ID) {
                auto pluginPtr =
                    bridge->getPlugin(ChainNodePath::mixerAnalysisDevice(MASTER_TRACK_ID, id));
                osc = dynamic_cast<daw::audio::OscilloscopePlugin*>(pluginPtr.get());
            }
        }
        miniOscilloscopeUI_->setPlugin(osc);
    }

    if (miniSpectrumUI_) {
        daw::audio::SpectrumAnalyzerPlugin* spec = nullptr;
        DeviceId id = INVALID_DEVICE_ID;
        if (bridge) {
            id = tm.findMixerAnalysisDevice(MASTER_TRACK_ID, "spectrumanalyzer");
            if (id != INVALID_DEVICE_ID) {
                auto pluginPtr =
                    bridge->getPlugin(ChainNodePath::mixerAnalysisDevice(MASTER_TRACK_ID, id));
                spec = dynamic_cast<daw::audio::SpectrumAnalyzerPlugin*>(pluginPtr.get());
            }
        }
        miniSpectrumUI_->setPlugin(spec);
    }
}

void MasterChannelStrip::setupControls() {
    // Title label
    titleLabel = std::make_unique<juce::Label>(
        "Master", magda::technicalText(magda::TechnicalTextToken::Master));
    titleLabel->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    titleLabel->setFont(FontManager::getInstance().getUIFont(12.0f));
    titleLabel->setJustificationType(juce::Justification::centredLeft);
    titleLabel->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*titleLabel);

    // Peak meter
    peakMeter = std::make_unique<LevelMeter>();
    addAndMakeVisible(*peakMeter);

    // Peak / fader-value readout above the fader. Mono font keeps digits in
    // a tabular grid so they don't shift sideways as the value changes.
    peakValueLabel = std::make_unique<ClickableLabel>();
    peakValueLabel->setText("-inf", juce::dontSendNotification);
    peakValueLabel->setJustificationType(juce::Justification::centred);
    peakValueLabel->setColour(juce::Label::textColourId,
                              DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    peakValueLabel->setFont(FontManager::getInstance().getMonoFont(10.0f));
    peakValueLabel->setTooltip("Click to reset peak");
    peakValueLabel->onClick = [this]() { resetPeak(); };
    addAndMakeVisible(*peakValueLabel);

    // Volume slider - TextSlider with vertical orientation and dB display
    volumeSlider = std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decibels);
    volumeSlider->setOrientation(daw::ui::TextSlider::Orientation::Vertical);
    volumeSlider->setRange(0.0, 1.0, 0.001);
    volumeSlider->setValue(dbToMeterPos(0.0f), juce::dontSendNotification);
    volumeSlider->setFont(FontManager::getInstance().getUIFont(9.0f));

    // Custom formatter: normalized position (0-1) -> dB string
    volumeSlider->setValueFormatter([](double pos) -> juce::String {
        float db = meterPosToDb(static_cast<float>(pos));
        if (db <= MIN_DB)
            return "-inf";
        if (std::abs(db) < 0.05f)
            db = 0.0f;
        return juce::String(db, 1);
    });

    // Custom parser: user input text -> normalized position (0-1)
    volumeSlider->setValueParser([](const juce::String& text) -> double {
        auto t = text.trim();
        if (t.endsWithIgnoreCase("db"))
            t = t.dropLastCharacters(2).trim();
        if (t.equalsIgnoreCase("-inf") || t.equalsIgnoreCase("inf"))
            return 0.0;
        float db = t.getFloatValue();
        return static_cast<double>(dbToMeterPos(db));
    });

    volumeSlider->setShowText(false);

    volumeSlider->onValueChanged = [](double pos) {
        float db = meterPosToDb(static_cast<float>(pos));
        float gain = dbToGain(db);
        UndoManager::getInstance().executeCommand(std::make_unique<SetMasterVolumeCommand>(gain));
    };
    addAndMakeVisible(*volumeSlider);

    // DbScale component
    dbScale_ = std::make_unique<DbScale>();
    addAndMakeVisible(*dbScale_);

    // Resize handle — controls faderTopInset to mirror channel strips.
    resizeHandle_ = std::make_unique<ResizeHandle>();
    resizeHandle_->onResize = [this](int deltaY, const juce::ModifierKeys& mods) {
        auto& metrics = MixerMetrics::getInstance();
        int fixedHeight = 38 + metrics.controlSpacing + 120 + 24 + metrics.buttonSize +
                          metrics.channelPadding * 2;
        int maxInset = juce::jmax(MixerMetrics::minFaderTopInset, getHeight() - fixedHeight);
        const int limit = juce::jmin(maxInset, MixerMetrics::maxFaderTopInset);
        const bool sameValue = mods.isAltDown();

        if (faderHeightResizeTargets_.empty()) {
            if (sameValue && allVisibleLayoutTargetsProvider)
                faderHeightResizeTargets_ = allVisibleLayoutTargetsProvider();
            if (faderHeightResizeTargets_.empty())
                faderHeightResizeTargets_ = {MASTER_TRACK_ID};

            faderHeightResizeClickedStartInset_ = effectiveMixerFaderTopInset(MASTER_TRACK_ID);
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
    resizeHandle_->onResizeEnd = [this](const juce::ModifierKeys&) {
        std::vector<std::unique_ptr<UndoableCommand>> commands;
        for (auto tid : faderHeightResizeTargets_) {
            const auto oldIt = faderHeightResizeStartValues_.find(tid);
            if (oldIt == faderHeightResizeStartValues_.end())
                continue;
            const int oldInset = oldIt->second;
            const int newInset = storedMixerFaderTopInset(tid);
            if (oldInset != newInset) {
                commands.push_back(
                    std::make_unique<SetTrackMixerFaderTopInsetCommand>(tid, oldInset, newInset));
            }
        }

        faderHeightResizeTargets_.clear();
        faderHeightResizeStartValues_.clear();
        faderHeightResizeStartEffective_.clear();

        if (commands.empty())
            return;

        auto& undo = UndoManager::getInstance();
        CompoundOperationScope scope("Resize Mixer Fader Height");
        for (auto& command : commands)
            undo.executeCommand(std::move(command));
    };
    resizeHandle_->onReset = [this](const juce::ModifierKeys& mods) {
        auto targets = std::vector<TrackId>{MASTER_TRACK_ID};
        if (mods.isAltDown() && allVisibleLayoutTargetsProvider) {
            targets = allVisibleLayoutTargetsProvider();
            if (targets.empty())
                targets = {MASTER_TRACK_ID};
        }

        std::vector<std::unique_ptr<UndoableCommand>> commands;
        for (auto tid : targets) {
            const int oldInset = storedMixerFaderTopInset(tid);
            if (oldInset != 0) {
                commands.push_back(
                    std::make_unique<SetTrackMixerFaderTopInsetCommand>(tid, oldInset, 0));
            }
        }

        if (!commands.empty()) {
            auto& undo = UndoManager::getInstance();
            CompoundOperationScope scope("Reset Mixer Fader Height");
            for (auto& command : commands)
                undo.executeCommand(std::move(command));
        }

        if (onSendAreaResized)
            onSendAreaResized();
    };
    addAndMakeVisible(*resizeHandle_);

    // Mini Oscilloscope / Spectrum bound to the master track's MixerAnalysis
    // section; rail toggle controls their visibility.
    // Expanding the compact analyzer controls grows the strip; relayout so the
    // plot isn't squeezed into the fixed height (mirrors the channel strips).
    auto relayoutOnExpand = [this]() {
        if (onSendAreaResized)
            onSendAreaResized();
    };

    miniOscilloscopeUI_ = std::make_unique<daw::ui::OscilloscopeUI>();
    miniOscilloscopeUI_->setCompact(true);
    miniOscilloscopeUI_->setPersistGlobalDefaults(false);
    miniOscilloscopeUI_->setVisible(false);
    miniOscilloscopeUI_->onControlsExpandedChanged = relayoutOnExpand;
    addAndMakeVisible(*miniOscilloscopeUI_);

    miniSpectrumUI_ = std::make_unique<daw::ui::SpectrumAnalyzerUI>();
    miniSpectrumUI_->setCompact(true);
    miniSpectrumUI_->setTrackId(MASTER_TRACK_ID);  // masking overlay (shown when popped out)
    miniSpectrumUI_->setPersistGlobalDefaults(false);
    miniSpectrumUI_->setVisible(false);
    miniSpectrumUI_->onControlsExpandedChanged = relayoutOnExpand;
    addAndMakeVisible(*miniSpectrumUI_);

    // Headphone icon (non-interactive, just a label)
    auto hpIcon = juce::Drawable::createFromImageData(BinaryData::headphones_svg,
                                                      BinaryData::headphones_svgSize);
    headphoneIcon_ =
        std::make_unique<juce::DrawableButton>("Headphones", juce::DrawableButton::ImageFitted);
    headphoneIcon_->setImages(hpIcon.get());
    headphoneIcon_->setClickingTogglesState(false);
    headphoneIcon_->setInterceptsMouseClicks(false, false);
    headphoneIcon_->setColour(juce::DrawableButton::backgroundColourId,
                              juce::Colours::transparentBlack);
    addAndMakeVisible(*headphoneIcon_);

    // Cue volume slider (horizontal)
    cueVolumeSlider_ = std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decibels);
    cueVolumeSlider_->setOrientation(daw::ui::TextSlider::Orientation::Horizontal);
    cueVolumeSlider_->setRange(0.0, 1.0, 0.001);
    cueVolumeSlider_->setValue(0.0, juce::dontSendNotification);  // -inf by default
    cueVolumeSlider_->setFont(FontManager::getInstance().getUIFont(9.0f));

    cueVolumeSlider_->setValueFormatter([](double pos) -> juce::String {
        float db = meterPosToDb(static_cast<float>(pos));
        if (db <= MIN_DB)
            return "-inf";
        if (std::abs(db) < 0.05f)
            db = 0.0f;
        return juce::String(db, 1);
    });

    cueVolumeSlider_->setValueParser([](const juce::String& text) -> double {
        auto t = text.trim();
        if (t.endsWithIgnoreCase("db"))
            t = t.dropLastCharacters(2).trim();
        if (t.equalsIgnoreCase("-inf") || t.equalsIgnoreCase("inf"))
            return 0.0;
        float db = t.getFloatValue();
        return static_cast<double>(dbToMeterPos(db));
    });

    // TODO: Wire to cue bus volume when implemented
    cueVolumeSlider_->onValueChanged = [](double /*pos*/) {};
    addAndMakeVisible(*cueVolumeSlider_);

    // Speaker on/off button (toggles master mute). Dual-icon SvgButton matching
    // the inspector: gray speaker (master_on) when audible, orange chip
    // (master_off) when muted. SvgButton's iconPadding + cornerRadius give the
    // padding and rounded box a raw DrawableButton can't.
    speakerButton = std::make_unique<magda::SvgButton>(
        "Speaker", BinaryData::master_on_svg, BinaryData::master_on_svgSize,
        BinaryData::master_off_svg, BinaryData::master_off_svgSize);
    speakerButton->setClickingTogglesState(true);
    speakerButton->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    speakerButton->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    speakerButton->setIconPadding(3.5f);  // larger speaker glyph
    speakerButton->onClick = [this]() {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetMasterMuteCommand>(speakerButton->getToggleState()));
    };
    addAndMakeVisible(*speakerButton);
}

void MasterChannelStrip::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));

    // Draw border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    auto ownBounds = getLocalBounds();
    const int stripHeight = 4;
    const int labelRowBottom = stripHeight + 26;
    if (selected_) {
        g.setColour(juce::Colours::black);
        g.fillRect(1, 1, ownBounds.getWidth() - 2, labelRowBottom);
    }
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
    g.fillRect(1, labelRowBottom, ownBounds.getWidth() - 2, 1);
}

void MasterChannelStrip::setSelected(bool shouldBeSelected) {
    if (selected_ != shouldBeSelected) {
        selected_ = shouldBeSelected;
        titleLabel->setColour(juce::Label::textColourId,
                              selected_ ? juce::Colours::white
                                        : DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        repaint();
    }
}

void MasterChannelStrip::mouseDown(const juce::MouseEvent& event) {
    if (!event.mods.isPopupMenu()) {
        if (magda::isToggleSelectClick(event.mods)) {
            SelectionManager::getInstance().toggleTrackSelection(MASTER_TRACK_ID);
        } else {
            SelectionManager::getInstance().selectTrack(MASTER_TRACK_ID);
        }
    }
}

void MasterChannelStrip::resized() {
    const auto& metrics = MixerMetrics::getInstance();
    auto bounds = getLocalBounds().reduced(metrics.channelPadding);

    if (orientation_ == Orientation::Vertical) {
        // Color indicator space (matches channel strip)
        bounds.removeFromTop(6);

        // Title row: [speaker icon] [Master label]
        auto titleRow = bounds.removeFromTop(24);
        auto speakerSlot = titleRow.removeFromLeft(20);
        titleRow.removeFromLeft(2);
        // Both the speaker and the "Master" label center vertically in the
        // painted title bar (component y in [1, labelRowBottom] — see paint()),
        // which is anchored to the component top and so sits ~6px higher than
        // the padded titleRow.
        constexpr int kTitleBarBottom = 4 + 26;  // labelRowBottom in paint()
        const int barMidY = (1 + kTitleBarBottom) / 2;
        titleLabel->setBounds(titleRow.withY(1).withHeight(kTitleBarBottom - 1));
        speakerButton->setBounds(
            juce::Rectangle<int>(0, 0, 18, 18).withCentre({speakerSlot.getCentreX(), barMidY}));

        bounds.removeFromTop(metrics.controlSpacing);

        // Mini analyzers, mirrored from ChannelStrip. Rail toggles control
        // visibility; refreshMiniAnalyzers() resolves the live plugin.
        constexpr int miniAnalyzerHeight = 64;
        const auto& cfg = Config::getInstance();
        if (cfg.getMixerShowOscilloscope() && miniOscilloscopeUI_) {
            const int h = miniAnalyzerHeight + miniOscilloscopeUI_->compactExtraHeight();
            miniOscilloscopeUI_->setBounds(bounds.removeFromTop(h));
            miniOscilloscopeUI_->setVisible(true);
            bounds.removeFromTop(2);
        } else if (miniOscilloscopeUI_) {
            miniOscilloscopeUI_->setVisible(false);
        }
        if (cfg.getMixerShowSpectrum() && miniSpectrumUI_) {
            const int h = miniAnalyzerHeight + miniSpectrumUI_->compactExtraHeight();
            miniSpectrumUI_->setBounds(bounds.removeFromTop(h));
            miniSpectrumUI_->setVisible(true);
            bounds.removeFromTop(2);
        } else if (miniSpectrumUI_) {
            miniSpectrumUI_->setVisible(false);
        }

        // Mirror channel-strip layout so the handle and fader line up: 2px
        // gap, then reserve the same sends region (Add Send row + max sends
        // across all tracks) when sends are visible, then the fader-top
        // inset, then the resize handle.
        bounds.removeFromTop(2);

        if (Config::getInstance().getMixerShowSends()) {
            const int sendSlotHeight = 18;
            size_t maxSends = 0;
            for (const auto& t : TrackManager::getInstance().getTracks())
                maxSends = std::max(maxSends, t.sends.size());
            int uniformSendsRegion = sendSlotHeight;
            if (maxSends > 0)
                uniformSendsRegion += 1 + static_cast<int>(maxSends) * (sendSlotHeight + 1) - 1;
            bounds.removeFromTop(uniformSendsRegion);
            bounds.removeFromTop(6);  // breathing room before handle
        }

        bounds.removeFromTop(effectiveMixerFaderTopInset(MASTER_TRACK_ID));

        if (resizeHandle_) {
            resizeHandle_->setBounds(bounds.removeFromTop(6));
            resizeHandle_->setAlwaysOnTop(true);
        }

        // Cue volume at bottom: [headphone icon] [slider]
        bounds.removeFromBottom(2);
        auto cueRow = bounds.removeFromBottom(20);
        headphoneIcon_->setBounds(cueRow.removeFromLeft(18));
        cueRow.removeFromLeft(2);
        cueVolumeSlider_->setBounds(cueRow);
        bounds.removeFromBottom(2);  // Gap between cue row and readout

        // Readout sits just below the fader, with a small breathing strip
        // beneath it so the number doesn't kiss the cue row.
        const int labelHeight = 12;
        const int bottomStrip = 4;
        bounds.removeFromBottom(bottomStrip);
        peakValueLabel->setBounds(bounds.removeFromBottom(labelHeight));

        // Reserve room above the meter so the top dB label (+6) isn't clipped
        // by the always-on-top resize handle (matches the track strips).
        bounds.removeFromTop(static_cast<int>(metrics.labelTextHeight / 2.0f + 2.0f));

        // Slider overlays the peak meter; dB scale (tick + label) sits to the
        // right. The slider paints only the thumb so the meter shows through.
        faderRegion_ = bounds;
        auto layoutArea = bounds;

        const int scaleWidth = 28;
        const int scaleGap = metrics.tickToMeterGap;
        auto scaleColumn = layoutArea.removeFromRight(scaleWidth);
        layoutArea.removeFromRight(scaleGap);

        peakMeterArea_ = layoutArea;
        faderArea_ = layoutArea;
        peakMeter->setBounds(peakMeterArea_);
        volumeSlider->setBounds(faderArea_);
        volumeSlider->toFront(false);

        int labelPad = static_cast<int>(metrics.labelTextHeight / 2.0f + 1.0f);
        dbScale_->setBounds(scaleColumn.getX(), peakMeterArea_.getY() - labelPad,
                            scaleColumn.getWidth(), peakMeterArea_.getHeight() + labelPad * 2);
    } else {
        // Horizontal layout (for Arrange view - at bottom of track content)
        titleLabel->setBounds(bounds.removeFromLeft(60));
        bounds.removeFromLeft(8);

        // Mute button
        speakerButton->setBounds(bounds.removeFromLeft(28).withSizeKeepingCentre(18, 18));
        bounds.removeFromLeft(8);

        // Value label above meter
        auto labelArea = bounds.removeFromTop(12);
        peakValueLabel->setBounds(labelArea.removeFromRight(40));

        // Single meter on right
        peakMeter->setBounds(bounds.removeFromRight(6));
        bounds.removeFromRight(4);
        volumeSlider->setBounds(bounds);

        // Hide components not used in horizontal mode
        dbScale_->setBounds(juce::Rectangle<int>());
        resizeHandle_->setBounds(juce::Rectangle<int>());
        headphoneIcon_->setBounds(juce::Rectangle<int>());
        cueVolumeSlider_->setBounds(juce::Rectangle<int>());

        // Clear vertical layout regions
        faderRegion_ = juce::Rectangle<int>();
        faderArea_ = juce::Rectangle<int>();
        peakMeterArea_ = juce::Rectangle<int>();
    }
}

void MasterChannelStrip::masterChannelChanged() {
    updateFromMasterState();
}

void MasterChannelStrip::updateFromMasterState() {
    const auto& master = TrackManager::getInstance().getMasterChannel();

    // Convert linear gain to fader position
    float db = gainToDb(master.volume);
    float faderPos = dbToMeterPos(db);
    volumeSlider->setValue(faderPos, juce::dontSendNotification);

    // Update mute button
    if (speakerButton) {
        speakerButton->setToggleState(master.muted, juce::dontSendNotification);
    }
}

void MasterChannelStrip::setPeakLevels(float leftPeak, float rightPeak) {
    if (peakMeter) {
        peakMeter->setLevels(leftPeak, rightPeak);
    }

    // Update peak value display (show max of both channels)
    float maxPeak = std::max(leftPeak, rightPeak);
    if (maxPeak > peakValue_) {
        peakValue_ = maxPeak;
        if (peakValueLabel) {
            float db = gainToDb(peakValue_);
            if (std::abs(db) < 0.05f)
                db = 0.0f;
            juce::String peakText;
            if (db <= MIN_DB) {
                peakText = "-inf";
            } else {
                peakText = juce::String(db, 1);
            }
            peakValueLabel->setText(peakText, juce::dontSendNotification);
        }
    }
}

void MasterChannelStrip::resetPeak() {
    peakValue_ = 0.0f;
    if (peakValueLabel)
        peakValueLabel->setText("-inf", juce::dontSendNotification);
}

}  // namespace magda
