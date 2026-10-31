// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SpectrumAnalyzerUI.hpp"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>

#include "AnalyzerColours.hpp"
#include "AnalyzerWindow.hpp"
#include "core/Config.hpp"
#include "core/TrackManager.hpp"
#include "core/TrackMeasurementManager.hpp"
#include "ui/components/chain/layout/NodeHeaderStyles.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kControlRowH = 22;    // full-editor horizontal control row
constexpr int kOverlayRowH = 22;    // full-editor top row: masking overlay picker
constexpr int kStackRowH = 18;      // one row of the compact vertical control stack
constexpr int kStackLabelW = 34;    // label column width in the stacked layout
constexpr int kChevronStripH = 16;  // dedicated strip below the plot for the chevron
constexpr float kSlopeOptions[] = {0.0f, 3.0f, 4.5f, 6.0f};  // dB/oct
constexpr float kSpeedOptions[] = {0.15f, 0.4f, 0.8f};       // smoothing (Slow/Med/Fast)
constexpr int kTapReadSamples = 32768;                       // the device's tap ring
constexpr int kOverlayReadSamples = 16384;  // the track tap's spectrum ring (magda-sdk)

template <typename Range> int nearestIndex(const Range& options, float value) {
    int best = 0;
    float bestErr = std::numeric_limits<float>::max();
    int i = 0;
    for (float opt : options) {
        const float err = std::abs(opt - value);
        if (err < bestErr) {
            bestErr = err;
            best = i;
        }
        ++i;
    }
    return best;
}

