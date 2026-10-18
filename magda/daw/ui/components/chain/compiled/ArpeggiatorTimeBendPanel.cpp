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
    curve_.setTooltip("Drag the handle to shape note timing within each arpeggio cycle. "
                      "Double-click to reset.");
    curve_.setMouseCursor(juce::MouseCursor::CrosshairCursor);
    curve_.onCurveChanged = [this](float depth, float skew) {
        writeParameter(Arp::kRamp, depth);
        writeParameter(Arp::kSkew, skew);
    };
    curve_.onHardAngleChanged = [this](bool hardAngle) {
        hardAngle_ = hardAngle;
        settingsEdited();
    };
    addAndMakeVisible(curve_);

    cycles_.setRange(1.0, 8.0, 1.0);
    cycles_.setTooltip("Arpeggio cycles the time bend spans");
    quantize_.setRange(0.0, 1.0, 0.0);
    quantize_.setTooltip("Pull bent notes back toward the subdivision grid");
    subdivision_.setRange(16.0, 512.0, 16.0);
    subdivision_.setTooltip("Grid the quantize amount pulls toward");
    for (auto* field : {&cycles_, &quantize_, &subdivision_}) {
        styleField(*field);
        field->onValueChange = [this] { settingsEdited(); };
        addAndMakeVisible(*field);
    }
}

ArpeggiatorTimeBendPanel::~ArpeggiatorTimeBendPanel() {
    stopTimer();
}

void ArpeggiatorTimeBendPanel::updateFromDevice(const magda::DeviceInfo& device) {
    const auto value = [&device](int slot, float fallback) {
        const auto* param = device.findParameterByIndex(slot);
        return param != nullptr ? param->currentValue : fallback;
    };
    if (arp_ == nullptr)
        curve_.setValues(value(Arp::kRamp, 0.0f), value(Arp::kSkew, 0.0f));
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

void ArpeggiatorTimeBendPanel::writeParameter(int slot, float value) {
    if (onParameterChanged_)
        onParameterChanged_(slot, value);
}

void ArpeggiatorTimeBendPanel::syncSettingsFromDevice() {
    if (arp_ == nullptr)
        return;
    cycles_.setValue(arp_->rampCycles.load(std::memory_order_relaxed), juce::dontSendNotification);
    quantize_.setValue(arp_->quantize.load(std::memory_order_relaxed), juce::dontSendNotification);
    subdivision_.setValue(arp_->quantizeSub.load(std::memory_order_relaxed),
                          juce::dontSendNotification);
    hardAngle_ = arp_->hardAngle.load(std::memory_order_relaxed);
    curve_.setHardAngle(hardAngle_);
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
    settings.set(IDs::rampCycles, juce::roundToInt(cycles_.getValue()));
    settings.set(IDs::quantize, static_cast<float>(quantize_.getValue()));
    settings.set(IDs::quantizeSub, juce::roundToInt(subdivision_.getValue()));
    settings.set(IDs::hardAngle, hardAngle_);
    magda::writeDeviceSettings(devicePath_, settings);
}

void ArpeggiatorTimeBendPanel::timerCallback() {
    // Modulated values and the play step, from the atomics process() publishes each block.
    curve_.setValues(arp_->displayedRamp_.load(std::memory_order_relaxed),
                     arp_->displayedSkew_.load(std::memory_order_relaxed));
    const int step = arp_->currentPlayStep_.load(std::memory_order_relaxed);
    const int length = arp_->currentSeqLength_.load(std::memory_order_relaxed);
    if (length > 0)
        curve_.setNumTicks(length);
    const int cycles =
        juce::jlimit(1, std::max(1, length), arp_->rampCycles.load(std::memory_order_relaxed));
    curve_.setPlaybackPosition(
        step >= 0 && length > 0 ? static_cast<float>(step) / static_cast<float>(length) : -1.0f,
        cycles);
}

void ArpeggiatorTimeBendPanel::paint(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    g.setFont(fonts.getMonoFont(10.0f).withExtraKerningFactor(0.12f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    g.drawText("TIME BEND", header_, juce::Justification::centredLeft, false);

    // The settings as tiles: the grid cell's fill, border and caption.
    g.setFont(fonts.getUIFont(12.5f));
    for (const auto& [tile, caption] :
         {std::pair{cyclesTile_, "Cycles"}, std::pair{quantizeTile_, "Quantize"},
          std::pair{subdivisionTile_, "Subdivision"}}) {
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
    curve_.setBounds(area);

    const int width = (tiles.getWidth() - 2 * kGap) / 3;
    const auto place = [&](juce::Rectangle<int>& tile, juce::Component& field) {
        tile = tiles.removeFromLeft(width);
        tiles.removeFromLeft(kGap);
        field.setBounds(tile.reduced(8, 6).withTrimmedTop(16));
    };
    place(cyclesTile_, cycles_);
    place(quantizeTile_, quantize_);
    place(subdivisionTile_, subdivision_);
}

const CompiledPresentationSpec& getArpeggiatorPresentation() {
    using Arp = magda::daw::audio::ArpeggiatorPlugin;
    // Pattern and rate head the faceplate; the rest stand in two columns beside it.
    // Paired rows: octaves / latch, gate / swing, depth / skew, then fixed velocity.
    static constexpr int kKnobSlots[] = {Arp::kPattern, Arp::kRate,    Arp::kVelMode, Arp::kOctaves,
                                         Arp::kLatch,   Arp::kGate,    Arp::kSwing,   Arp::kRamp,
                                         Arp::kSkew,    Arp::kFixedVel};
    static constexpr std::pair<int, const char*> kKnobLabels[] = {{Arp::kVelMode, "Vel mode"},
                                                                  {Arp::kFixedVel, "Fixed vel"},
                                                                  {Arp::kRamp, "Depth"},
                                                                  {Arp::kSkew, "Skew"}};
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
