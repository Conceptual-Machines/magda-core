#include "compiled/ArpeggiatorTimeBendPanel.hpp"

#include "core/DeviceStateCommands.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kRowHeight = 24;
constexpr int kGap = 8;
constexpr int kTileHeight = 52;  ///< The grid's text tile, so the settings read as its cells.

void styleField(magda::DraggableValueLabel& field) {
    field.setFont(FontManager::getInstance().getUIFontMedium(16.0f));
    field.setTextColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
    field.setJustification(juce::Justification::centredLeft);
    field.setShowFillIndicator(false);
    field.setDrawBackground(false);
    field.setDrawBorder(false);
}

}  // namespace

ArpeggiatorTimeBendPanel::ArpeggiatorTimeBendPanel() {
    bend_.setShowsAmountAndCycles(false);
    bend_.onCommit = [this](const auto&, const auto&) { settingsEdited(); };
    addAndMakeVisible(bend_);

    quantize_.setRange(0.0, 1.0, 0.0);
    quantize_.setTooltip("Pull bent notes back toward the subdivision grid");
    subdivision_.setRange(16.0, 512.0, 16.0);
    subdivision_.setTooltip("Divisions of the bend's cycle the quantize amount pulls toward");
    for (auto* field : {&quantize_, &subdivision_}) {
        styleField(*field);
        field->onValueChange = [this] { settingsEdited(); };
        addAndMakeVisible(*field);
    }
}

ArpeggiatorTimeBendPanel::~ArpeggiatorTimeBendPanel() {
    stopTimer();
}

void ArpeggiatorTimeBendPanel::updateFromDevice(const magda::DeviceInfo&) {
    syncSettingsFromDevice();
}

void ArpeggiatorTimeBendPanel::updateFromDevice(const magda::DeviceInfo& device,
                                                const ParamLinkContext* linkContext) {
    if (linkContext != nullptr)
        devicePath_ = linkContext->devicePath;
    updateFromDevice(device);
}

void ArpeggiatorTimeBendPanel::bindDevice(std::shared_ptr<magda::daw::audio::MagdaDevice> device) {
    stopTimer();
    arp_ = std::dynamic_pointer_cast<Arp>(std::move(device));
    if (arp_ == nullptr)
        return;
    syncSettingsFromDevice();
    startTimerHz(30);
}

void ArpeggiatorTimeBendPanel::syncSettingsFromDevice() {
    if (arp_ == nullptr)
        return;
    auto bend = arp_->timeBend.current();
    bend.amount = arp_->displayedBendAmount_.load(std::memory_order_relaxed);
    bend_.setBend(bend);
    quantize_.setValue(arp_->quantize.load(std::memory_order_relaxed), juce::dontSendNotification);
    subdivision_.setValue(arp_->quantizeSub.load(std::memory_order_relaxed),
                          juce::dontSendNotification);
}

void ArpeggiatorTimeBendPanel::settingsEdited() {
    // The provider resolves the path afresh; a chain rebuild can move the device.
    if (linkContextProvider_)
        if (const auto context = linkContextProvider_())
            devicePath_ = context->devicePath;
    if (!devicePath_.isValid())
        return;

    using IDs = Arp::SettingIDs;
    juce::NamedValueSet settings;
    settings.set(magda::daw::audio::DeviceTimeBend::settingId,
                 juce::String(sdk::sequencer::timeBendToText(bend_.getBend())));
    settings.set(IDs::quantize, static_cast<float>(quantize_.getValue()));
    settings.set(IDs::quantizeSub, juce::roundToInt(subdivision_.getValue()));
    magda::writeDeviceSettings(devicePath_, settings);
}