// Snapshot the plugin's current settings as the global last-used spectrum
// default (config.json), so the next freshly-created spectrum adopts them.
void persistSpectrumDefaults(const std::shared_ptr<SpectrumTelemetrySource>& telemetry,
                             bool enabled) {
    if (telemetry == nullptr || !enabled)
        return;
    Config::SpectrumDefaults d;
    d.fftOrder = telemetry->fftOrder();
    d.slopeDbPerOct = telemetry->slopeDbPerOct();
    d.smoothing = telemetry->smoothing();
    // The last-used colour too: the header's toggle deletes the device, so its
    // own document does not survive turning an analyser off and on (#2663).
    d.traceColour = telemetry->traceColourIndex();
    Config::getInstance().setSpectrumDefaults(d);
    Config::getInstance().save();
}
/// The v1 dropdown, as on the sequencer faceplate (#3092).
void applyComboColours(juce::ComboBox& c) {
    c.setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    c.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    c.setColour(juce::ComboBox::textColourId,
                ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
    c.setColour(juce::ComboBox::arrowColourId, ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
}
}  // namespace

SpectrumAnalyzerUI::SpectrumAnalyzerUI() {
    auto styleLabel = [this](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setFont(FontManager::getInstance().getUIFont(10.0f));
        l.setColour(juce::Label::textColourId, ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
        l.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(l);
    };
    auto styleCombo = [this](juce::ComboBox& c) {
        c.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
        applyComboColours(c);
        addAndMakeVisible(c);
    };

    styleLabel(fftLabel_, "FFT");
    fftChoice_.setOptions({"2k", "4k", "8k"});
    fftChoice_.onChange = [this](int index) {
        const int order = 11 + index;
        if (telemetry_ != nullptr)
            telemetry_->setFftOrder(order);
        rebuildFft(order);
        commitSettings();
        persistSpectrumDefaults(telemetry_, persistGlobalDefaults_);
    };
    addAndMakeVisible(fftChoice_);

    styleLabel(slopeLabel_, "Slope");
    slopeChoice_.setOptions({"0", "3", "4.5", "6"});
    slopeChoice_.onChange = [this](int index) {
        slopeDbPerOct_ = kSlopeOptions[static_cast<size_t>(index)];
        if (telemetry_ != nullptr)
            telemetry_->setSlopeDbPerOct(slopeDbPerOct_);
        commitSettings();
        persistSpectrumDefaults(telemetry_, persistGlobalDefaults_);
    };
    addAndMakeVisible(slopeChoice_);

    styleLabel(speedLabel_, "Time");
    speedChoice_.setOptions({"Slow", "Med", "Fast"});
    speedChoice_.onChange = [this](int index) {
        smoothing_ = kSpeedOptions[static_cast<size_t>(index)];
        if (telemetry_ != nullptr)
            telemetry_->setSmoothing(smoothing_);
        commitSettings();
        persistSpectrumDefaults(telemetry_, persistGlobalDefaults_);
    };
    addAndMakeVisible(speedChoice_);

    styleLabel(colourLabel_, "Color");
    colourDots_.onChange = [this](int index) {
        if (telemetry_ != nullptr)
            telemetry_->setTraceColourIndex(index);
        commitSettings();
        persistSpectrumDefaults(telemetry_, persistGlobalDefaults_);
    };
    addAndMakeVisible(colourDots_);

    // Inter-track masking overlay (#1400): pick another track to overlay and
    // shade the clashing frequency zones. Top row, full-editor / pop-out only.
    overlayLabel_.setText("Overlay", juce::dontSendNotification);
    overlayLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    overlayLabel_.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    overlayLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(overlayLabel_);
    overlayCombo_.addItem("No overlay", 1);
    overlayCombo_.setSelectedId(1, juce::dontSendNotification);
    overlayCombo_.onChange = [this] {
        const int id = overlayCombo_.getSelectedId();
        magda::TrackId target = magda::INVALID_TRACK_ID;
        const int idx = id - 2;  // item ids: 1 = Off, track entries start at 2
        if (idx >= 0 && idx < static_cast<int>(overlayItems_.size()))
            target = overlayItems_[static_cast<size_t>(idx)];
        selectOverlayTrack(target);
    };
    styleCombo(overlayCombo_);
    overlayCombo_.setTooltip("Overlay another track's spectrum");

    popoutButton_ = std::make_unique<magda::SvgButton>("Pop out", BinaryData::open_in_new_svg,
                                                       BinaryData::open_in_new_svgSize);
    daw::ui::node_header::applyHeaderIconStyle(*popoutButton_,
                                               ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
    popoutButton_->onClick = [this] { openPopout(); };
    addChildComponent(*popoutButton_);  // shown only in compact mode

    settingsButton_ = std::make_unique<magda::SvgButton>("Settings", BinaryData::settings_nobg_svg,
                                                         BinaryData::settings_nobg_svgSize);
    settingsButton_->setTooltip("Show the analyzer's settings");
    settingsButton_->onClick = [this] { setControlsExpanded(!controlsExpanded_); };
    settingsButton_->setClickingTogglesState(true);
    addChildComponent(*settingsButton_);  // only a host's header shows it

    rebuildFft(11);
    updateTimerState();
}

SpectrumAnalyzerUI::~SpectrumAnalyzerUI() {
    stopTimer();
    releaseMeasurementArming();  // undo any Mix Analysis we switched on for the overlay
    // Clear the shared LookAndFeel before the combo is destroyed.
    overlayCombo_.setLookAndFeel(nullptr);
}

void SpectrumAnalyzerUI::lookAndFeelChanged() {
    // Re-apply cached theme colours after a live theme switch.
    for (auto* label : {&fftLabel_, &slopeLabel_, &speedLabel_, &colourLabel_, &overlayLabel_})
        label->setColour(juce::Label::textColourId,
                         ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));

    applyComboColours(overlayCombo_);

    if (popoutButton_)
        daw::ui::node_header::applyHeaderIconStyle(
            *popoutButton_, ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));

    repaint();
}

void SpectrumAnalyzerUI::rebuildFft(int order) {
    spectrum_.prepare(order);
    overlaySpectrum_.prepare(order);
    const auto numBins = static_cast<size_t>(spectrum_.numBins());
    readBuf_.assign(static_cast<size_t>(kTapReadSamples), 0.0f);
    smoothedDb_.assign(numBins, kMinDb);
    peakDb_.assign(numBins, kMinDb);
    overlaySmoothedDb_.assign(numBins, kMinDb);
}

double SpectrumAnalyzerUI::binHz() const {
    const double sr = (telemetry_ != nullptr) ? telemetry_->sampleRate() : 44100.0;
    return sr / static_cast<double>(spectrum_.fftSize());
}

bool SpectrumAnalyzerUI::analyseLatest(audio::OverlappedSpectrum& spectrum,
                                       const std::function<std::size_t(float*, int)>& readLatest,
                                       int capacity) {
    const auto written = readLatest(readBuf_.data(), capacity);
    const int fresh = spectrum.samplesToRead(written, capacity);
    if (fresh == 0)
        return false;
    spectrum.analyse(readBuf_.data() + (capacity - fresh), fresh, written, frameDb_);
    return true;
}

void SpectrumAnalyzerUI::foldFrame(double sampleRate, std::vector<float>& smoothed,
                                   std::vector<float>* peak) const {
    const auto hz = static_cast<float>(sampleRate / static_cast<double>(spectrum_.fftSize()));
    for (size_t i = 0; i < frameDb_.size(); ++i) {
        float db = frameDb_[i];
        // Display slope (tilt) pivoted at 1 kHz so music reads roughly flat.
        if (i > 0)
            db += slopeDbPerOct_ * std::log2(static_cast<float>(i) * hz / 1000.0f);
        db = juce::jlimit(kMinDb, kMaxDb, db);

        float& sm = smoothed[i];
        sm += smoothing_ * (db - sm);
        if (peak != nullptr) {
            float& pk = (*peak)[i];
            pk = sm > pk ? sm : juce::jmax(kMinDb, pk - kPeakDecayDb);
        }
    }
}

juce::Path SpectrumAnalyzerUI::columnPath(const std::vector<float>& binDb,
                                          juce::Rectangle<float> area) {
    columnDb_.resize(static_cast<size_t>(juce::jmax(1, static_cast<int>(area.getWidth()))));
    audio::spectrumColumns(binDb, binHz(), kMinHz, kMaxHz, columnDb_);
    juce::Path p;
    for (size_t c = 0; c < columnDb_.size(); ++c) {
        const float x = area.getX() + static_cast<float>(c) + 0.5f;
        const float y = dbToY(columnDb_[c], area);
        if (c == 0)
            p.startNewSubPath(x, y);
        else
            p.lineTo(x, y);
    }
    return p;
}

void SpectrumAnalyzerUI::setTelemetrySource(std::shared_ptr<SpectrumTelemetrySource> telemetry) {
    if (telemetry_ == telemetry)
        return;

    telemetry_ = std::move(telemetry);
    spectrum_.reset();
    if (popoutUI_ != nullptr)
        popoutUI_->setTelemetrySource(telemetry_);  // keep the popped-out window live
    refreshSettingsFromSource();
}

void SpectrumAnalyzerUI::refreshSettingsFromSource() {
    if (telemetry_ == nullptr)
        return;

    slopeDbPerOct_ = telemetry_->slopeDbPerOct();
    smoothing_ = telemetry_->smoothing();
    fftChoice_.setSelectedIndex(juce::jlimit(0, 2, telemetry_->fftOrder() - 11));
    slopeChoice_.setSelectedIndex(nearestIndex(kSlopeOptions, slopeDbPerOct_));
    speedChoice_.setSelectedIndex(nearestIndex(kSpeedOptions, smoothing_));
    colourDots_.setSelectedIndex(telemetry_->traceColourIndex());
    rebuildFft(telemetry_->fftOrder());
    repaint();
}

/// Every setting each time: the document is patched rather than replaced, and
/// which control moved is not worth tracking.
void SpectrumAnalyzerUI::commitSettings() {
    if (!onSettingsEdited)
        return;

    juce::NamedValueSet settings;
    settings.set("fftOrder", 11 + fftChoice_.getSelectedIndex());
    settings.set("slopeDbPerOct", slopeDbPerOct_);
    settings.set("smoothing", smoothing_);
    settings.set("traceColour", colourDots_.getSelectedIndex());
    onSettingsEdited(settings);
}

void SpectrumAnalyzerUI::setTrackId(magda::TrackId trackId) {
    trackId_ = trackId;
    if (popoutUI_ != nullptr)
        popoutUI_->setTrackId(trackId);
    refreshOverlayList();
}

void SpectrumAnalyzerUI::refreshOverlayList() {
    auto& tm = magda::TrackManager::getInstance();
    std::vector<magda::TrackId> items;
    juce::String sig;
    for (const auto& t : tm.getTracks()) {
        if (t.id == magda::INVALID_TRACK_ID || t.id == trackId_)
            continue;
        items.push_back(t.id);
        sig << t.id << ":" << t.name << ";";
    }
    if (sig == overlayListSig_)
        return;  // nothing changed; leave the (possibly open) combo untouched
    overlayListSig_ = sig;
    overlayItems_ = items;

    overlayCombo_.clear(juce::dontSendNotification);
    overlayCombo_.addItem("No overlay", 1);
    int selId = 1;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const auto* info = tm.getTrack(items[static_cast<size_t>(i)]);
        overlayCombo_.addItem(info != nullptr ? info->name : juce::String("Track"), i + 2);
        if (items[static_cast<size_t>(i)] == overlayTrackId_)
            selId = i + 2;
    }
    overlayCombo_.setSelectedId(selId, juce::dontSendNotification);
    // The overlaid track was deleted: drop the overlay and release its arming.
    if (selId == 1 && overlayTrackId_ != magda::INVALID_TRACK_ID)
        selectOverlayTrack(magda::INVALID_TRACK_ID);
}

