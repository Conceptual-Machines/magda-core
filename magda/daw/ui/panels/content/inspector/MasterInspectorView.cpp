#include "MasterInspectorView.hpp"

#include <algorithm>
#include <cmath>

#include "../../../components/common/MasterSpeakerButton.hpp"
#include "../../../components/mixer/LevelMeterScale.hpp"
#include "../../../themes/ActiveTheme.hpp"
#include "../../../themes/FontManager.hpp"
#include "audio/TrackMeters.hpp"
#include "audio/io/AudioIOControl.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackManager.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"
#include "engine/AudioEngine.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kPad = 12;
constexpr int kCaption = 18;
constexpr int kField = 28;
constexpr int kRowGap = 10;
constexpr int kContributorRow = 26;
constexpr int kMaxContributors = 5;

void caption(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(12.0f));
    label.setColour(juce::Label::textColourId, ActiveTheme::getSecondaryTextColour());
    label.setBorderSize({});
    label.setInterceptsMouseClicks(false, false);
}

juce::String formatDb(float db) {
    if (db <= magda::level_meter_scale::minDb + 0.05f)
        return juce::String::fromUTF8("\xe2\x80\x93inf");
    return juce::String(db, 1) + " dB";
}

}  // namespace

MasterInspectorView::MasterInspectorView() {
    setOpaque(true);

    nameLabel_.setText("Master", juce::dontSendNotification);
    nameLabel_.setFont(FontManager::getInstance().getUIFontMedium(16.0f));
    nameLabel_.setBorderSize({});
    addAndMakeVisible(nameLabel_);

    summaryLabel_.setFont(FontManager::getInstance().getMonoFont(11.0f));
    summaryLabel_.setBorderSize({});
    addAndMakeVisible(summaryLabel_);

    muteButton_ = magda::makeMasterSpeakerButton();
    muteButton_->setIconPadding(8.0f);
    muteButton_->setTooltip("Mute master");
    muteButton_->onClick = [this]() {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetMasterMuteCommand>(muteButton_->getToggleState()));
    };
    addAndMakeVisible(*muteButton_);

    for (auto* header : {&outputHeader_, &latencyHeader_}) {
        header->onCollapsedChanged = [this]() { layoutSections(); };
        addAndMakeVisible(*header);
    }

    caption(volumeCaption_, "Volume");
    addAndMakeVisible(volumeCaption_);
    volume_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Decibels);
    volume_->setRange(-60.0, 6.0, 0.0);
    volume_->setFillProportionMapper(magda::level_meter_scale::dbFillProportion);
    volume_->onValueChange = [this]() {
        const double db = volume_->getValue();
        const float gain = db <= -60.0 ? 0.0f : std::pow(10.0f, static_cast<float>(db) / 20.0f);
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetMasterVolumeCommand>(gain));
    };
    addAndMakeVisible(*volume_);

    meter_.setOrientation(magda::LevelMeter::Orientation::Horizontal);
    meter_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(meter_);

    peakLabel_.setFont(FontManager::getInstance().getMonoFont(11.0f));
    peakLabel_.setJustificationType(juce::Justification::centred);
    peakLabel_.setBorderSize({});
    peakLabel_.setTooltip("Peak hold; click to reset");
    peakLabel_.addMouseListener(this, false);
    addAndMakeVisible(peakLabel_);

    applyColours();
}

void MasterInspectorView::applyColours() {
    nameLabel_.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
    summaryLabel_.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    peakLabel_.setColour(juce::Label::textColourId, ActiveTheme::getSecondaryTextColour());
    for (auto* label : {&volumeCaption_})
        label->setColour(juce::Label::textColourId, ActiveTheme::getSecondaryTextColour());
}

void MasterInspectorView::lookAndFeelChanged() {
    applyColours();
    repaint();
}

void MasterInspectorView::mouseUp(const juce::MouseEvent& e) {
    if (e.eventComponent == &peakLabel_) {
        meter_.resetPeak();
        timerCallback();
    }
}

MasterInspectorView::~MasterInspectorView() {
    stopTimer();
    peakLabel_.removeMouseListener(this);
}

void MasterInspectorView::refresh() {
    auto& tm = magda::TrackManager::getInstance();
    const auto& master = tm.getMasterChannel();

    const float db = master.volume <= 0.0f ? -60.0f : 20.0f * std::log10(master.volume);
    volume_->setValue(db, juce::dontSendNotification);
    muteButton_->setToggleState(master.muted, juce::dontSendNotification);

    int devices = 0;
    if (const auto* track = tm.getTrack(magda::MASTER_TRACK_ID))
        devices = static_cast<int>(track->chain.fxChainElements.size());
    const auto dot = juce::String::fromUTF8(" \xc2\xb7 ");
    summaryLabel_.setText("Master" + dot + "stereo" + dot + juce::String(devices) +
                              (devices == 1 ? " device" : " devices"),
                          juce::dontSendNotification);

    refreshLatency();
    layoutSections();
    repaint();
}