void ArpeggiatorTimeBendPanel::timerCallback() {
    // The modulated amount, the cycle the walk is in and the steps it holds, from process().
    auto bend = bend_.getBend();
    const auto amount = arp_->displayedBendAmount_.load(std::memory_order_relaxed);
    if (bend.amount != amount) {
        bend.amount = amount;
        bend_.setBend(bend);
    }
    const bool playing = arp_->currentSeqLength_.load(std::memory_order_relaxed) > 0 &&
                         arp_->currentPlayStep_.load(std::memory_order_relaxed) >= 0;
    bend_.setPlayheadPhase(playing ? arp_->displayedBendPhase_.load(std::memory_order_relaxed)
                                   : -1.0f);
    bend_.setCycle(arp_->displayedCycleBeats_.load(std::memory_order_relaxed),
                   arp_->displayedBeatsPerBar_.load(std::memory_order_relaxed));
    bend_.setDefaultDivision(arp_->displayedStepBeats_.load(std::memory_order_relaxed));
}

void ArpeggiatorTimeBendPanel::paint(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    g.setFont(fonts.getMonoFont(10.0f).withExtraKerningFactor(0.12f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    g.drawText("TIME BEND", header_, juce::Justification::centredLeft, false);

    // The settings as tiles: the grid cell's fill, border and caption.
    g.setFont(fonts.getUIFont(12.5f));
    for (const auto& [tile, caption] :
         {std::pair{quantizeTile_, "Quantize"}, std::pair{subdivisionTile_, "Subdivision"}}) {
        const auto bounds = tile.toFloat();
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
        g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
        g.drawText(caption, tile.reduced(12, 7).removeFromTop(14), juce::Justification::centredLeft,
                   true);
    }
}

void ArpeggiatorTimeBendPanel::resized() {
    auto area = getLocalBounds();
    header_ = area.removeFromTop(kRowHeight);
    area.removeFromTop(4);

    auto tiles = area.removeFromBottom(kTileHeight);
    area.removeFromBottom(kGap);
    bend_.setBounds(area);

    const int width = (tiles.getWidth() - kGap) / 2;
    const auto place = [&](juce::Rectangle<int>& tile, juce::Component& field) {
        tile = tiles.removeFromLeft(width);
        tiles.removeFromLeft(kGap);
        field.setBounds(tile.reduced(8, 6).withTrimmedTop(16));
    };
    place(quantizeTile_, quantize_);
    place(subdivisionTile_, subdivision_);
}

const CompiledPresentationSpec& getArpeggiatorPresentation() {
    using Arp = magda::daw::audio::ArpeggiatorPlugin;
    // Pattern and rate head the faceplate; the rest stand in two columns beside it.
    // Paired rows: octaves / latch, gate / swing, bend amount / cycles, then fixed velocity.
    static constexpr int kKnobSlots[] = {
        Arp::kPattern, Arp::kRate,  Arp::kVelMode,    Arp::kOctaves,    Arp::kLatch,
        Arp::kGate,    Arp::kSwing, Arp::kBendAmount, Arp::kBendCycles, Arp::kFixedVel};
    static constexpr std::pair<int, const char*> kKnobLabels[] = {{Arp::kVelMode, "Vel mode"},
                                                                  {Arp::kFixedVel, "Fixed vel"},
                                                                  {Arp::kBendAmount, "Bend"},
                                                                  {Arp::kBendCycles, "Cycles"}};
    static const CompiledPresentationSpec kSpec{
        .pluginId = Arp::xmlTypeName,
        .layoutCellCount = Arp::kNumParams,
        .layoutCellsPerRow = Arp::kNumParams,
        .createPanel = [](juce::String) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<ArpeggiatorTimeBendPanel>();
        },
        // Fixed velocity only means something in Fixed mode.
        .isParameterEnabled =
            [](const magda::DeviceInfo& device, int slot) {
                if (slot != Arp::kFixedVel)
                    return true;
                const auto* mode = device.findParameterByIndex(Arp::kVelMode);
                return mode != nullptr && juce::roundToInt(mode->currentValue) ==
                                              static_cast<int>(Arp::VelocityMode::Fixed);
            },
        .knobSlots = kKnobSlots,
        .knobLabels = kKnobLabels,
        .faceplateWidth = 420,
    };
    return kSpec;
}

}  // namespace magda::daw::ui