void SpectrumAnalyzerUI::selectOverlayTrack(magda::TrackId target) {
    if (target == overlayTrackId_)
        return;
    releaseMeasurementArming();
    overlayTrackId_ = target;
    overlayValid_ = false;
    overlaySpectrum_.reset();
    maskingFindings_.clear();

    if (overlayTrackId_ != magda::INVALID_TRACK_ID && trackId_ != magda::INVALID_TRACK_ID) {
        // Auto-arm Mix Analysis the moment a track is overlaid. Remember what we
        // switch on so releasing only undoes our own additions, not state another
        // consumer (the Levels meter, the mixing-agent capture) still wants.
        auto& tmm = magda::TrackMeasurementManager::getInstance();
        if (!tmm.isGlobalEnabled()) {
            tmm.setGlobalEnabled(true);  // must precede setTrackEnabled so taps get inserted
            armedGlobal_ = true;
        }
        for (magda::TrackId id : {trackId_, overlayTrackId_}) {
            if (!tmm.isTrackEnabled(id)) {
                tmm.setTrackEnabled(id, true);
                armedTracks_.push_back(id);
            }
        }
        if (!tmm.isMaskingAnalysisEnabled()) {
            tmm.setMaskingAnalysisEnabled(true);
            armedMasking_ = true;
        }
    }
    repaint();
}