void MasterInspectorView::refreshLatency() {
    auto& tm = magda::TrackManager::getInstance();
    totalMs_ = tm.getTrackLatencySeconds(magda::MASTER_TRACK_ID) * 1000.0;

    sampleRate_ = 0.0;
    if (audioEngine_ != nullptr)
        if (auto* io = audioEngine_->getAudioIO())
            sampleRate_ = io->status().sampleRate;
    totalSamples_ = juce::roundToInt(totalMs_ * 0.001 * sampleRate_);

    contributors_.clear();
    if (const auto* track = tm.getTrack(magda::MASTER_TRACK_ID)) {
        for (const auto& element : track->chain.fxChainElements) {
            if (!magda::isDevice(element))
                continue;
            const auto& device = magda::getDevice(element);
            const double ms = tm.getDeviceLatencySeconds(magda::ChainNodePath::topLevelDevice(
                                  magda::MASTER_TRACK_ID, device.id)) *
                              1000.0;
            if (ms > 0.0)
                contributors_.push_back({device.name, ms});
        }
    }
    std::ranges::sort(contributors_, [](const auto& a, const auto& b) { return a.ms > b.ms; });
    if (contributors_.size() > kMaxContributors)
        contributors_.resize(kMaxContributors);

    latencyHeader_.setCaption(contributors_.empty() ? juce::String() : "master chain");
}

void MasterInspectorView::visibilityChanged() {
    if (isVisible())
        startTimerHz(30);
    else
        stopTimer();
}

void MasterInspectorView::timerCallback() {
    if (audioEngine_ == nullptr || !isShowing())
        return;
    auto& meters = audioEngine_->meters();
    meter_.setLevels(meters.getMasterPeakL(), meters.getMasterPeakR());
    const auto peak = formatDb(meter_.getPeakDb());
    if (peakLabel_.getText() != "peak " + peak)
        peakLabel_.setText("peak " + peak, juce::dontSendNotification);
}

void MasterInspectorView::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getColour(ActiveTheme::BACKGROUND));

    // Neutral swatch: the master has no track colour.
    const auto swatch = juce::Rectangle<float>(static_cast<float>(headerArea_.getX()),
                                               static_cast<float>(headerArea_.getY() + 2), 4.0f,
                                               static_cast<float>(headerArea_.getHeight() - 4));
    g.setColour(ActiveTheme::getTextColour().withAlpha(0.85f));
    g.fillRoundedRectangle(swatch, 2.0f);

    g.setColour(ActiveTheme::getColour(ActiveTheme::SEPARATOR));
    for (const int y : separatorYs_)
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));

    const auto dim = ActiveTheme::getColour(ActiveTheme::TEXT_DIM);
    const auto mono = FontManager::getInstance().getMonoFont(11.0f);

    if (!meterScaleArea_.isEmpty()) {
        g.setFont(mono);
        g.setColour(dim);
        g.drawText(juce::String::fromUTF8("\xe2\x80\x93inf"), meterScaleArea_,
                   juce::Justification::centredLeft, false);
        g.drawText("+6", meterScaleArea_, juce::Justification::centredRight, false);
    }

    if (!latencyTotalArea_.isEmpty()) {
        auto row = latencyTotalArea_;
        juce::AttributedString total;
        total.append(juce::String(totalMs_, 1), FontManager::getInstance().getMonoFont(24.0f),
                     ActiveTheme::getTextColour());
        total.append(" ms total", FontManager::getInstance().getMonoFont(12.0f), dim);
        total.setJustification(juce::Justification::bottomLeft);
        total.draw(g, row.toFloat());

        if (sampleRate_ > 0.0) {
            juce::AttributedString samples;
            samples.append(juce::String(totalSamples_),
                           FontManager::getInstance().getMonoFont(15.0f),
                           ActiveTheme::getSecondaryTextColour());
            const auto khz = sampleRate_ / 1000.0;
            const auto rate = std::abs(khz - std::round(khz)) < 0.05
                                  ? juce::String(juce::roundToInt(khz))
                                  : juce::String(khz, 1);
            samples.append(" smp @ " + rate + "k", mono, dim);
            samples.setJustification(juce::Justification::bottomRight);
            samples.draw(g, row.toFloat().withTrimmedBottom(2.0f));
        }
    }

    if (!contributorsArea_.isEmpty()) {
        const auto box = contributorsArea_.toFloat();
        g.setColour(ActiveTheme::getBorderColour());
        g.drawRoundedRectangle(box.reduced(0.5f), 5.0f, 1.0f);

        if (contributors_.empty()) {
            g.setFont(FontManager::getInstance().getUIFont(12.0f));
            g.setColour(dim);
            g.drawText("No plugin latency on the master chain", contributorsArea_,
                       juce::Justification::centred, false);
            return;
        }

        const double maxMs = contributors_.front().ms;
        auto rows = contributorsArea_;
        for (size_t i = 0; i < contributors_.size(); ++i) {
            auto row = rows.removeFromTop(kContributorRow);
            if (i > 0) {
                g.setColour(ActiveTheme::getColour(ActiveTheme::SEPARATOR).withAlpha(0.6f));
                g.drawHorizontalLine(row.getY(), box.getX() + 1.0f, box.getRight() - 1.0f);
            }
            row.reduce(10, 0);
            const auto& c = contributors_[i];
            auto bar = row.removeFromRight(juce::jmin(110, row.getWidth() / 3))
                           .withSizeKeepingCentre(juce::jmin(110, row.getWidth() / 3), 4);
            g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE));
            g.fillRoundedRectangle(bar.toFloat(), 2.0f);
            g.setColour(i == 0 ? ActiveTheme::getColour(ActiveTheme::STATUS_WARNING)
                               : ActiveTheme::getSecondaryTextColour());
            g.fillRoundedRectangle(
                bar.toFloat().withWidth(static_cast<float>(bar.getWidth() * (c.ms / maxMs))), 2.0f);
            row.removeFromRight(10);
            g.setFont(mono);
            g.setColour(ActiveTheme::getTextColour());
            g.drawText(juce::String(c.ms, 1) + " ms", row.removeFromRight(64),
                       juce::Justification::centredRight, false);
            g.setFont(FontManager::getInstance().getUIFont(12.5f));
            g.drawText(c.name, row, juce::Justification::centredLeft, true);
        }
    }
}