void SpectrumAnalyzerUI::releaseMeasurementArming() {
    auto& tmm = magda::TrackMeasurementManager::getInstance();
    for (magda::TrackId id : armedTracks_)
        tmm.setTrackEnabled(id, false);
    armedTracks_.clear();
    if (armedMasking_) {
        tmm.setMaskingAnalysisEnabled(false);
        armedMasking_ = false;
    }
    if (armedGlobal_) {
        tmm.setGlobalEnabled(false);
        armedGlobal_ = false;
    }
}

void SpectrumAnalyzerUI::pollOverlayData() {
    if (overlayTrackId_ == magda::INVALID_TRACK_ID || trackId_ == magda::INVALID_TRACK_ID) {
        overlayValid_ = false;
        maskingFindings_.clear();
        return;
    }
    auto& tmm = magda::TrackMeasurementManager::getInstance();

    // Clash zones come from the band-based masking detector, filtered to this pair.
    maskingFindings_.clear();
    for (const auto& f : tmm.getMaskingFindings())
        if ((f.trackA == trackId_ && f.trackB == overlayTrackId_) ||
            (f.trackA == overlayTrackId_ && f.trackB == trackId_))
            maskingFindings_.push_back(f);

    // The overlay trace runs this track's pipeline on the overlaid track's
    // captured samples, so the two are visually comparable (not the coarse band spectrum).
    double sr = 44100.0;
    const auto readOverlay = [this, &sr](float* dest, int numSamples) {
        return magda::TrackMeasurementManager::readTrackSpectrumSamples(overlayTrackId_, dest,
                                                                        numSamples, sr);
    };
    if (analyseLatest(overlaySpectrum_, readOverlay, kOverlayReadSamples)) {
        foldFrame(sr, overlaySmoothedDb_, nullptr);
        overlayValid_ = true;
    }
}

void SpectrumAnalyzerUI::visibilityChanged() {
    updateTimerState();
}

void SpectrumAnalyzerUI::parentHierarchyChanged() {
    updateTimerState();
}

void SpectrumAnalyzerUI::setCompact(bool compact) {
    if (compact_ == compact)
        return;
    compact_ = compact;
    if (!compact_) {
        controlsExpanded_ = false;  // the full editor always shows controls; no toggle
        controlsFadeActive_ = false;
        controlsAlpha_ = 1.0f;
    }
    updateControlVisibility();
    resized();
    repaint();
}

void SpectrumAnalyzerUI::setPersistGlobalDefaults(bool persist) {
    persistGlobalDefaults_ = persist;
    if (popoutUI_ != nullptr)
        popoutUI_->setPersistGlobalDefaults(persist);
}

void SpectrumAnalyzerUI::setControlsExpanded(bool expanded) {
    if (!controlsToggle() || controlsExpanded_ == expanded)
        return;
    controlsExpanded_ = expanded;
    if (settingsButton_)
        settingsButton_->setToggleState(expanded, juce::dontSendNotification);
    startControlsFade(expanded);
    updateControlVisibility();
    resized();
    repaint();
    if (expanded && onControlsExpandedChanged)
        onControlsExpandedChanged();
}

int SpectrumAnalyzerUI::expandedControlsHeight() const {
    if (!compact_ || !showControls())
        return 0;
    constexpr int fullHeight =
        4 * kStackRowH + 4;  // FFT + Slope + Time + Color rows, plus top padding
    // Reserve the full height for the whole fade (both directions) so the strip
    // relayouts once on open and once on close, not every frame. The controls
    // cross-fade their opacity over this stable area; animating the height each
    // frame (and relaying out the whole mixer with it) is what made the reveal
    // flicker.
    return (controlsExpanded_ || controlsFadeActive_) ? fullHeight : 0;
}

void SpectrumAnalyzerUI::updateControlVisibility() {
    const bool show = showControls();
    for (juce::Component* c : controlRowComponents())
        c->setVisible(show);
    if (popoutButton_)
        popoutButton_->setVisible(compact_);  // lives in the strip, compact only
    // The masking overlay picker is a full-editor / pop-out feature only; the
    // compact mixer visualizer has no room for it.
    overlayLabel_.setVisible(!compact_ && !overlayHosted());
    overlayCombo_.setVisible(!compact_);
    applyControlsAlpha();
}

void SpectrumAnalyzerUI::startControlsFade(bool expanding) {
    controlsFadeActive_ = true;
    controlsFadeStartMs_ = juce::Time::getMillisecondCounterHiRes();
    controlsFadeStartAlpha_ = expanding ? 0.0f : controlsAlpha_;
    controlsFadeTargetAlpha_ = expanding ? 1.0f : 0.0f;
    controlsAlpha_ = controlsFadeStartAlpha_;
}

void SpectrumAnalyzerUI::advanceControlsFade() {
    if (!controlsFadeActive_)
        return;

    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - controlsFadeStartMs_;
    const auto progress =
        static_cast<float>(juce::jlimit(0.0, 1.0, elapsed / kCompactControlsFadeMs));
    controlsAlpha_ =
        controlsFadeStartAlpha_ + (controlsFadeTargetAlpha_ - controlsFadeStartAlpha_) * progress;
    // Opacity-only during the fade — height is already reserved, so no per-frame
    // relayout (that was the flicker). The strip relayouts once on completion.
    applyControlsAlpha();
    repaint();

    if (progress < 1.0f)
        return;

    controlsFadeActive_ = false;
    controlsAlpha_ = controlsExpanded_ ? 1.0f : 0.0f;
    updateControlVisibility();
    if (onControlsExpandedChanged)
        onControlsExpandedChanged();
    resized();
    repaint();
}

void SpectrumAnalyzerUI::applyControlsAlpha() {
    const float alpha = controlsToggle() ? controlsAlpha_ : 1.0f;
    for (juce::Component* c : controlRowComponents())
        c->setAlpha(alpha);
}

int SpectrumAnalyzerUI::compactExtraHeight() const {
    return compact_ ? kChevronStripH + expandedControlsHeight() : 0;
}

void SpectrumAnalyzerUI::resized() {
    syncDocked();
    if (compact_) {
        auto area = getLocalBounds();
        const int controlsHeight = expandedControlsHeight();
        auto controls =
            controlsHeight > 0 ? area.removeFromBottom(controlsHeight) : juce::Rectangle<int>();
        // Dedicated chevron/pop-out strip directly below the plot.
        auto strip = area.removeFromBottom(kChevronStripH);
        chevronRect_ = juce::Rectangle<int>(strip.getCentreX() - 7, strip.getCentreY() - 7, 14, 14);
        popoutRect_ = juce::Rectangle<int>(strip.getRight() - 19, strip.getCentreY() - 7, 14, 14);
        if (popoutButton_)
            popoutButton_->setBounds(popoutRect_);
        if (controlsHeight > 0 && showControls()) {
            controls.removeFromTop(2);
            auto stackRow = [&controls](juce::Label& label, juce::Component& control) {
                auto row = controls.removeFromTop(kStackRowH);
                label.setBounds(row.removeFromLeft(kStackLabelW));
                control.setBounds(row.reduced(2, 1));
            };
            slopeLabel_.setText("Slope", juce::dontSendNotification);
            stackRow(fftLabel_, fftChoice_);
            stackRow(slopeLabel_, slopeChoice_);
            stackRow(speedLabel_, speedChoice_);
            stackRow(colourLabel_, colourDots_);
        }
        return;
    }

    chevronRect_ = juce::Rectangle<int>();
    popoutRect_ = juce::Rectangle<int>();

    // Docked, the row lies over the plot's foot while the gear shows it.
    if (overlayHosted())
        return layoutControlRow(
            plotArea().toNearestInt().reduced(4).removeFromBottom(kControlRowH));

    // Top row: masking overlay picker.
    auto overlayRow = getLocalBounds().removeFromTop(kOverlayRowH);
    overlayRow.removeFromLeft(4);
    overlayLabel_.setBounds(overlayRow.removeFromLeft(48));
    overlayCombo_.setBounds(overlayRow.removeFromLeft(150).reduced(2, 1));
    layoutControlRow(getLocalBounds().removeFromBottom(kControlRowH));
}