void MasterInspectorView::paintOverChildren(juce::Graphics& g) {
    // 0 dB mark on the volume field.
    if (volume_->isVisible()) {
        const auto field = volume_->getBounds();
        const auto x =
            field.getX() +
            juce::roundToInt(field.getWidth() * magda::level_meter_scale::dbFillProportion(0.0));
        g.setColour(ActiveTheme::getTextColour().withAlpha(0.6f));
        g.drawVerticalLine(x, static_cast<float>(field.getY() + 3),
                           static_cast<float>(field.getBottom() - 3));
    }
}

void MasterInspectorView::resized() {
    layoutSections();
}

void MasterInspectorView::layoutSections() {
    separatorYs_.clear();
    meterScaleArea_ = latencyTotalArea_ = contributorsArea_ = {};
    auto bounds = getLocalBounds();

    // Header: swatch, name and summary, mute.
    auto header = bounds.removeFromTop(64).reduced(kPad, 12);
    headerArea_ = header;
    muteButton_->setBounds(header.removeFromRight(34).withSizeKeepingCentre(34, 30));
    header.removeFromLeft(14);
    nameLabel_.setBounds(header.removeFromTop(22));
    summaryLabel_.setBounds(header);
    separatorYs_.push_back(bounds.getY());

    // Output.
    auto output = bounds.reduced(kPad, 0);
    output.removeFromTop(6);
    outputHeader_.setBounds(output.removeFromTop(CollapsibleSectionHeader::kHeight));
    const bool outputOpen = !outputHeader_.isCollapsed();
    for (juce::Component* c : std::initializer_list<juce::Component*>{
             &volumeCaption_, volume_.get(), &meter_, &peakLabel_})
        c->setVisible(outputOpen);
    if (outputOpen) {
        output.removeFromTop(4);
        volumeCaption_.setBounds(output.removeFromTop(kCaption));
        volume_->setBounds(output.removeFromTop(kField));
        output.removeFromTop(kRowGap);
        meter_.setBounds(output.removeFromTop(kField));
        auto scale = output.removeFromTop(20);
        meterScaleArea_ = scale;
        peakLabel_.setBounds(
            scale.withSizeKeepingCentre(juce::jmin(160, scale.getWidth() / 2), scale.getHeight()));
        output.removeFromTop(kRowGap + 4);
    } else {
        output.removeFromTop(6);
    }
    separatorYs_.push_back(output.getY());

    // Latency.
    output.removeFromTop(6);
    latencyHeader_.setBounds(output.removeFromTop(CollapsibleSectionHeader::kHeight));
    if (!latencyHeader_.isCollapsed()) {
        output.removeFromTop(6);
        latencyTotalArea_ = output.removeFromTop(34);
        output.removeFromTop(kRowGap);
        const int rows = juce::jmax(1, static_cast<int>(contributors_.size()));
        contributorsArea_ = output.removeFromTop(rows * kContributorRow + 2);
    }
    repaint();
}

}  // namespace magda::daw::ui