void SpectrumAnalyzerUI::layoutControlRow(juce::Rectangle<int> controls) {
    // Each control at the width its options need, its caption before it.
    slopeLabel_.setText("Slope dB/oct", juce::dontSendNotification);
    auto cell = [&controls](juce::Label& label, int labelW, juce::Component& control, int width) {
        controls.removeFromLeft(6);
        label.setBounds(controls.removeFromLeft(labelW));
        controls.removeFromLeft(4);
        control.setBounds(controls.removeFromLeft(width).reduced(0, 1));
    };
    cell(fftLabel_, 28, fftChoice_, fftChoice_.getPreferredWidth());
    cell(slopeLabel_, 64, slopeChoice_, slopeChoice_.getPreferredWidth());
    cell(speedLabel_, 30, speedChoice_, speedChoice_.getPreferredWidth());
    cell(colourLabel_, 34, colourDots_, colourDots_.getPreferredWidth());
}

std::vector<juce::Component*> SpectrumAnalyzerUI::controlRowComponents() {
    return {&fftLabel_,   &fftChoice_,   &slopeLabel_,  &slopeChoice_,
            &speedLabel_, &speedChoice_, &colourLabel_, &colourDots_};
}

std::vector<juce::Component*> SpectrumAnalyzerUI::headerControls() {
    return {&overlayCombo_, settingsButton_.get()};
}

void SpectrumAnalyzerUI::syncDocked() {
    // Reparenting the picker tells this component nothing, so each layout checks where it is.
    if (overlayHosted() == dockedApplied_)
        return;
    dockedApplied_ = overlayHosted();
    if (!dockedApplied_ && !compact_) {
        controlsExpanded_ = false;
        controlsFadeActive_ = false;
        controlsAlpha_ = 1.0f;
    }
    updateControlVisibility();
    repaint();
}

void SpectrumAnalyzerUI::mouseDown(const juce::MouseEvent& e) {
    if (compact_ && chevronRect_.contains(e.getPosition()))
        setControlsExpanded(!controlsExpanded_);  // pop-out is handled by popoutButton_
}

void SpectrumAnalyzerUI::openPopout() {
    // popoutButton_ is a toggle: its state after the click is the desired open
    // state, so the icon and window stay in sync (and the window X clears it).
    const bool wantOpen = (popoutButton_ == nullptr) || popoutButton_->getToggleState();

    if (popoutWindow_ == nullptr) {
        if (!wantOpen)
            return;
        auto content = std::make_unique<SpectrumAnalyzerUI>();  // full-size (not compact)
        popoutUI_ = content.get();
        popoutUI_->setPersistGlobalDefaults(persistGlobalDefaults_);
        popoutUI_->onSettingsEdited = onSettingsEdited;
        popoutUI_->setTelemetrySource(telemetry_);
        popoutUI_->setTrackId(trackId_);
        popoutWindow_ = std::make_unique<AnalyzerWindow>("Spectrum Analyzer", std::move(content));
        popoutWindow_->onClose = [this]() {
            if (popoutButton_)
                popoutButton_->setToggleState(false, juce::dontSendNotification);
        };
    } else {
        popoutWindow_->setVisible(wantOpen);
        if (wantOpen)
            popoutWindow_->toFront(true);
    }
}

void SpectrumAnalyzerUI::timerCallback() {
    const bool fadeWasActive = controlsFadeActive_;
    advanceControlsFade();
    bool needsRepaint = fadeWasActive || controlsFadeActive_;

    if (!isShowing())
        return;

    // Keep the overlay picker current with track adds/renames, and refresh the
    // overlaid track's spectrum + clash findings independently of this track's
    // own audio (the overlay can change while this track is silent).
    refreshOverlayList();
    if (overlayTrackId_ != magda::INVALID_TRACK_ID) {
        pollOverlayData();
        needsRepaint = true;
    }

    if (telemetry_ == nullptr) {
        if (needsRepaint)
            repaint();
        return;
    }

    const auto readTap = [this](float* dest, int numSamples) {
        return telemetry_->readLatest(dest, numSamples);
    };
    if (analyseLatest(spectrum_, readTap, kTapReadSamples)) {
        foldFrame(telemetry_->sampleRate(), smoothedDb_, &peakDb_);
        needsRepaint = true;
    }
    if (needsRepaint)
        repaint();
}

void SpectrumAnalyzerUI::updateTimerState() {
    if (isVisible() && getParentComponent() != nullptr)
        startTimerHz(kTimerHz);
    else
        stopTimer();
}

float SpectrumAnalyzerUI::freqToX(float hz, juce::Rectangle<float> area) {
    const float t = std::log(juce::jmax(kMinHz, hz) / kMinHz) / std::log(kMaxHz / kMinHz);
    return area.getX() + juce::jlimit(0.0f, 1.0f, t) * area.getWidth();
}

float SpectrumAnalyzerUI::dbToY(float db, juce::Rectangle<float> area) {
    const float t = (db - kMinDb) / (kMaxDb - kMinDb);
    return area.getBottom() - juce::jlimit(0.0f, 1.0f, t) * area.getHeight();
}

juce::Rectangle<float> SpectrumAnalyzerUI::plotArea() const {
    auto a = getLocalBounds();
    if (!compact_) {
        if (!overlayHosted()) {
            a.removeFromTop(kOverlayRowH);  // overlay picker row
            a.removeFromBottom(kControlRowH);
        }
    } else {
        a.removeFromBottom(expandedControlsHeight());
        a.removeFromBottom(kChevronStripH);  // chevron/pop-out strip
    }
    return a.toFloat().reduced(4.0f);
}

void SpectrumAnalyzerUI::paint(juce::Graphics& g) {
    const auto plot = plotArea();

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillRoundedRectangle(plot, 4.0f);

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_GRID));
    for (float f : {100.0f, 1000.0f, 10000.0f})
        g.drawVerticalLine(static_cast<int>(freqToX(f, plot)), plot.getY(), plot.getBottom());

    // dB scale on the left axis.
    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    for (float db = kMaxDb; db >= kMinDb; db -= 20.0f) {
        const float y = dbToY(db, plot);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_GRID));
        g.drawHorizontalLine(static_cast<int>(y), plot.getX(), plot.getRight());
        if (!compact_) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
            const float ly = (db >= kMaxDb - 0.01f) ? y + 1.0f : y - 11.0f;
            g.drawText(juce::String(static_cast<int>(db)),
                       juce::Rectangle<float>(plot.getX() + 2.0f, ly, 30.0f, 11.0f),
                       juce::Justification::topLeft);
        }
    }

    // Frequency axis labels along the bottom of the plot.
    if (!compact_) {
        g.setFont(FontManager::getInstance().getUIFont(9.0f));
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
        auto freqLabel = [&](float f, const juce::String& s) {
            const float x = freqToX(f, plot);
            g.drawText(s, juce::Rectangle<float>(x - 18.0f, plot.getBottom() - 13.0f, 36.0f, 12.0f),
                       juce::Justification::centred);
        };
        freqLabel(100.0f, "100");
        freqLabel(1000.0f, "1k");
        freqLabel(10000.0f, "10k");
    }

    // Inter-track masking overlay (#1400): the picked track's spectrum drawn
    // over this one, with the clashing frequency zones shaded. Behind this
    // track's own trace so the subject stays primary. Drawn even when this track
    // is silent (smoothedDb_ empty), since the overlay updates independently.
    if (overlayTrackId_ != magda::INVALID_TRACK_ID) {
        // Clash zones first, so the spectrum traces sit on top of the shading.
        // Kept faint (a tint, not a block) so it marks the region without burying
        // the traces; severity nudges the opacity within a narrow range.
        const auto clash = ActiveTheme::getColour(ActiveTheme::MIDI_LEARN);
        for (const auto& f : maskingFindings_) {
            const float x0 = freqToX(f.loHz, plot);
            const float x1 = freqToX(f.hiHz, plot);
            const float alpha = juce::jlimit(0.05f, 0.16f, 0.05f + f.severity * 0.12f);
            g.setColour(clash.withAlpha(alpha));
            g.fillRect(juce::Rectangle<float>(x0, plot.getY(), juce::jmax(1.0f, x1 - x0),
                                              plot.getHeight()));
        }

        // The overlaid track's spectrum, computed by the same FFT pipeline as
        // this track's trace (see pollOverlayData), so it matches in resolution
        // and smoothing. Drawn in a neutral colour, secondary to this track.
        if (overlayValid_ && !overlaySmoothedDb_.empty()) {
            const auto op = columnPath(overlaySmoothedDb_, plot);
            g.setColour(ActiveTheme::getColour(ActiveTheme::SPECTRUM_OVERLAY).withAlpha(0.7f));
            g.strokePath(op, juce::PathStrokeType(1.5f));
        }
    }

    auto drawChevron = [&] {
        if (!compact_)
            return;
        // Chevron/pop-out strip directly below the plot.
        auto strip = getLocalBounds();
        strip.removeFromBottom(expandedControlsHeight());
        strip = strip.removeFromBottom(kChevronStripH);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_PANEL));
        g.fillRect(strip);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
        g.drawHorizontalLine(strip.getY(), static_cast<float>(strip.getX()),
                             static_cast<float>(strip.getRight()));
        // Chevron points down to open (controls below) and up to collapse.
        drawAnalyzerExpandChevron(g, chevronRect_, controlsExpanded_,
                                  ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
        // Pop-out is the SvgButton (open_in_new) positioned in the strip.
    };

    if (smoothedDb_.empty()) {
        drawChevron();
        return;
    }

    const juce::Colour trace =
        analyzerTraceColour(telemetry_ != nullptr ? telemetry_->traceColourIndex() : 0);
    g.setColour(trace.withAlpha(0.4f));  // peak-hold
    g.strokePath(columnPath(peakDb_, plot), juce::PathStrokeType(1.0f));
    g.setColour(trace);  // live spectrum, last so columnDb_ holds it for the readout
    g.strokePath(columnPath(smoothedDb_, plot), juce::PathStrokeType(1.5f));

    // Hover readout: a vertical cursor plus the frequency and spectrum level at it.
    if (mouseOver_) {
        const float mx =
            juce::jlimit(plot.getX(), plot.getRight(), static_cast<float>(mousePos_.x));
        const float t = (mx - plot.getX()) / plot.getWidth();
        const float freq = kMinHz * std::pow(kMaxHz / kMinHz, juce::jlimit(0.0f, 1.0f, t));
        const auto column = static_cast<size_t>(juce::jlimit(
            0, static_cast<int>(columnDb_.size()) - 1, static_cast<int>(mx - plot.getX())));
        const float db = columnDb_[column];

        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM).withAlpha(0.6f));
        g.drawVerticalLine(static_cast<int>(mx), plot.getY(), plot.getBottom());
        g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_INFO));
        g.fillEllipse(mx - 2.5f, dbToY(db, plot) - 2.5f, 5.0f, 5.0f);

        const juce::String fTxt =
            freq >= 1000.0f ? juce::String(freq / 1000.0f, freq >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String(juce::roundToInt(freq)) + " Hz";
        const juce::String txt =
            fTxt + "   " + audio::nearestNoteLabel(freq) + "   " + juce::String(db, 1) + " dB";
        auto box = plot.reduced(6.0f, 4.0f).removeFromTop(14.0f).withWidth(210.0f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG).withAlpha(0.75f));
        g.fillRect(box);
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
        g.drawText(txt, box, juce::Justification::centredLeft);
    }

    if (overlayHosted() && showControls()) {
        // A backdrop so the docked control row reads over the spectrum.
        const auto row = plot.reduced(2.0f).removeFromBottom(kControlRowH + 6.0f);
        g.setColour(
            ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG).withAlpha(0.9f * controlsAlpha_));
        g.fillRoundedRectangle(row, 4.0f);
    }

    drawChevron();
}

void SpectrumAnalyzerUI::mouseMove(const juce::MouseEvent& e) {
    mousePos_ = e.getPosition();
    mouseOver_ = plotArea().contains(mousePos_.toFloat());
    repaint();
}

void SpectrumAnalyzerUI::mouseExit(const juce::MouseEvent&) {
    mouseOver_ = false;
    repaint();
}

}  // namespace magda::daw::ui
