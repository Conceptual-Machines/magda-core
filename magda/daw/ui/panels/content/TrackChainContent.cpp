#include "TrackChainContent.hpp"

#include <BinaryData.h>

#include <cmath>
#include <thread>

#include "../../../../agents/gain_staging_agent.hpp"
#include "../../components/mixer/LevelMeter.hpp"
#include "../../debug/DebugSettings.hpp"
#include "../../dialogs/ChainTreeDialog.hpp"
#include "../../dialogs/GainStagingDialog.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/MixerMetrics.hpp"
#include "../../themes/SmallButtonLookAndFeel.hpp"
#include "PluginBrowserContent.hpp"
#include "core/AutomationInfo.hpp"
#include "core/DeviceInfo.hpp"
#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/PresetManager.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "ui/components/chain/DeviceSlotComponent.hpp"
#include "ui/components/chain/NodeComponent.hpp"
#include "ui/components/chain/RackComponent.hpp"
#include "ui/components/chain/modulation/MacroEditorPanel.hpp"
#include "ui/components/chain/modulation/MacroPanelComponent.hpp"
#include "ui/components/chain/modulation/ModsPanelComponent.hpp"
#include "ui/components/chain/modulation/ModulatorEditorPanel.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {
namespace {
void configureMasterSpeakerButton(SvgButton& button) {
    // Dual-icon (pre-baked colors): audible = gray speaker (master_on), muted =
    // orange chip (master_off). Toggle state drives which icon shows.
    button.setClickingTogglesState(true);
    button.setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    button.setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
}

void syncMasterSpeakerButton(SvgButton& button, bool muted) {
    button.setToggleState(muted, juce::dontSendNotification);
    button.setTooltip(muted ? "Unmute master" : "Mute master");
}
}  // namespace

namespace {
bool dragObjectToChainNodePath(const juce::DynamicObject& obj, magda::ChainNodePath& path) {
    path = {};
    const auto type = obj.getProperty("type").toString();
    if (type != "chainElement" && type != "chainElements")
        return false;

    path.trackId = static_cast<magda::TrackId>(static_cast<int>(obj.getProperty("trackId")));
    path.topLevelDeviceId =
        static_cast<magda::DeviceId>(static_cast<int>(obj.getProperty("topLevelDeviceId")));
    path.isTrackLevel = static_cast<bool>(obj.getProperty("isTrackLevel"));

    auto stepTypes =
        juce::StringArray::fromTokens(obj.getProperty("stepTypes").toString(), ",", "");
    auto stepIds = juce::StringArray::fromTokens(obj.getProperty("stepIds").toString(), ",", "");
    if (stepTypes.size() != stepIds.size())
        return false;

    for (int i = 0; i < stepTypes.size(); ++i) {
        const int typeValue = stepTypes[i].getIntValue();
        if (typeValue < static_cast<int>(magda::ChainStepType::Rack) ||
            typeValue > static_cast<int>(magda::ChainStepType::Device))
            return false;
        path.steps.push_back(
            {static_cast<magda::ChainStepType>(typeValue), stepIds[i].getIntValue()});
    }

    return path.isValid();
}

bool dragObjectToChainNodePathAt(const juce::DynamicObject& obj, int index,
                                 magda::ChainNodePath& path) {
    path = {};
    const auto suffix = juce::String(index);

    path.trackId =
        static_cast<magda::TrackId>(static_cast<int>(obj.getProperty("trackId" + suffix)));
    path.topLevelDeviceId = static_cast<magda::DeviceId>(
        static_cast<int>(obj.getProperty("topLevelDeviceId" + suffix)));
    path.isTrackLevel = static_cast<bool>(obj.getProperty("isTrackLevel" + suffix));

    auto stepTypes =
        juce::StringArray::fromTokens(obj.getProperty("stepTypes" + suffix).toString(), ",", "");
    auto stepIds =
        juce::StringArray::fromTokens(obj.getProperty("stepIds" + suffix).toString(), ",", "");
    if (stepTypes.size() != stepIds.size())
        return false;

    for (int i = 0; i < stepTypes.size(); ++i) {
        const int typeValue = stepTypes[i].getIntValue();
        if (typeValue < static_cast<int>(magda::ChainStepType::Rack) ||
            typeValue > static_cast<int>(magda::ChainStepType::Device))
            return false;
        path.steps.push_back(
            {static_cast<magda::ChainStepType>(typeValue), stepIds[i].getIntValue()});
    }

    return path.isValid();
}

std::vector<magda::ChainNodePath> dragObjectToChainNodePaths(const juce::DynamicObject& obj) {
    std::vector<magda::ChainNodePath> paths;
    const auto count = static_cast<int>(obj.getProperty("pathCount"));
    for (int i = 0; i < count; ++i) {
        magda::ChainNodePath path;
        if (dragObjectToChainNodePathAt(obj, i, path))
            paths.push_back(path);
    }

    if (paths.empty()) {
        magda::ChainNodePath path;
        if (dragObjectToChainNodePath(obj, path))
            paths.push_back(path);
    }
    return paths;
}
}  // namespace

//==============================================================================
// GainMeterComponent - Vertical gain slider with peak meter background
//==============================================================================
class GainMeterComponent : public juce::Component,
                           public juce::Label::Listener,
                           private juce::Timer {
  public:
    GainMeterComponent() {
        // Editable label for dB value
        dbLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
        dbLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        dbLabel_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        dbLabel_.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        dbLabel_.setColour(juce::Label::outlineWhenEditingColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        dbLabel_.setColour(juce::Label::backgroundWhenEditingColourId,
                           DarkTheme::getColour(DarkTheme::BACKGROUND));
        dbLabel_.setJustificationType(juce::Justification::centred);
        dbLabel_.setEditable(false, true, false);  // Single-click to edit
        dbLabel_.addListener(this);
        addAndMakeVisible(dbLabel_);

        updateLabel();

        // Start timer for mock meter animation
        startTimerHz(30);
    }

    ~GainMeterComponent() override {
        stopTimer();
    }

    void setGainDb(double db, juce::NotificationType notification = juce::sendNotification) {
        db = juce::jlimit(-60.0, 6.0, db);
        if (std::abs(gainDb_ - db) > 0.01) {
            gainDb_ = db;
            updateLabel();
            repaint();
            if (notification != juce::dontSendNotification && onGainChanged) {
                onGainChanged(gainDb_);
            }
        }
    }

    double getGainDb() const {
        return gainDb_;
    }

    // Mock meter level (0-1) - in real implementation this would come from audio processing
    void setMeterLevel(float level) {
        meterLevel_ = juce::jlimit(0.0f, 1.0f, level);
        repaint();
    }

    std::function<void(double)> onGainChanged;

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        auto meterArea = bounds.removeFromTop(bounds.getHeight() - 14).reduced(2);

        // Background
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
        g.fillRoundedRectangle(meterArea.toFloat(), 2.0f);

        // Meter fill (from bottom up)
        float fillHeight = meterLevel_ * meterArea.getHeight();
        auto fillArea = meterArea.removeFromBottom(static_cast<int>(fillHeight));

        // Gradient from green (low) to yellow to red (high)
        juce::ColourGradient gradient(
            juce::Colour(0xff2ecc71), 0.0f, static_cast<float>(meterArea.getBottom()),
            juce::Colour(0xffe74c3c), 0.0f, static_cast<float>(meterArea.getY()), false);
        gradient.addColour(0.7, juce::Colour(0xfff39c12));  // Yellow at 70%
        g.setGradientFill(gradient);
        g.fillRect(fillArea);

        // Gain position indicator (horizontal line)
        float gainNormalized = static_cast<float>((gainDb_ + 60.0) / 66.0);  // -60 to +6 dB
        int gainY =
            meterArea.getY() + static_cast<int>((1.0f - gainNormalized) * meterArea.getHeight());
        g.setColour(DarkTheme::getTextColour());
        g.drawHorizontalLine(gainY, static_cast<float>(meterArea.getX()),
                             static_cast<float>(meterArea.getRight()));

        // Small triangles on sides to show gain position
        juce::Path triangle;
        triangle.addTriangle(static_cast<float>(meterArea.getX()), static_cast<float>(gainY - 3),
                             static_cast<float>(meterArea.getX()), static_cast<float>(gainY + 3),
                             static_cast<float>(meterArea.getX() + 4), static_cast<float>(gainY));
        g.fillPath(triangle);

        triangle.clear();
        triangle.addTriangle(
            static_cast<float>(meterArea.getRight()), static_cast<float>(gainY - 3),
            static_cast<float>(meterArea.getRight()), static_cast<float>(gainY + 3),
            static_cast<float>(meterArea.getRight() - 4), static_cast<float>(gainY));
        g.fillPath(triangle);

        // Border
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        auto fullMeterArea = getLocalBounds().removeFromTop(getHeight() - 14).reduced(2);
        g.drawRoundedRectangle(fullMeterArea.toFloat(), 2.0f, 1.0f);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        dbLabel_.setBounds(bounds.removeFromBottom(14));
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isLeftButtonDown()) {
            dragging_ = true;
            setGainFromY(e.y);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragging_) {
            setGainFromY(e.y);
        }
    }

    void mouseUp(const juce::MouseEvent&) override {
        dragging_ = false;
    }

    void mouseDoubleClick(const juce::MouseEvent&) override {
        // Reset to unity (0 dB)
        setGainDb(0.0);
    }

    // Label::Listener
    void labelTextChanged(juce::Label* label) override {
        if (label == &dbLabel_) {
            auto text = dbLabel_.getText().trim();
            // Remove "dB" suffix if present
            if (text.endsWithIgnoreCase("db")) {
                text = text.dropLastCharacters(2).trim();
            }
            double newDb = text.getDoubleValue();
            setGainDb(newDb);
        }
    }

  private:
    double gainDb_ = 0.0;
    float meterLevel_ = 0.0f;
    float peakLevel_ = 0.0f;
    bool dragging_ = false;
    juce::Label dbLabel_;

    void updateLabel() {
        if (gainDb_ <= -60.0) {
            dbLabel_.setText("-inf", juce::dontSendNotification);
        } else {
            dbLabel_.setText(juce::String(gainDb_, 1), juce::dontSendNotification);
        }
    }

    void setGainFromY(int y) {
        auto meterArea = getLocalBounds().removeFromTop(getHeight() - 14).reduced(2);
        float normalized = 1.0f - static_cast<float>(y - meterArea.getY()) / meterArea.getHeight();
        normalized = juce::jlimit(0.0f, 1.0f, normalized);
        double db = -60.0 + normalized * 66.0;  // -60 to +6 dB range
        setGainDb(db);
    }

    void timerCallback() override {
        // Mock meter animation - simulate audio activity
        // In real implementation, this would receive actual audio levels
        float targetLevel = static_cast<float>((gainDb_ + 60.0) / 66.0) * 0.8f;
        targetLevel += (juce::Random::getSystemRandom().nextFloat() - 0.5f) * 0.1f;
        meterLevel_ = meterLevel_ * 0.9f + targetLevel * 0.1f;
        meterLevel_ = juce::jlimit(0.0f, 1.0f, meterLevel_);
        repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainMeterComponent)
};

//==============================================================================
// DeviceButtonLookAndFeel - Small buttons with minimal rounding for device slots
//==============================================================================
class DeviceButtonLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& bgColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        // Minimal corner radius (2% of smaller dimension)
        float cornerRadius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.02f;

        auto baseColour = bgColour;
        if (shouldDrawButtonAsDown)
            baseColour = baseColour.darker(0.2f);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter(0.1f);

        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, cornerRadius);

        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool /*isMouseOver*/,
                        bool /*isButtonDown*/) override {
        auto font =
            FontManager::getInstance().getUIFont(DebugSettings::getInstance().getButtonFontSize());
        g.setFont(font);
        g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                              : juce::TextButton::textColourOffId));
        g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    }
};

//==============================================================================
// ChainContainer - Container for track chain that paints arrows between elements
//==============================================================================
namespace {
// Defined further down (with the chain-preset menu wiring); declared here so
// ChainContainer's inline drop handlers can surface load failures the same way.
void showChainPresetErrorAsync(const juce::String& title, const juce::String& message);
}  // namespace

class TrackChainContent::ChainContainer : public juce::Component,
                                          public juce::DragAndDropTarget,
                                          public juce::FileDragAndDropTarget {
  public:
    explicit ChainContainer(TrackChainContent& owner) : owner_(owner) {}

    void setNodeComponents(const std::vector<std::unique_ptr<NodeComponent>>* nodes) {
        nodeComponents_ = nodes;
    }

    void mouseMove(const juce::MouseEvent&) override {
        // Check if drop state is stale (drag was cancelled)
        checkAndResetStaleDropState();
    }

    void mouseEnter(const juce::MouseEvent&) override {
        // Check if drop state is stale (drag was cancelled while outside)
        checkAndResetStaleDropState();
    }

    void mouseDown(const juce::MouseEvent& e) override {
        // Alt/Option + click = start zoom drag
        if (e.mods.isAltDown()) {
            isZoomDragging_ = true;
            zoomDragStartX_ = e.x;
            zoomStartLevel_ = owner_.zoomLevel_;
            DBG("ChainContainer: Alt+click - starting zoom drag");
        } else {
            // Clicking empty area deselects all devices
            owner_.clearDeviceSelection();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (isZoomDragging_) {
            // Drag right = zoom in, drag left = zoom out
            int deltaX = e.x - zoomDragStartX_;
            float zoomDelta = deltaX * 0.005f;  // Sensitivity factor
            owner_.setZoomLevel(zoomStartLevel_ + zoomDelta);
        }
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (isZoomDragging_) {
            isZoomDragging_ = false;
            DBG("ChainContainer: zoom drag ended");
        }
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        // Alt/Option + scroll wheel also works for zoom
        if (e.mods.isAltDown()) {
            owner_.setZoomLevel(owner_.zoomLevel_ + (wheel.deltaY > 0 ? 0.1f : -0.1f));
        } else {
            Component::mouseWheelMove(e, wheel);
        }
    }

    void paint(juce::Graphics& g) override {
        auto appendZone = juce::Rectangle<int>(
            owner_.calculateAppendZoneX(), 0,
            owner_.getScaledWidth(TrackChainContent::APPEND_ZONE_WIDTH), getHeight());
        const bool appendHighlighted =
            owner_.dragInsertIndex_ == static_cast<int>(owner_.nodeComponents_.size()) ||
            owner_.dropInsertIndex_ == static_cast<int>(owner_.nodeComponents_.size());
        auto appendColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE)
                                .withAlpha(appendHighlighted ? 0.18f : 0.06f);
        g.setColour(appendColour);
        g.fillRoundedRectangle(appendZone.reduced(6, 10).toFloat(), 4.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE)
                        .withAlpha(appendHighlighted ? 0.75f : 0.24f));
        g.drawRoundedRectangle(appendZone.reduced(6, 10).toFloat(), 4.0f, 1.0f);

        // Draw insertion indicator during drag (reorder or drop)
        if (owner_.dragInsertIndex_ >= 0 || owner_.dropInsertIndex_ >= 0) {
            int indicatorIndex =
                owner_.dragInsertIndex_ >= 0 ? owner_.dragInsertIndex_ : owner_.dropInsertIndex_;
            int indicatorX = owner_.calculateIndicatorX(indicatorIndex);
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
            g.fillRect(indicatorX - 2, 0, 4, getHeight());
        }

        // Draw ghost image during drag
        if (owner_.dragGhostImage_.isValid()) {
            g.setOpacity(0.6f);
            int ghostX = owner_.dragMousePos_.x - owner_.dragGhostImage_.getWidth() / 2;
            int ghostY = owner_.dragMousePos_.y - owner_.dragGhostImage_.getHeight() / 2;
            g.drawImageAt(owner_.dragGhostImage_, ghostX, ghostY);
            g.setOpacity(1.0f);
        }
    }

    // DragAndDropTarget implementation
    bool isInterestedInDragSource(const SourceDetails& details) override {
        if (owner_.selectedTrackId_ == magda::INVALID_TRACK_ID) {
            return false;
        }
        if (auto* obj = details.description.getDynamicObject()) {
            auto type = obj->getProperty("type").toString();
            if (type == "plugin" || type == "chainElement" || type == "chainElements") {
                return true;
            }
            // Linux: the media browser drags rows as {type:"files",paths:[...]}.
            // macOS/Windows route the same drop through FileDragAndDropTarget.
            if (type == "files") {
                return anyDroppablePreset(filePathsFromDescription(details.description));
            }
        }
        return false;
    }

    void itemDragEnter(const SourceDetails& details) override {
        owner_.dropInsertIndex_ = owner_.calculateInsertIndex(details.localPosition.x);
        owner_.startTimerHz(10);  // Start timer to detect stale drop state
        owner_.resized();         // Trigger relayout to add left padding
        repaint();
    }

    void itemDragMove(const SourceDetails& details) override {
        owner_.dropInsertIndex_ = owner_.calculateInsertIndex(details.localPosition.x);
        repaint();
    }

    void itemDragExit(const SourceDetails&) override {
        owner_.dropInsertIndex_ = -1;
        owner_.stopTimer();
        owner_.resized();  // Trigger relayout to remove left padding
        repaint();
    }

    void itemDropped(const SourceDetails& details) override {
        if (auto* obj = details.description.getDynamicObject()) {
            int insertIndex = owner_.dropInsertIndex_ >= 0
                                  ? owner_.dropInsertIndex_
                                  : static_cast<int>(nodeComponents_->size());
            const bool shouldScrollToEnd = insertIndex >= static_cast<int>(nodeComponents_->size());
            const auto type = obj->getProperty("type").toString();

            // Linux: media-browser preset rows arrive as a {type:"files"} payload.
            if (type == "files") {
                clearDropFeedback();
                applyDroppedPresets(filePathsFromDescription(details.description), insertIndex,
                                    shouldScrollToEnd);
                return;
            }

            if (type == "chainElement" || type == "chainElements") {
                auto sourcePaths = dragObjectToChainNodePaths(*obj);
                if (!sourcePaths.empty()) {
                    magda::ChainNodePath destinationPath;
                    destinationPath.trackId = owner_.selectedTrackId_;
                    auto safeOwner = juce::Component::SafePointer<TrackChainContent>(&owner_);

                    // Alt+drag copies the devices instead of moving them, matching
                    // the track-header and clip copy gestures.
                    const bool copy = juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown();

                    owner_.scrollToEndAfterNextDeviceChange_ = shouldScrollToEnd;
                    owner_.suppressNextImplicitScrollToEnd_ = !shouldScrollToEnd;
                    owner_.dropInsertIndex_ = -1;
                    owner_.stopTimer();
                    owner_.resized();
                    repaint();

                    if (copy) {
                        auto elements =
                            magda::TrackManager::getInstance().copyChainElements(sourcePaths);
                        juce::MessageManager::callAsync([safeOwner, destinationPath,
                                                         elements = std::move(elements),
                                                         insertIndex]() mutable {
                            auto command = std::make_unique<magda::PasteChainElementsCommand>(
                                destinationPath, std::move(elements), insertIndex);
                            auto* pasteCommand = command.get();
                            magda::UndoManager::getInstance().executeCommand(std::move(command));
                            if (!pasteCommand->didPaste() && safeOwner != nullptr) {
                                safeOwner->scrollToEndAfterNextDeviceChange_ = false;
                                safeOwner->suppressNextImplicitScrollToEnd_ = false;
                            }
                        });
                    } else {
                        juce::MessageManager::callAsync([safeOwner, sourcePaths, destinationPath,
                                                         insertIndex]() {
                            auto command = std::make_unique<magda::MoveChainElementsCommand>(
                                sourcePaths, destinationPath, insertIndex);
                            auto* moveCommand = command.get();
                            magda::UndoManager::getInstance().executeCommand(std::move(command));
                            if (!moveCommand->didMove() && safeOwner != nullptr) {
                                safeOwner->scrollToEndAfterNextDeviceChange_ = false;
                                safeOwner->suppressNextImplicitScrollToEnd_ = false;
                            }
                        });
                    }
                    return;
                }
            }

            if (type != "plugin") {
                owner_.dropInsertIndex_ = -1;
                owner_.stopTimer();
                owner_.resized();
                repaint();
                return;
            }

            // Create DeviceInfo from dropped plugin
            magda::DeviceInfo device;
            device.name = obj->getProperty("name").toString().toStdString();
            device.manufacturer = obj->getProperty("manufacturer").toString().toStdString();
            auto uniqueId = obj->getProperty("uniqueId").toString();
            device.pluginId = uniqueId.isNotEmpty() ? uniqueId
                                                    : obj->getProperty("name").toString() + "_" +
                                                          obj->getProperty("format").toString();
            device.isInstrument = static_cast<bool>(obj->getProperty("isInstrument"));
            if (obj->getProperty("subcategory").toString() == "MIDI")
                device.deviceType = magda::DeviceType::MIDI;
            else if (device.isInstrument)
                device.deviceType = magda::DeviceType::Instrument;
            // External plugin identification - critical for loading
            device.uniqueId = obj->getProperty("uniqueId").toString();
            device.fileOrIdentifier = obj->getProperty("fileOrIdentifier").toString();

            juce::String format = obj->getProperty("format").toString();
            if (format == "VST3") {
                device.format = magda::PluginFormat::VST3;
            } else if (format == "AU") {
                device.format = magda::PluginFormat::AU;
            } else if (format == "VST") {
                device.format = magda::PluginFormat::VST;
            } else if (format == "Internal") {
                device.format = magda::PluginFormat::Internal;
            }

            owner_.scrollToEndAfterNextDeviceChange_ = shouldScrollToEnd;
            owner_.suppressNextImplicitScrollToEnd_ = !shouldScrollToEnd;
            magda::TrackManager::getInstance().addDeviceToTrack(owner_.selectedTrackId_, device,
                                                                insertIndex);

            DBG("Dropped plugin: " + juce::String(device.name) + " at index " +
                juce::String(insertIndex));
        }
        owner_.dropInsertIndex_ = -1;
        owner_.stopTimer();
        owner_.resized();  // Trigger relayout to remove left padding
        repaint();
    }

    // FileDragAndDropTarget — macOS/Windows deliver the media-browser preset
    // drag as an OS file drag through here; Linux takes the internal path above.
    bool isInterestedInFileDrag(const juce::StringArray& files) override {
        return owner_.selectedTrackId_ != magda::INVALID_TRACK_ID && anyDroppablePreset(files);
    }

    void fileDragEnter(const juce::StringArray&, int x, int /*y*/) override {
        owner_.dropInsertIndex_ = owner_.calculateInsertIndex(x);
        owner_.startTimerHz(10);
        owner_.resized();
        repaint();
    }

    void fileDragMove(const juce::StringArray&, int x, int /*y*/) override {
        owner_.dropInsertIndex_ = owner_.calculateInsertIndex(x);
        repaint();
    }

    void fileDragExit(const juce::StringArray&) override {
        clearDropFeedback();
    }

    void filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/) override {
        const int insertIndex = owner_.dropInsertIndex_ >= 0
                                    ? owner_.dropInsertIndex_
                                    : static_cast<int>(nodeComponents_->size());
        const bool shouldScrollToEnd = insertIndex >= static_cast<int>(nodeComponents_->size());
        clearDropFeedback();
        applyDroppedPresets(files, insertIndex, shouldScrollToEnd);
    }

  private:
    void clearDropFeedback() {
        owner_.dropInsertIndex_ = -1;
        owner_.stopTimer();
        owner_.resized();
        repaint();
    }

    static juce::StringArray filePathsFromDescription(const juce::var& description) {
        juce::StringArray paths;
        if (auto* obj = description.getDynamicObject()) {
            if (obj->getProperty("type").toString() == "files") {
                if (auto* arr = obj->getProperty("paths").getArray()) {
                    for (const auto& v : *arr) {
                        paths.add(v.toString());
                    }
                }
            }
        }
        return paths;
    }

    // Phase 1 accepts chain and device presets; rack presets are a follow-up.
    static bool isDroppablePreset(const juce::String& path) {
        const auto ref = magda::PresetManager::getInstance().classifyPresetFile(juce::File(path));
        return ref.has_value() && (ref->kind == magda::PresetManager::PresetRef::Kind::Chain ||
                                   ref->kind == magda::PresetManager::PresetRef::Kind::Device);
    }

    static bool anyDroppablePreset(const juce::StringArray& files) {
        for (const auto& f : files) {
            if (isDroppablePreset(f)) {
                return true;
            }
        }
        return false;
    }

    // Load each dropped preset onto the selected track: chain presets replace
    // the whole chain, device presets insert at the drop position (advancing
    // the index so a multi-file drop keeps its order).
    void applyDroppedPresets(const juce::StringArray& files, int insertIndex,
                             bool shouldScrollToEnd) {
        const auto trackId = owner_.selectedTrackId_;
        if (trackId == magda::INVALID_TRACK_ID) {
            return;
        }
        auto& presets = magda::PresetManager::getInstance();
        auto& tracks = magda::TrackManager::getInstance();
        for (const auto& path : files) {
            const auto ref = presets.classifyPresetFile(juce::File(path));
            if (!ref.has_value()) {
                continue;
            }
            if (ref->kind == magda::PresetManager::PresetRef::Kind::Chain) {
                std::vector<magda::ChainElement> elements;
                if (!presets.loadChainPreset(ref->name, elements)) {
                    showChainPresetErrorAsync("Load Chain Preset Failed", presets.getLastError());
                    continue;
                }
                if (!tracks.applyChainPreset(trackId, std::move(elements))) {
                    showChainPresetErrorAsync("Load Chain Preset Failed",
                                              "Failed to apply preset to track.");
                }
            } else if (ref->kind == magda::PresetManager::PresetRef::Kind::Device) {
                magda::DeviceInfo device;
                if (!presets.loadDevicePreset(ref->pluginFolder, ref->name, device)) {
                    showChainPresetErrorAsync("Load Device Preset Failed", presets.getLastError());
                    continue;
                }
                owner_.scrollToEndAfterNextDeviceChange_ = shouldScrollToEnd;
                owner_.suppressNextImplicitScrollToEnd_ = !shouldScrollToEnd;
                tracks.addDeviceToTrack(trackId, device, insertIndex);
                ++insertIndex;
            }
        }
    }

    void checkAndResetStaleDropState() {
        if (owner_.dropInsertIndex_ >= 0) {
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
                if (!container->isDragAndDropActive()) {
                    owner_.dropInsertIndex_ = -1;
                    owner_.resized();
                    repaint();
                }
            }
        }
    }

    TrackChainContent& owner_;
    const std::vector<std::unique_ptr<NodeComponent>>* nodeComponents_ = nullptr;

    // Zoom drag state
    bool isZoomDragging_ = false;
    int zoomDragStartX_ = 0;
    float zoomStartLevel_ = 1.0f;
};

//==============================================================================
// ZoomableViewport - Viewport that supports Alt+scroll for zooming
//==============================================================================
class TrackChainContent::ZoomableViewport : public juce::Viewport {
  public:
    explicit ZoomableViewport(TrackChainContent& owner) : owner_(owner) {
        DBG("ZoomableViewport created for TrackChainContent");
    }

    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override {
        // Alt/Option + scroll wheel = zoom
        if (event.mods.isAltDown()) {
            float delta =
                wheel.deltaY > 0 ? TrackChainContent::ZOOM_STEP : -TrackChainContent::ZOOM_STEP;
            DBG("  -> Zooming by " << delta);
            owner_.setZoomLevel(owner_.zoomLevel_ + delta);
        } else {
            // Normal scroll - let viewport handle horizontal scrolling
            Viewport::mouseWheelMove(event, wheel);
        }
    }

  private:
    TrackChainContent& owner_;
};

// dB conversion helpers
namespace {
constexpr float MIN_DB = -60.0f;

float gainToDb(float gain) {
    if (gain <= 0.0f)
        return MIN_DB;
    return 20.0f * std::log10(gain);
}

float dbToGain(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}
}  // namespace

TrackChainContent::TrackChainContent()
    : chainViewport_(std::make_unique<ZoomableViewport>(*this)),
      chainContainer_(std::make_unique<ChainContainer>(*this)) {
    setName("Track Chain");

    // Listen for debug settings changes
    DebugSettings::getInstance().addListener([this]() {
        // Force all node components to update their fonts
        for (auto& node : nodeComponents_) {
            node->resized();
            node->repaint();
        }
        resized();
        repaint();
    });

    // Viewport for horizontal scrolling of chain content
    DBG("TrackChainContent::ctor - Setting up ZoomableViewport for chain content");
    chainViewport_->setViewedComponent(chainContainer_.get(), false);
    chainViewport_->setScrollBarsShown(true, true);  // Vertical and horizontal
    addAndMakeVisible(*chainViewport_);

    addDeviceButton_.setButtonText("+");
    addDeviceButton_.setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.24f));
    addDeviceButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    addDeviceButton_.onClick = [this]() { onAddDeviceClicked(); };
    addDeviceButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    chainContainer_->addAndMakeVisible(addDeviceButton_);

    // No selection label
    noSelectionLabel_.setText("Select a track to view its signal chain",
                              juce::dontSendNotification);
    noSelectionLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    noSelectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    noSelectionLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(noSelectionLabel_);

    // === HEADER BAR CONTROLS - LEFT SIDE (action buttons) ===

    // Global mods toggle button (same icon as rack/device mod buttons)
    globalModsButton_ = std::make_unique<magda::SvgButton>("Mod", BinaryData::iconmodsboldm_svg,
                                                           BinaryData::iconmodsboldm_svgSize);
    globalModsButton_->setClickingTogglesState(true);
    globalModsButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    globalModsButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    globalModsButton_->setActiveColor(juce::Colours::white);
    globalModsButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    globalModsButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    globalModsButton_->onClick = [this]() {
        globalModsButton_->setActive(globalModsButton_->getToggleState());
        globalModsVisible_ = globalModsButton_->getToggleState();
        if (!globalModsVisible_) {
            hideGlobalModEditor();
        }
        if (globalModsPanel_)
            globalModsPanel_->setVisible(globalModsVisible_);
        if (auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_))
            track->globalModsPanelOpen = globalModsVisible_;
        resized();
        repaint();
    };
    addChildComponent(*globalModsButton_);

    // Macro button (global macros toggle)
    macroButton_ =
        std::make_unique<magda::SvgButton>("Macro", BinaryData::knob_svg, BinaryData::knob_svgSize);
    macroButton_->setClickingTogglesState(true);
    macroButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    macroButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    macroButton_->setActiveColor(juce::Colours::white);
    macroButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    macroButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    macroButton_->onClick = [this]() {
        macroButton_->setActive(macroButton_->getToggleState());
        globalMacrosVisible_ = macroButton_->getToggleState();
        if (!globalMacrosVisible_) {
            hideGlobalMacroEditor();
        }
        if (globalMacrosPanel_)
            globalMacrosPanel_->setVisible(globalMacrosVisible_);
        if (auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_))
            track->globalMacrosPanelOpen = globalMacrosVisible_;
        resized();
        repaint();
    };
    addChildComponent(*macroButton_);

    // Add rack button (rack icon with blue fill, grey border)
    addRackButton_ = std::make_unique<magda::SvgButton>("Rack", BinaryData::iconracksboldm_svg,
                                                        BinaryData::iconracksboldm_svgSize);
    addRackButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));  // Match SVG fill color
    addRackButton_->setNormalColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    addRackButton_->setHoverColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).brighter(0.2f));
    addRackButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    addRackButton_->onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().addRackToTrack(selectedTrackId_);
        }
    };
    addChildComponent(*addRackButton_);

    // Tree view button (show chain tree dialog)
    treeViewButton_ = std::make_unique<magda::SvgButton>("Tree", BinaryData::icontreeviewboldm_svg,
                                                         BinaryData::icontreeviewboldm_svgSize);
    treeViewButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    treeViewButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    treeViewButton_->setHoverColor(DarkTheme::getTextColour());
    treeViewButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    treeViewButton_->onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::ChainTreeDialog::show(selectedTrackId_);
        }
    };
    addChildComponent(*treeViewButton_);

    // Preset button (MAGDA track-chain presets menu) — same indigo cue as
    // device / rack preset buttons so it reads as the same feature, just
    // sitting on the LEFT of the track header instead of inside a node.
    presetButton_ =
        std::make_unique<magda::SvgButton>("Presets", BinaryData::iconpresetsroundboldm_svg,
                                           BinaryData::iconpresetsroundboldm_svgSize);
    constexpr juce::uint32 PRESET_INDIGO = 0xFF5577CC;
    presetButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    presetButton_->setNormalColor(juce::Colour(PRESET_INDIGO));
    presetButton_->setHoverColor(juce::Colour(PRESET_INDIGO).brighter(0.2f));
    presetButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    presetButton_->setTooltip("MAGDA Track Presets");
    presetButton_->onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID)
            showPresetMenu();
    };
    addChildComponent(*presetButton_);

    // Gain-staging pass toggle. Steps the GainStagingManager through
    // start (collect) -> stop (compute + apply) -> clear for the selected
    // track. Active tint follows the mode: red while collecting, amber once
    // staged. The icon recolors via SvgButton's black-replacement path
    // (the SVG uses currentColor).
    gainStagingButton_ = std::make_unique<magda::SvgButton>(
        "GainStaging", BinaryData::gainstaging_svg, BinaryData::gainstaging_svgSize);
    gainStagingButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    gainStagingButton_->setHoverColor(DarkTheme::getTextColour());
    gainStagingButton_->setActiveColor(juce::Colours::white.darker(0.18f));
    gainStagingButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    gainStagingButton_->onClick = [this]() {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID || aiProcessing_)
            return;
        auto& gsm = magda::GainStagingManager::getInstance();
        // Idle -> ask for the target and AI choice, then start. Collecting ->
        // stop: the AI path hands off to the agent, otherwise the flat cascade.
        if (gsm.getMode() == magda::GainStagingMode::Idle) {
            const auto trackId = selectedTrackId_;
            magda::GainStagingDialog::showDialog(
                this, gsm.getTargetDb(), gsm.getUseAi(),
                [trackId](const magda::GainStagingDialog::Settings& settings) {
                    auto& m = magda::GainStagingManager::getInstance();
                    m.setTargetDb(settings.targetDb);
                    m.setUseAi(settings.useAi);
                    m.startCollection(trackId);
                });
        } else if (gsm.getMode() == magda::GainStagingMode::Collecting) {
            if (gsm.getUseAi())
                runAiGainStagingPass();
            else
                gsm.stopCollection();
        }
    };
    addChildComponent(*gainStagingButton_);
    refreshGainStagingButton();

    // Analysis-device toggles — one-click add/remove of an Oscilloscope or
    // Spectrum in this track's post-fx. Lit while the device is present; the
    // model keeps them unique per kind, so this is a clean on/off.
    // Active colours are chosen to NOT clash with the mod (orange) and macro
    // (purple) toggles next door.
    auto setupAnalysisToggle = [this](std::unique_ptr<magda::SvgButton>& button, const char* name,
                                      const char* svg, size_t svgSize, const juce::String& tooltip,
                                      const juce::String& pluginId, const juce::String& displayName,
                                      juce::Colour activeBg) {
        button = std::make_unique<magda::SvgButton>(name, svg, svgSize);
        // Tell SvgButton the icon's native fill so it recolors the glyph (grey
        // idle, white when engaged). Engaged look = subtle tint + coloured
        // border rather than a solid candy fill.
        button->setOriginalColor(juce::Colour(0xFFB3B3B3));
        button->setNormalColor(DarkTheme::getSecondaryTextColour());
        button->setHoverColor(DarkTheme::getTextColour());
        button->setActiveColor(juce::Colours::white.darker(0.18f));
        button->setActiveBackgroundColor(activeBg.withAlpha(0.20f));
        button->setActiveBorderColor(activeBg);
        button->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
        button->setTooltip(tooltip);
        button->onClick = [this, pluginId, displayName]() {
            togglePostFxAnalysisDevice(pluginId, displayName);
        };
        addChildComponent(*button);
    };
    // Muted so they sit with the dark chrome rather than reading as candy
    // (and still clear of the mod orange / macro purple next door).
    const auto muted = [](juce::uint32 c) {
        return juce::Colour(c).withMultipliedSaturation(0.55f).withMultipliedBrightness(0.85f);
    };
    setupAnalysisToggle(oscToggleButton_, "Oscilloscope", BinaryData::oscilloscope3_svg,
                        BinaryData::oscilloscope3_svgSize, "Oscilloscope (post-FX)", "oscilloscope",
                        "Oscilloscope", muted(DarkTheme::ACCENT_GREEN));
    setupAnalysisToggle(specToggleButton_, "Spectrum", BinaryData::iconspectrumboldm_svg,
                        BinaryData::iconspectrumboldm_svgSize, "Spectrum Analyzer (post-FX)",
                        "spectrumanalyzer", "Spectrum Analyzer", muted(DarkTheme::ACCENT_CYAN));
    setupAnalysisToggle(levelsToggleButton_, "Levels", BinaryData::iconlevelsboldm_svg,
                        BinaryData::iconlevelsboldm_svgSize, "Levels meter (post-FX)", "levels",
                        "Levels", muted(DarkTheme::ACCENT_BLUE));

    // Post-FX panel show/hide toggle. The panel itself lives in BottomPanel,
    // which wires onPostFxPanelToggled / setPostFxPanelOpen.
    postFxPanelButton_ = std::make_unique<magda::SvgButton>("PostFx", BinaryData::postfx_svg,
                                                            BinaryData::postfx_svgSize);
    postFxPanelButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    postFxPanelButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    postFxPanelButton_->setHoverColor(DarkTheme::getTextColour());
    postFxPanelButton_->setActiveColor(juce::Colours::white.darker(0.18f));
    postFxPanelButton_->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.20f));
    postFxPanelButton_->setActiveBorderColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    postFxPanelButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    postFxPanelButton_->setTooltip("Show/hide the post-FX panel");
    postFxPanelButton_->onClick = [this]() {
        if (onPostFxPanelToggled)
            onPostFxPanelToggled(!postFxPanelOpen_);
    };
    addChildComponent(*postFxPanelButton_);

    // === HEADER BAR CONTROLS - RIGHT SIDE (track info) ===

    // Track name label - clicks pass through for track selection
    trackNameLabel_.setFont(FontManager::getInstance().getUIFontBold(11.0f));
    trackNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    trackNameLabel_.setJustificationType(juce::Justification::centredRight);
    trackNameLabel_.setInterceptsMouseClicks(false, false);
    addChildComponent(trackNameLabel_);

    // Mute button
    muteButton_.setButtonText("M");
    muteButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    muteButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::STATUS_WARNING));
    muteButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    muteButton_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    muteButton_.setClickingTogglesState(true);
    muteButton_.onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackMuteCommand>(selectedTrackId_,
                                                             muteButton_.getToggleState()));
        }
    };
    muteButton_.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    muteButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addChildComponent(muteButton_);

    // Master mute: speaker toggle shown in place of "M" when the master is selected.
    configureMasterSpeakerButton(masterMuteButton_);
    masterMuteButton_.onClick = [this]() {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetMasterMuteCommand>(masterMuteButton_.getToggleState()));
    };
    addChildComponent(masterMuteButton_);

    // Chord-track audition (mute) toggle: cyan speaker, mirrors the chord header.
    chordSpeakerButton_ = std::make_unique<magda::SvgButton>(
        "ChordAudition", BinaryData::chord_off_svg, BinaryData::chord_off_svgSize,
        BinaryData::chord_on_1_svg, BinaryData::chord_on_1_svgSize);
    chordSpeakerButton_->setTooltip("Preview chords on playback");
    chordSpeakerButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    chordSpeakerButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_CYAN));
    chordSpeakerButton_->onClick = [this]() {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;
        const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
        const bool nowMuted = track ? !track->muted : true;
        chordSpeakerButton_->setActive(!nowMuted);
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetTrackMuteCommand>(selectedTrackId_, nowMuted));
    };
    addChildComponent(*chordSpeakerButton_);

    // Input monitor (Off/In/Auto), same 3-state control as the header.
    monitorButton_.setButtonText("-");
    monitorButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    monitorButton_.setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    monitorButton_.setColour(juce::TextButton::buttonOnColourId,
                             DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
    monitorButton_.setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    monitorButton_.setColour(juce::TextButton::textColourOnId,
                             DarkTheme::getColour(DarkTheme::BACKGROUND));
    monitorButton_.setColour(juce::ComboBox::outlineColourId,
                             DarkTheme::getColour(DarkTheme::BORDER));
    monitorButton_.setTooltip("Input monitoring (Off/In/Auto)");
    monitorButton_.onClick = [this]() {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;
        const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
        if (!track)
            return;
        magda::InputMonitorMode next;
        switch (track->inputMonitor) {
            case magda::InputMonitorMode::Off:
                next = magda::InputMonitorMode::In;
                break;
            case magda::InputMonitorMode::In:
                next = magda::InputMonitorMode::Auto;
                break;
            default:
                next = magda::InputMonitorMode::Off;
                break;
        }
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetTrackInputMonitorCommand>(selectedTrackId_, next));
    };
    addChildComponent(monitorButton_);

    // Solo button
    soloButton_.setButtonText("S");
    soloButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    soloButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    soloButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    soloButton_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    soloButton_.setClickingTogglesState(true);
    soloButton_.onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackSoloCommand>(selectedTrackId_,
                                                             soloButton_.getToggleState()));
        }
    };
    soloButton_.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    soloButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addChildComponent(soloButton_);

    // Volume label (dB format, draggable)
    volumeLabel_.setRange(-60.0, 6.0, 0.0);
    // Curve the fill to match the level meter's power scale (consistent with the
    // other volume controls).
    volumeLabel_.setFillExponent(static_cast<double>(magda::LevelMeter::METER_CURVE_EXPONENT));
    volumeLabel_.setValue(0.0, juce::dontSendNotification);  // Unity gain (0 dB)
    volumeLabel_.setFontSize(10.0f);
    volumeLabel_.setFillColour(DarkTheme::getColour(DarkTheme::CONTROL_VALUE_FILL));
    volumeLabel_.onValueChange = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            float gain = dbToGain(static_cast<float>(volumeLabel_.getValue()));
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackVolumeCommand>(selectedTrackId_, gain));
        }
    };
    addChildComponent(volumeLabel_);

    // Pan label (L/C/R format, draggable)
    panLabel_.setRange(-1.0, 1.0, 0.0);
    panLabel_.setValue(0.0, juce::dontSendNotification);  // Center
    panLabel_.setFontSize(10.0f);
    panLabel_.setFillColour(DarkTheme::getColour(DarkTheme::CONTROL_VALUE_FILL));
    panLabel_.onValueChange = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackPanCommand>(
                    selectedTrackId_, static_cast<float>(panLabel_.getValue())));
        }
    };
    addChildComponent(panLabel_);

    // Chain bypass button (power icon - same as device bypass buttons)
    chainBypassButton_ = std::make_unique<magda::SvgButton>("Power", BinaryData::power_on_svg,
                                                            BinaryData::power_on_svgSize);
    chainBypassButton_->setClickingTogglesState(true);
    chainBypassButton_->setToggleState(true,
                                       juce::dontSendNotification);  // Start active (not bypassed)
    chainBypassButton_->setNormalColor(DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    chainBypassButton_->setActiveColor(juce::Colours::white);
    chainBypassButton_->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_GREEN).darker(0.3f));
    chainBypassButton_->setActive(true);  // Start active
    chainBypassButton_->onClick = [this]() {
        bool active = chainBypassButton_->getToggleState();
        chainBypassButton_->setActive(active);
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().setChainBypassed(selectedTrackId_, !active);
        }
        // Update all node components to reflect bypass state
        for (auto& node : nodeComponents_) {
            node->setBypassed(!active);
        }
    };
    addChildComponent(*chainBypassButton_);

    // Link mode indicator label (centered, big text)
    linkModeLabel_.setText("LINK MODE", juce::dontSendNotification);
    linkModeLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    linkModeLabel_.setColour(juce::Label::textColourId,
                             DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    linkModeLabel_.setJustificationType(juce::Justification::centred);
    linkModeLabel_.setVisible(false);
    addChildComponent(linkModeLabel_);

    // Gain-staging mode indicator label (centered, big text), parallel to the
    // link-mode label.
    gainStagingLabel_.setText("GAIN STAGING", juce::dontSendNotification);
    gainStagingLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    gainStagingLabel_.setColour(juce::Label::textColourId,
                                DarkTheme::getColour(DarkTheme::STATUS_DANGER));
    gainStagingLabel_.setJustificationType(juce::Justification::centred);
    gainStagingLabel_.setMinimumHorizontalScale(0.5f);  // let the AI summary shrink to fit
    // The banner spans the whole header bar; it must NOT eat clicks meant for
    // the gain-staging button underneath (otherwise you can't stop a pass).
    gainStagingLabel_.setInterceptsMouseClicks(false, false);
    gainStagingLabel_.setVisible(false);
    addChildComponent(gainStagingLabel_);

    // Blink the "GETTING AI RESULTS" banner while the agent is thinking.
    aiBlinkTimer_.onTick = [this]() {
        aiBlinkOn_ = !aiBlinkOn_;
        refreshGainStagingButton();
    };

    // Initialize global mods/macros panels
    initGlobalModsPanel();
    initGlobalMacrosPanel();

    // Register as listeners
    magda::TrackManager::getInstance().addListener(this);
    magda::SelectionManager::getInstance().addListener(this);
    magda::LinkModeManager::getInstance().addListener(this);
    magda::GainStagingManager::getInstance().addListener(this);

    // Check if there's already a selected track
    selectedTrackId_ = magda::TrackManager::getInstance().getSelectedTrack();
    updateFromSelectedTrack();
}

TrackChainContent::~TrackChainContent() {
    stopTimer();
    magda::TrackManager::getInstance().removeListener(this);
    magda::SelectionManager::getInstance().removeListener(this);
    magda::LinkModeManager::getInstance().removeListener(this);
    magda::GainStagingManager::getInstance().removeListener(this);
}

// ==============================================================================
// Global Mods/Macros Panel Init
// ==============================================================================

void TrackChainContent::initGlobalModsPanel() {
    globalModsPanel_ = std::make_unique<ModsPanelComponent>();
    globalModsPanel_->onModTargetChanged = [this](int modIndex, magda::ControlTarget target) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().setModTarget(
                ChainNodePath::trackLevel(selectedTrackId_), modIndex, target);
    };
    globalModsPanel_->onModLinkRemoved = [this](int modIndex, magda::ControlTarget target) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().removeModLink(
                ChainNodePath::trackLevel(selectedTrackId_), modIndex, target);
            updateGlobalModsPanel();
        }
    };
    globalModsPanel_->onModAllLinksCleared = [this](int modIndex) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().clearAllModLinks(
                ChainNodePath::trackLevel(selectedTrackId_), modIndex);
            updateGlobalModsPanel();
        }
    };
    globalModsPanel_->onModNameChanged = [this](int modIndex, juce::String name) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID)
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetModNameCommand>(
                    ChainNodePath::trackLevel(selectedTrackId_), modIndex, name));
    };
    globalModsPanel_->onModClicked = [this](int modIndex) {
        if (globalModEditorVisible_ && selectedGlobalModIndex_ == modIndex) {
            hideGlobalModEditor();
        } else {
            showGlobalModEditor(modIndex);
        }
    };
    globalModsPanel_->onAddModRequested = [this](int slotIndex, magda::ModType type,
                                                 magda::LFOWaveform waveform) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().addMod(ChainNodePath::trackLevel(selectedTrackId_),
                                                      slotIndex, type, waveform);
            magda::TrackManager::getInstance().notifyTrackDevicesChanged(selectedTrackId_);
        }
    };
    globalModsPanel_->onModRemoveRequested = [this](int modIndex) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            if (selectedGlobalModIndex_ == modIndex)
                hideGlobalModEditor();
            magda::TrackManager::getInstance().removeMod(
                ChainNodePath::trackLevel(selectedTrackId_), modIndex);
        }
    };
    globalModsPanel_->onModEnableToggled = [this](int modIndex, bool enabled) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().setModEnabled(
                ChainNodePath::trackLevel(selectedTrackId_), modIndex, enabled);
    };
    globalModsPanel_->onAddPageRequested = [this](int) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().addModPage(
                ChainNodePath::trackLevel(selectedTrackId_));
    };
    globalModsPanel_->onRemovePageRequested = [this](int) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().removeModPage(
                ChainNodePath::trackLevel(selectedTrackId_));
    };
    globalModsPanel_->onPanelClicked = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            auto trackPath = magda::ChainNodePath::trackLevel(selectedTrackId_);
            magda::SelectionManager::getInstance().selectModsPanel(trackPath);
        }
    };
    addChildComponent(*globalModsPanel_);

    // Modulator editor panel
    globalModEditorPanel_ = std::make_unique<ModulatorEditorPanel>();
    globalModEditorPanel_->onNameChanged = [this](juce::String name) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalModIndex_ >= 0)
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetModNameCommand>(
                    ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalModIndex_, name));
    };
    globalModEditorPanel_->onRateChanged = [this](float rate) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalModIndex_ >= 0)
            magda::TrackManager::getInstance().setModRate(
                ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalModIndex_, rate);
    };
    globalModEditorPanel_->onWaveformChanged = [this](magda::LFOWaveform waveform) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalModIndex_ >= 0)
            magda::TrackManager::getInstance().setModWaveform(
                ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalModIndex_, waveform);
    };
    globalModEditorPanel_->onTempoSyncChanged = [this](bool tempoSync) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalModIndex_ >= 0)
            magda::TrackManager::getInstance().setModTempoSync(
                ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalModIndex_, tempoSync);
    };
    globalModEditorPanel_->onSyncDivisionChanged = [this](magda::SyncDivision division) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalModIndex_ >= 0)
            magda::TrackManager::getInstance().setModSyncDivision(
                ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalModIndex_, division);
    };
    globalModEditorPanel_->onTriggerModeChanged = [this](magda::LFOTriggerMode mode) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalModIndex_ >= 0)
            magda::TrackManager::getInstance().setModTriggerMode(
                ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalModIndex_, mode);
    };
    globalModEditorPanel_->onAudioAttackChanged = [this](float ms) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalModIndex_ >= 0)
            magda::TrackManager::getInstance().setModAudioAttack(
                ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalModIndex_, ms);
    };
    globalModEditorPanel_->onAudioReleaseChanged = [this](float ms) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalModIndex_ >= 0)
            magda::TrackManager::getInstance().setModAudioRelease(
                ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalModIndex_, ms);
    };
    globalModEditorPanel_->onEnvelopeChanged = [this](const magda::ModInfo& mod) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalModIndex_ >= 0)
            magda::TrackManager::getInstance().setModEnvelope(
                ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalModIndex_, mod);
    };
    globalModEditorPanel_->onRandomChanged = [this](const magda::ModInfo& mod) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalModIndex_ >= 0)
            magda::TrackManager::getInstance().setModRandom(
                ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalModIndex_, mod);
    };
    globalModEditorPanel_->onFollowerChanged = [this](const magda::ModInfo& mod) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalModIndex_ >= 0)
            magda::TrackManager::getInstance().setModFollower(
                ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalModIndex_, mod);
    };
    globalModEditorPanel_->onCurveChanged = [this]() {
        DBG("[HardCorner] TrackChainContent global onCurveChanged trackId="
            << static_cast<int>(selectedTrackId_) << " modIndex=" << selectedGlobalModIndex_);
        if (globalModsPanel_)
            globalModsPanel_->repaintWaveforms();
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalModIndex_ >= 0)
            magda::TrackManager::getInstance().notifyModCurveChanged(
                ChainNodePath::trackLevel(selectedTrackId_));
    };
    globalModEditorPanel_->onModLinkDeleted = [this](int modIndex, magda::ControlTarget target) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().removeModLink(
                ChainNodePath::trackLevel(selectedTrackId_), modIndex, target);
    };
    globalModEditorPanel_->onModLinkBipolarChanged =
        [this](int modIndex, magda::ControlTarget target, bool bipolar) {
            if (selectedTrackId_ != magda::INVALID_TRACK_ID)
                magda::TrackManager::getInstance().setModLinkBipolar(
                    ChainNodePath::trackLevel(selectedTrackId_), modIndex, target, bipolar);
        };
    globalModEditorPanel_->onModLinkEnabledChanged =
        [this](int modIndex, magda::ControlTarget target, bool enabled) {
            if (selectedTrackId_ != magda::INVALID_TRACK_ID)
                magda::TrackManager::getInstance().setModLinkEnabled(
                    ChainNodePath::trackLevel(selectedTrackId_), modIndex, target, enabled);
        };
    globalModEditorPanel_->onModLinkAmountChanged =
        [this](int modIndex, magda::ControlTarget target, float amount) {
            if (selectedTrackId_ != magda::INVALID_TRACK_ID)
                magda::TrackManager::getInstance().setModLinkAmount(
                    ChainNodePath::trackLevel(selectedTrackId_), modIndex, target, amount);
        };
    globalModEditorPanel_->setParamNameResolver(
        [this](magda::DeviceId deviceId, int paramIndex) -> juce::String {
            if (selectedTrackId_ == magda::INVALID_TRACK_ID)
                return "P" + juce::String(paramIndex);
            const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
            if (!track)
                return "P" + juce::String(paramIndex);
            std::function<juce::String(const std::vector<magda::ChainElement>&)> findParam;
            findParam = [&](const std::vector<magda::ChainElement>& elements) -> juce::String {
                for (const auto& element : elements) {
                    if (magda::isDevice(element)) {
                        const auto& device = magda::getDevice(element);
                        if (device.id == deviceId && paramIndex >= 0) {
                            // paramIndex is the TE index — search both buckets
                            // by ParameterInfo::paramIndex, not by array
                            // position, since the wrapper dry/wet pair lives
                            // in wrapperParameters (so the array no longer
                            // mirrors TE indices 1:1).
                            for (const auto& p : device.parameters)
                                if (p.paramIndex == paramIndex)
                                    return p.name;
                            for (const auto& p : device.wrapperParameters)
                                if (p.paramIndex == paramIndex)
                                    return p.name;
                        }
                    } else {
                        const auto& rack = magda::getRack(element);
                        for (const auto& chain : rack.chains) {
                            auto result = findParam(chain.elements);
                            if (result.isNotEmpty())
                                return result;
                        }
                    }
                }
                return {};
            };
            auto name = findParam(track->chain.fxChainElements);
            return name.isNotEmpty() ? name : ("P" + juce::String(paramIndex));
        });
    addChildComponent(*globalModEditorPanel_);
}

void TrackChainContent::initGlobalMacrosPanel() {
    globalMacrosPanel_ = std::make_unique<MacroPanelComponent>();
    globalMacrosPanel_->onMacroValueChanged = [this](int macroIndex, float value) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().setMacroValue(
                ChainNodePath::trackLevel(selectedTrackId_), macroIndex, value);
    };
    globalMacrosPanel_->onMacroTargetChanged = [this](int macroIndex, magda::ControlTarget target) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().setMacroTarget(
                ChainNodePath::trackLevel(selectedTrackId_), macroIndex, target);
    };
    globalMacrosPanel_->onMacroNameChanged = [this](int macroIndex, juce::String name) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID)
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetMacroNameCommand>(
                    ChainNodePath::trackLevel(selectedTrackId_), macroIndex, name));
    };
    globalMacrosPanel_->onMacroLinkRemoved = [this](int macroIndex, magda::ControlTarget target) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().removeMacroLink(
                ChainNodePath::trackLevel(selectedTrackId_), macroIndex, target);
            updateGlobalMacrosPanel();
        }
    };
    globalMacrosPanel_->onMacroAllLinksCleared = [this](int macroIndex) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().clearAllMacroLinks(
                ChainNodePath::trackLevel(selectedTrackId_), macroIndex);
            updateGlobalMacrosPanel();
        }
    };
    globalMacrosPanel_->onMacroClicked = [this](int macroIndex) {
        if (globalMacroEditorVisible_ && selectedGlobalMacroIndex_ == macroIndex) {
            hideGlobalMacroEditor();
        } else {
            showGlobalMacroEditor(macroIndex);
        }
    };
    globalMacrosPanel_->onAddPageRequested = [this](int) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().addMacroPage(
                ChainNodePath::trackLevel(selectedTrackId_));
    };
    globalMacrosPanel_->onRemovePageRequested = [this](int) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().removeMacroPage(
                ChainNodePath::trackLevel(selectedTrackId_));
    };
    globalMacrosPanel_->onPanelClicked = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            auto trackPath = magda::ChainNodePath::trackLevel(selectedTrackId_);
            magda::SelectionManager::getInstance().selectMacrosPanel(trackPath);
        }
    };
    addChildComponent(*globalMacrosPanel_);

    // Macro editor panel
    globalMacroEditorPanel_ = std::make_unique<MacroEditorPanel>();
    globalMacroEditorPanel_->onNameChanged = [this](juce::String name) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalMacroIndex_ >= 0)
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetMacroNameCommand>(
                    ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalMacroIndex_, name));
    };
    globalMacroEditorPanel_->onLinkAmountChanged = [this](magda::ControlTarget target,
                                                          float amount) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalMacroIndex_ >= 0)
            magda::TrackManager::getInstance().setMacroLinkAmount(
                ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalMacroIndex_, target,
                amount);
    };
    globalMacroEditorPanel_->onLinkRemoved = [this](magda::ControlTarget target) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalMacroIndex_ >= 0) {
            magda::TrackManager::getInstance().removeMacroLink(
                ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalMacroIndex_, target);
            updateGlobalMacrosPanel();
        }
    };
    globalMacroEditorPanel_->onLinkBipolarToggled = [this](magda::ControlTarget target,
                                                           bool bipolar) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID && selectedGlobalMacroIndex_ >= 0) {
            magda::TrackManager::getInstance().setMacroLinkBipolar(
                ChainNodePath::trackLevel(selectedTrackId_), selectedGlobalMacroIndex_, target,
                bipolar);
            updateGlobalMacrosPanel();
        }
    };
    globalMacroEditorPanel_->setParamNameResolver(
        [this](magda::DeviceId deviceId, int paramIndex) -> juce::String {
            if (selectedTrackId_ == magda::INVALID_TRACK_ID)
                return "P" + juce::String(paramIndex);
            const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
            if (!track)
                return "P" + juce::String(paramIndex);
            std::function<juce::String(const std::vector<magda::ChainElement>& elements)> findParam;
            findParam = [&](const std::vector<magda::ChainElement>& elements) -> juce::String {
                for (const auto& element : elements) {
                    if (magda::isDevice(element)) {
                        const auto& device = magda::getDevice(element);
                        if (device.id == deviceId && paramIndex >= 0) {
                            // paramIndex is the TE index — search both buckets
                            // by ParameterInfo::paramIndex, not by array
                            // position, since the wrapper dry/wet pair lives
                            // in wrapperParameters (so the array no longer
                            // mirrors TE indices 1:1).
                            for (const auto& p : device.parameters)
                                if (p.paramIndex == paramIndex)
                                    return p.name;
                            for (const auto& p : device.wrapperParameters)
                                if (p.paramIndex == paramIndex)
                                    return p.name;
                        }
                    } else {
                        const auto& rack = magda::getRack(element);
                        for (const auto& chain : rack.chains) {
                            auto result = findParam(chain.elements);
                            if (result.isNotEmpty())
                                return result;
                        }
                    }
                }
                return {};
            };
            auto name = findParam(track->chain.fxChainElements);
            return name.isNotEmpty() ? name : ("P" + juce::String(paramIndex));
        });
    globalMacroEditorPanel_->setModNameResolver(
        [this](magda::ModId modId, int modParamIndex) -> juce::String {
            // Same scope as the macro itself — track-level mods. Look up the
            // mod by id and append a label for the targeted param. Only Rate
            // (modParamIndex 0) is wired today.
            if (selectedTrackId_ == magda::INVALID_TRACK_ID)
                return {};
            const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
            if (!track)
                return {};
            for (const auto& m : track->mods) {
                if (m.id == modId) {
                    return magda::getModParameterDisplayName(m, modParamIndex);
                }
            }
            return {};
        });
    addChildComponent(*globalMacroEditorPanel_);
}

void TrackChainContent::updateGlobalModsPanel() {
    if (!globalModsPanel_ || selectedTrackId_ == magda::INVALID_TRACK_ID)
        return;
    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (!track)
        return;

    auto trackPath = magda::ChainNodePath::trackLevel(selectedTrackId_);
    globalModsPanel_->setParentPath(trackPath);

    std::vector<std::pair<magda::DeviceId, juce::String>> allDevices;
    std::map<magda::DeviceId, std::vector<juce::String>> allDeviceParams;

    std::function<void(const std::vector<magda::ChainElement>&)> collectDevices;
    collectDevices = [&](const std::vector<magda::ChainElement>& elements) {
        for (const auto& element : elements) {
            if (magda::isDevice(element)) {
                const auto& device = magda::getDevice(element);
                allDevices.push_back({device.id, device.name});
                std::vector<juce::String> names;
                names.reserve(device.parameters.size());
                for (const auto& p : device.parameters)
                    names.push_back(p.name);
                allDeviceParams[device.id] = std::move(names);
            } else {
                const auto& rack = magda::getRack(element);
                for (const auto& chain : rack.chains)
                    collectDevices(chain.elements);
            }
        }
    };
    collectDevices(track->chain.fxChainElements);

    globalModsPanel_->setAvailableDevices(allDevices);
    globalModsPanel_->setDeviceParamNames(allDeviceParams);

    // Track-level mods can target other track-level mods' rate via the
    // right-click "Link to Modulator" submenu. Pass the same-scope list;
    // each knob filters out its own ModId before populating its menu.
    std::vector<std::pair<magda::ModId, juce::String>> trackModsList;
    trackModsList.reserve(track->mods.size());
    for (const auto& m : track->mods)
        if (m.enabled)
            trackModsList.emplace_back(m.id, magda::getModDisplayName(m));
    globalModsPanel_->setAvailableModifiers(trackModsList);

    globalModsPanel_->setMods(track->mods);

    // Update editor if visible
    if (globalModEditorVisible_ && globalModEditorPanel_ && selectedGlobalModIndex_ >= 0 &&
        selectedGlobalModIndex_ < static_cast<int>(track->mods.size())) {
        int idx = selectedGlobalModIndex_;
        auto trackId = selectedTrackId_;
        globalModEditorPanel_->setSelectedModIndex(idx);
        globalModEditorPanel_->setModInfo(
            track->mods[idx], &track->mods[idx], [trackId, idx]() -> const magda::ModInfo* {
                const auto* t = magda::TrackManager::getInstance().getTrack(trackId);
                if (!t || idx >= static_cast<int>(t->mods.size()))
                    return nullptr;
                return &t->mods[idx];
            });
    }
}

void TrackChainContent::updateGlobalMacrosPanel() {
    if (!globalMacrosPanel_ || selectedTrackId_ == magda::INVALID_TRACK_ID)
        return;
    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (!track)
        return;

    // Collect all devices (flat list across devices and racks) for the link menu
    std::vector<std::pair<magda::DeviceId, juce::String>> allDevices;
    std::map<magda::DeviceId, std::vector<juce::String>> allDeviceParams;

    std::function<void(const std::vector<magda::ChainElement>&)> collectDevices;
    collectDevices = [&](const std::vector<magda::ChainElement>& elements) {
        for (const auto& element : elements) {
            if (magda::isDevice(element)) {
                const auto& device = magda::getDevice(element);
                allDevices.push_back({device.id, device.name});
                std::vector<juce::String> names;
                names.reserve(device.parameters.size());
                for (const auto& p : device.parameters) {
                    names.push_back(p.name);
                }
                allDeviceParams[device.id] = std::move(names);
            } else {
                const auto& rack = magda::getRack(element);
                for (const auto& chain : rack.chains) {
                    collectDevices(chain.elements);
                }
            }
        }
    };
    collectDevices(track->chain.fxChainElements);

    auto trackPath = magda::ChainNodePath::trackLevel(selectedTrackId_);
    globalMacrosPanel_->setParentPath(trackPath);
    globalMacrosPanel_->setAvailableDevices(allDevices);
    globalMacrosPanel_->setDeviceParamNames(allDeviceParams);

    // Track-level macros can target track-level modifiers — populate the
    // "Modulators" submenu in the link picker with the same-scope mods.
    std::vector<std::pair<magda::ModId, juce::String>> trackMods;
    trackMods.reserve(track->mods.size());
    for (const auto& m : track->mods)
        if (m.enabled)
            trackMods.emplace_back(m.id, magda::getModDisplayName(m));
    globalMacrosPanel_->setAvailableModifiers(trackMods);

    globalMacrosPanel_->setMacros(track->macros);

    // Update editor if visible
    if (globalMacroEditorVisible_ && globalMacroEditorPanel_ && selectedGlobalMacroIndex_ >= 0 &&
        selectedGlobalMacroIndex_ < static_cast<int>(track->macros.size())) {
        globalMacroEditorPanel_->setMacroInfo(track->macros[selectedGlobalMacroIndex_]);
    }
}

void TrackChainContent::showGlobalModEditor(int modIndex) {
    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (!track || modIndex < 0 || modIndex >= static_cast<int>(track->mods.size()))
        return;

    selectedGlobalModIndex_ = modIndex;
    globalModEditorVisible_ = true;
    if (auto* t = magda::TrackManager::getInstance().getTrack(selectedTrackId_))
        t->selectedGlobalModIndex = modIndex;
    auto trackId = selectedTrackId_;
    // OwnerPath drives the rate-slider's link-mode scope check + the
    // automation target — pass the track-level path so cross-scope macros/mods
    // can drive this LFO's rate via the click-to-link flow.
    globalModEditorPanel_->setOwnerPath(selectedTrackId_,
                                        magda::ChainNodePath::trackLevel(selectedTrackId_));
    globalModEditorPanel_->setSelectedModIndex(modIndex);
    globalModEditorPanel_->setModInfo(track->mods[modIndex], &track->mods[modIndex],
                                      [trackId, modIndex]() -> const magda::ModInfo* {
                                          const auto* t =
                                              magda::TrackManager::getInstance().getTrack(trackId);
                                          if (!t || modIndex >= static_cast<int>(t->mods.size()))
                                              return nullptr;
                                          return &t->mods[modIndex];
                                      });
    globalModEditorPanel_->setVisible(true);
    resized();
}

void TrackChainContent::hideGlobalModEditor() {
    bool wasVisible = globalModEditorVisible_;
    selectedGlobalModIndex_ = -1;
    globalModEditorVisible_ = false;
    if (auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_))
        track->selectedGlobalModIndex = -1;
    if (globalModEditorPanel_)
        globalModEditorPanel_->setVisible(false);
    if (wasVisible)
        resized();
}

void TrackChainContent::showGlobalMacroEditor(int macroIndex) {
    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (!track || macroIndex < 0 || macroIndex >= static_cast<int>(track->macros.size()))
        return;

    selectedGlobalMacroIndex_ = macroIndex;
    globalMacroEditorVisible_ = true;
    if (auto* t = magda::TrackManager::getInstance().getTrack(selectedTrackId_))
        t->selectedGlobalMacroIndex = macroIndex;
    globalMacroEditorPanel_->setMacroInfo(track->macros[macroIndex]);
    globalMacroEditorPanel_->setVisible(true);
    resized();
}

void TrackChainContent::hideGlobalMacroEditor() {
    bool wasVisible = globalMacroEditorVisible_;
    selectedGlobalMacroIndex_ = -1;
    globalMacroEditorVisible_ = false;
    if (auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_))
        track->selectedGlobalMacroIndex = -1;
    if (globalMacroEditorPanel_)
        globalMacroEditorPanel_->setVisible(false);
    if (wasVisible)
        resized();
}

void TrackChainContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
        auto bounds = getLocalBounds();

        // Draw panel area borders (panels are child components, they paint themselves)
        int panelAreaWidth = 0;
        if (globalMacrosVisible_)
            panelAreaWidth += MODS_PANEL_WIDTH;
        if (globalMacroEditorVisible_)
            panelAreaWidth += ModulatorEditorPanel::PREFERRED_WIDTH;
        if (globalModsVisible_)
            panelAreaWidth += MODS_PANEL_WIDTH;
        if (globalModEditorVisible_)
            panelAreaWidth += ModulatorEditorPanel::PREFERRED_WIDTH;

        if (panelAreaWidth > 0) {
            // Vertical separator between panels and chain content
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawVerticalLine(panelAreaWidth, static_cast<float>(bounds.getY()),
                               static_cast<float>(bounds.getBottom()));
        }

        // Arrows between chain elements are drawn by ChainContainer::paint
        // which scrolls correctly with the viewport
    }
}

void TrackChainContent::mouseDown(const juce::MouseEvent& e) {
    // Alt/Option + click = start zoom drag
    if (e.mods.isAltDown()) {
        isZoomDragging_ = true;
        zoomDragStartX_ = e.x;
        zoomStartLevel_ = zoomLevel_;
    }
}

void TrackChainContent::mouseDrag(const juce::MouseEvent& e) {
    if (isZoomDragging_) {
        // Drag right = zoom in, drag left = zoom out
        int deltaX = e.x - zoomDragStartX_;
        float zoomDelta = deltaX * 0.005f;  // Sensitivity factor
        setZoomLevel(zoomStartLevel_ + zoomDelta);
    }
}

void TrackChainContent::mouseUp(const juce::MouseEvent&) {
    isZoomDragging_ = false;
}

void TrackChainContent::mouseWheelMove(const juce::MouseEvent& e,
                                       const juce::MouseWheelDetails& wheel) {
    // Alt/Option + scroll wheel = zoom
    if (e.mods.isAltDown()) {
        float delta = wheel.deltaY > 0 ? ZOOM_STEP : -ZOOM_STEP;
        setZoomLevel(zoomLevel_ + delta);
    } else {
        // Forward to viewport for scrolling
        chainViewport_->mouseWheelMove(e, wheel);
    }
}

void TrackChainContent::resized() {
    auto bounds = getLocalBounds();

    if (aiReasoningOverlay_ != nullptr)
        aiReasoningOverlay_->setBounds(getLocalBounds());

    if (selectedTrackId_ == magda::INVALID_TRACK_ID) {
        noSelectionLabel_.setBounds(bounds);
        hideHeaderControls();
    } else {
        noSelectionLabel_.setVisible(false);

        // === GLOBAL PANELS (left side: macros | macro editor | mods | mod editor) ===
        if (globalMacrosVisible_ && globalMacrosPanel_) {
            globalMacrosPanel_->setBounds(bounds.removeFromLeft(MODS_PANEL_WIDTH));
        }
        if (globalMacroEditorVisible_ && globalMacroEditorPanel_) {
            globalMacroEditorPanel_->setBounds(
                bounds.removeFromLeft(ModulatorEditorPanel::PREFERRED_WIDTH));
        }
        if (globalModsVisible_ && globalModsPanel_) {
            globalModsPanel_->setBounds(bounds.removeFromLeft(MODS_PANEL_WIDTH));
        }
        if (globalModEditorVisible_ && globalModEditorPanel_) {
            globalModEditorPanel_->setBounds(
                bounds.removeFromLeft(ModulatorEditorPanel::PREFERRED_WIDTH));
        }

        // === CONTENT AREA LAYOUT ===
        // Everything flows horizontally: [Device] → [Device] → [Rack] → [Rack] → ...
        // ChainPanel is displayed within the rack when a chain is selected
        auto contentArea = bounds.reduced(8);

        // Viewport fills the content area
        chainViewport_->setBounds(contentArea);

        // Layout chain content inside the container
        layoutChainContent();
    }
}

void TrackChainContent::layoutChainContent() {
    auto viewportBounds = chainViewport_->getLocalBounds();
    int viewportHeight = viewportBounds.getHeight();
    int availableWidth = viewportBounds.getWidth();

    // Enforce minimum content height — viewport scrolls vertically if panel is too short
    int chainHeight = juce::jmax(MIN_CHAIN_HEIGHT, viewportHeight);

    // Calculate total content width (with zoom applied)
    int totalWidth = calculateTotalContentWidth();

    // Account for scrollbar if needed
    if (totalWidth > availableWidth) {
        chainHeight = juce::jmax(MIN_CHAIN_HEIGHT, chainHeight - 8);  // Space for scrollbar
    }

    // Set container size
    chainContainer_->setSize(juce::jmax(totalWidth, availableWidth), chainHeight);
    chainContainer_->setNodeComponents(&nodeComponents_);

    // Add left padding during drag/drop to show insertion indicator before first node
    bool isDraggingOrDropping = dragOriginalIndex_ >= 0 || dropInsertIndex_ >= 0;
    int scaledArrowWidth = getScaledWidth(ARROW_WIDTH);
    int scaledSlotSpacing = getScaledWidth(SLOT_SPACING);
    int x = isDraggingOrDropping ? getScaledWidth(DRAG_LEFT_PADDING) : 0;

    // Layout all node components horizontally (with zoom applied)
    for (auto& node : nodeComponents_) {
        // Check if it's a RackComponent to set available width
        if (auto* rack = dynamic_cast<RackComponent*>(node.get())) {
            int remainingWidth =
                juce::jmax(0, availableWidth - x - scaledArrowWidth - scaledSlotSpacing);
            rack->setAvailableWidth(remainingWidth);
        }

        int nodeWidth = getScaledWidth(node->getPreferredWidth());
        node->setBounds(x, 0, nodeWidth, chainHeight);
        x += nodeWidth + scaledArrowWidth + scaledSlotSpacing;
    }

    // Append "+" lives in the append zone, which is pinned to the right edge
    // of the container (see calculateAppendZoneX) rather than trailing the last
    // node, so it sits all the way to the right.
    const int appendZoneWidth = getScaledWidth(APPEND_ZONE_WIDTH);
    const int appendX = calculateAppendZoneX();
    constexpr int buttonSize = 20;
    addDeviceButton_.setVisible(selectedTrackId_ != magda::INVALID_TRACK_ID);
    addDeviceButton_.setBounds(appendX + juce::jmax(0, (appendZoneWidth - buttonSize) / 2),
                               juce::jmax(0, (chainHeight - buttonSize) / 2), buttonSize,
                               buttonSize);
    addDeviceButton_.toFront(false);
}

int TrackChainContent::calculateTotalContentWidth() const {
    // Add left padding during drag/drop to show insertion indicator before first node
    bool isDraggingOrDropping = dragOriginalIndex_ >= 0 || dropInsertIndex_ >= 0;
    int scaledArrowWidth = getScaledWidth(ARROW_WIDTH);
    int scaledSlotSpacing = getScaledWidth(SLOT_SPACING);
    int totalWidth = isDraggingOrDropping ? getScaledWidth(DRAG_LEFT_PADDING) : 0;

    // Add width for all node components (with zoom applied)
    for (const auto& node : nodeComponents_) {
        totalWidth +=
            getScaledWidth(node->getPreferredWidth()) + scaledArrowWidth + scaledSlotSpacing;
    }

    totalWidth += getScaledWidth(APPEND_ZONE_WIDTH);

    return totalWidth;
}

int TrackChainContent::getOptimalPanelHeight(int windowHeight) const {
    // Check whether the currently selected device has a custom UI that
    // benefits from more vertical space (chord engine, step sequencer,
    // sampler, etc.). Otherwise use the default 1/3 height.
    if (selectedDeviceId_ != magda::INVALID_DEVICE_ID &&
        selectedTrackId_ != magda::INVALID_TRACK_ID) {
        if (auto* dev =
                magda::TrackManager::getInstance().getDevice(selectedTrackId_, selectedDeviceId_)) {
            auto pid = dev->pluginId.toLowerCase();
            if (pid.contains("chord") || pid.contains("step_seq") || pid.contains("sampler") ||
                pid.contains("4osc")) {
                return windowHeight / 2;
            }
        }
    }
    return juce::jmax(360, windowHeight / 3);
}

void TrackChainContent::onActivated() {
    selectedTrackId_ = magda::TrackManager::getInstance().getSelectedTrack();
    updateFromSelectedTrack();
}

void TrackChainContent::onDeactivated() {
    // Nothing to do
}

void TrackChainContent::tracksChanged() {
    if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
        const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
        if (!track)
            selectedTrackId_ = magda::INVALID_TRACK_ID;
    }
    updateFromSelectedTrack();
}

void TrackChainContent::trackPropertyChanged(int trackId) {
    if (static_cast<magda::TrackId>(trackId) == selectedTrackId_) {
        // Only update header widgets — don't rebuild device components,
        // as the device chain hasn't changed and rebuilding destroys
        // transient UI state (e.g. AI results in step sequencer).
        const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
        if (track) {
            trackNameLabel_.setText(track->name, juce::dontSendNotification);
            muteButton_.setToggleState(track->muted, juce::dontSendNotification);
            syncMasterSpeakerButton(masterMuteButton_, track->muted);
            chordSpeakerButton_->setActive(!track->muted);
            soloButton_.setToggleState(track->soloed, juce::dontSendNotification);
            volumeLabel_.setValue(gainToDb(track->volume), juce::dontSendNotification);
            panLabel_.setValue(track->pan, juce::dontSendNotification);
        }
    }
}

void TrackChainContent::trackSelectionChanged(magda::TrackId trackId) {
    selectedTrackId_ = trackId;
    // Each track has its own "current preset" affordance — clearing on
    // selection change ensures the save-overwrite item won't appear with the
    // previous track's preset name.
    currentPresetName_.clear();
    updateFromSelectedTrack();
}

void TrackChainContent::trackDevicesChanged(magda::TrackId trackId) {
    if (trackId == selectedTrackId_) {
        const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
        const int previousElementCount = static_cast<int>(nodeComponents_.size());
        const int nextElementCount =
            track ? static_cast<int>(track->chain.fxChainElements.size()) : previousElementCount;
        const bool shouldScrollToEnd =
            scrollToEndAfterNextDeviceChange_ ||
            (!suppressNextImplicitScrollToEnd_ && nextElementCount > previousElementCount);
        scrollToEndAfterNextDeviceChange_ = false;
        suppressNextImplicitScrollToEnd_ = false;

        rebuildNodeComponents();
        updateGlobalModsPanel();
        updateGlobalMacrosPanel();
        refreshAnalysisToggles();  // post-fx contents may have changed

        if (shouldScrollToEnd)
            scrollToEndAsync();
    }
}

void TrackChainContent::togglePostFxAnalysisDevice(const juce::String& pluginId,
                                                   const juce::String& displayName) {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID)
        return;

    auto& tm = magda::TrackManager::getInstance();
    const magda::DeviceId existing = tm.findPostFxDevice(selectedTrackId_, pluginId);
    if (existing != magda::INVALID_DEVICE_ID) {
        tm.removeDeviceFromChainByPath(
            magda::ChainNodePath::postFxDevice(selectedTrackId_, existing));
    } else {
        magda::DeviceInfo device;
        device.name = displayName;
        device.manufacturer = "MAGDA";
        device.pluginId = pluginId;
        device.deviceType = magda::DeviceType::Analysis;
        device.format = magda::PluginFormat::Internal;
        tm.addDeviceToPostFx(selectedTrackId_, device);
        // Reveal the panel so the analyzer you just added is visible.
        if (onPostFxPanelToggled)
            onPostFxPanelToggled(true);
    }
    // trackDevicesChanged() fires from the mutations above and refreshes the
    // toggles, but refresh here too so the lit state updates even if this panel
    // is not the listener that drives the rebuild.
    refreshAnalysisToggles();
}

void TrackChainContent::setPostFxPanelOpen(bool open) {
    postFxPanelOpen_ = open;
    if (postFxPanelButton_)
        postFxPanelButton_->setActive(open);
}

void TrackChainContent::refreshAnalysisToggles() {
    if (!oscToggleButton_ || !specToggleButton_ || !levelsToggleButton_)
        return;
    auto& tm = magda::TrackManager::getInstance();
    const bool hasTrack = selectedTrackId_ != magda::INVALID_TRACK_ID;
    oscToggleButton_->setActive(hasTrack && tm.findPostFxDevice(selectedTrackId_, "oscilloscope") !=
                                                magda::INVALID_DEVICE_ID);
    specToggleButton_->setActive(hasTrack &&
                                 tm.findPostFxDevice(selectedTrackId_, "spectrumanalyzer") !=
                                     magda::INVALID_DEVICE_ID);
    levelsToggleButton_->setActive(hasTrack && tm.findPostFxDevice(selectedTrackId_, "levels") !=
                                                   magda::INVALID_DEVICE_ID);
}

void TrackChainContent::modulationNamesChanged(magda::TrackId trackId) {
    if (trackId != selectedTrackId_)
        return;

    refreshVisibleModulationPanels();
}

void TrackChainContent::deviceModifiersChanged(magda::TrackId trackId) {
    if (trackId != selectedTrackId_)
        return;

    refreshVisibleModulationPanels();
}

void TrackChainContent::refreshVisibleModulationPanels() {
    updateGlobalModsPanel();
    updateGlobalMacrosPanel();
    for (auto& node : nodeComponents_) {
        if (node)
            node->refreshPanels();
    }
    resized();
    repaint();
}

void TrackChainContent::macroValueChanged(magda::TrackId trackId, magda::ChainScope scope,
                                          int ownerId, int macroIndex, float value) {
    if (trackId != selectedTrackId_)
        return;

    // Targeted single-knob update per scope — avoid the full updateMacroPanel
    // rebuild (setMacros + setAvailableDevices + setDeviceParamNames) that was
    // congesting the message thread under high-rate controller writes.
    switch (scope) {
        case magda::ChainScope::Track:
            // Track-level macros live on globalMacrosPanel_; the device
            // panels' knobs must NOT be updated here (TrackId and DeviceId
            // namespaces overlap, and the old isRack-based dispatch wrongly
            // forwarded track writes to a numerically-matching device knob).
            if (globalMacrosPanel_)
                globalMacrosPanel_->updateMacroValueDisplay(macroIndex, value);
            return;
        case magda::ChainScope::Rack:
            for (auto& node : nodeComponents_) {
                if (node && node->getNodePath().getRackId() == ownerId)
                    node->updateMacroValueDisplay(macroIndex, value);
            }
            return;
        case magda::ChainScope::Device: {
            const magda::DeviceId deviceId = ownerId;
            for (auto& node : nodeComponents_) {
                if (node && node->getNodePath().getDeviceId() == deviceId)
                    node->updateMacroValueDisplay(macroIndex, value);
            }
            return;
        }
    }
}

void TrackChainContent::selectionTypeChanged(magda::SelectionType /*newType*/) {
    // Repaint header when selection type changes (Track vs ChainNode)
    // to update the header background color
    repaint();
}

void TrackChainContent::modLinkModeChanged(bool active, const magda::ModSelection& /*selection*/) {
    linkModeLabel_.setVisible(active);
    if (active) {
        linkModeLabel_.setColour(juce::Label::textColourId,
                                 DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    }
    resized();
}

void TrackChainContent::macroLinkModeChanged(bool active,
                                             const magda::MacroSelection& /*selection*/) {
    linkModeLabel_.setVisible(active);
    if (active) {
        linkModeLabel_.setColour(juce::Label::textColourId,
                                 DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    }
    resized();
}

void TrackChainContent::gainStagingModeChanged(magda::GainStagingMode /*mode*/,
                                               magda::TrackId /*trackId*/) {
    refreshGainStagingButton();
    resized();
}

void TrackChainContent::refreshGainStagingButton() {
    if (!gainStagingButton_)
        return;

    auto& gs = magda::GainStagingManager::getInstance();
    // Only treat the button as engaged when the active pass is on the track
    // currently shown in this header.
    const bool onThisTrack = gs.getActiveTrack() == selectedTrackId_;
    const bool collecting = gs.getMode() == magda::GainStagingMode::Collecting && onThisTrack;

    const auto yellow = DarkTheme::getColour(DarkTheme::STATUS_WARNING);

    if (aiProcessing_) {
        gainStagingButton_->setActiveBackgroundColor(yellow.withAlpha(0.20f));
        gainStagingButton_->setActiveBorderColor(yellow);
        gainStagingButton_->setTooltip("Gain staging: AI is analysing the chain...");
    } else if (collecting) {
        const auto red = DarkTheme::getColour(DarkTheme::STATUS_DANGER);
        gainStagingButton_->setActiveBackgroundColor(red.withAlpha(0.20f));
        gainStagingButton_->setActiveBorderColor(red);
        gainStagingButton_->setTooltip("Gain staging: stop and apply");
    } else {
        gainStagingButton_->setTooltip("Gain staging: capture levels, then auto-set device gains");
    }

    gainStagingButton_->setActive(aiProcessing_ || collecting);

    // Centered banner: blinking "GETTING AI RESULTS" while the agent runs (the
    // reasoning is shown in the overlay afterwards), "GAIN STAGING" while
    // capturing. Position it to fill the header bar each time it's shown -- the
    // header only re-lays-out on a BottomPanel resize, which doesn't fire when
    // the pass starts.
    const bool bannerVisible = aiProcessing_ || collecting;
    gainStagingLabel_.setVisible(bannerVisible);
    if (bannerVisible)
        if (auto* parent = gainStagingLabel_.getParentComponent())
            gainStagingLabel_.setBounds(parent->getLocalBounds());
    if (aiProcessing_) {
        gainStagingLabel_.setText("GETTING AI RESULTS", juce::dontSendNotification);
        gainStagingLabel_.setColour(juce::Label::textColourId,
                                    yellow.withAlpha(aiBlinkOn_ ? 1.0f : 0.25f));
    } else if (collecting) {
        gainStagingLabel_.setText("GAIN STAGING", juce::dontSendNotification);
        gainStagingLabel_.setColour(juce::Label::textColourId,
                                    DarkTheme::getColour(DarkTheme::STATUS_DANGER));
    }
}

void TrackChainContent::runAiGainStagingPass() {
    auto& gsm = magda::GainStagingManager::getInstance();
    auto snapshot = gsm.finishCaptureForAi();
    if (snapshot.empty()) {
        gsm.reset();
        return;
    }
    const float target = gsm.getTargetDb();

    // Map the manager snapshot onto the agent's input. The agent identifies
    // devices by list index (DeviceId is section-local and not unique), so keep
    // a parallel paths_ vector to translate decisions back to a concrete device.
    std::vector<magda::GainStagingAgent::DeviceLevel> levels;
    std::vector<magda::ChainNodePath> paths;
    levels.reserve(snapshot.size());
    paths.reserve(snapshot.size());
    for (const auto& s : snapshot) {
        magda::GainStagingAgent::DeviceLevel lvl;
        lvl.name = s.name.toStdString();
        lvl.pluginId = s.pluginId.toStdString();
        lvl.isInstrument = s.isInstrument;
        lvl.capturedPeakDb = s.capturedPeakDb;
        lvl.currentGainDb = s.currentGainDb;
        lvl.suggestedGainDb = s.suggestedGainDb;
        for (const auto& p : s.params)
            lvl.params.push_back({p.name.toStdString(), (double)p.value, p.unit.toStdString()});
        levels.push_back(std::move(lvl));
        paths.push_back(s.path);
    }

    aiProcessing_ = true;
    aiReasoningOverlay_.reset();  // drop any stale panel
    aiBlinkOn_ = true;
    aiBlinkTimer_.startTimerHz(2);  // ~2 Hz blink while thinking
    refreshGainStagingButton();

    // The LLM call blocks; run it off the message thread and apply on return.
    juce::Component::SafePointer<TrackChainContent> safe(this);
    std::thread([safe, levels = std::move(levels), paths = std::move(paths), target]() {
        magda::GainStagingAgent agent;
        auto result = agent.generate(target, levels);

        // Build the human-readable reasoning while we still have device names.
        juce::String reasoning;
        if (result.hasError) {
            reasoning = "AI gain staging failed:\n" + juce::String(result.error);
        } else {
            if (!result.summary.empty())
                reasoning << juce::String(result.summary) << "\n\n";
            for (const auto& d : result.decisions) {
                juce::String name;
                if (d.index >= 0 && d.index < (int)levels.size())
                    name = juce::String(levels[(size_t)d.index].name);
                reasoning << name << ":  " << (d.newGainDb >= 0.0f ? "+" : "")
                          << juce::String(d.newGainDb, 1) << " dB";
                if (!d.reason.empty())
                    reasoning << "   " << juce::String(d.reason);
                reasoning << "\n";
            }
        }

        juce::MessageManager::callAsync([safe, result, reasoning, paths]() {
            if (safe == nullptr)
                return;
            safe->aiProcessing_ = false;
            safe->aiBlinkTimer_.stopTimer();

            auto& m = magda::GainStagingManager::getInstance();
            if (result.hasError) {
                juce::Logger::writeToLog("[GainStaging] AI error: " + juce::String(result.error));
                m.reset();  // drop the frozen capture
            } else {
                std::vector<std::pair<magda::ChainNodePath, float>> moves;
                for (const auto& d : result.decisions)
                    if (d.index >= 0 && d.index < (int)paths.size())
                        moves.push_back({paths[(size_t)d.index], d.newGainDb});
                m.applyAiMoves(moves);
                juce::Logger::writeToLog("[GainStaging] AI: " + juce::String(result.summary));
            }

            safe->refreshGainStagingButton();
            safe->showAiReasoning(reasoning);
        });
    }).detach();
}

namespace {
// Transient panel that prints the agent's reasoning over the chain; any click
// anywhere on it dismisses it.
class AiReasoningOverlay : public juce::Component {
  public:
    std::function<void()> onDismiss;

    void setText(juce::String text) {
        text_ = std::move(text);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black.withAlpha(0.55f));

        auto area = getLocalBounds().reduced(24);
        if (area.getWidth() > 560)
            area = area.withSizeKeepingCentre(560, area.getHeight());

        g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        g.fillRoundedRectangle(area.toFloat(), 8.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        g.drawRoundedRectangle(area.toFloat(), 8.0f, 1.5f);

        auto inner = area.reduced(16);
        auto titleArea = inner.removeFromTop(22);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        g.setFont(FontManager::getInstance().getUIFontBold(15.0f));
        g.drawText("AI gain staging", titleArea, juce::Justification::topLeft);

        auto hintArea = inner.removeFromBottom(18);
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText("click to dismiss", hintArea, juce::Justification::topRight);

        g.setColour(DarkTheme::getTextColour());
        g.setFont(FontManager::getInstance().getUIFont(13.0f));
        g.drawFittedText(text_, inner, juce::Justification::topLeft, 40);
    }

    void mouseDown(const juce::MouseEvent&) override {
        if (onDismiss)
            onDismiss();
    }

  private:
    juce::String text_;
};
}  // namespace

void TrackChainContent::showAiReasoning(const juce::String& text) {
    if (text.isEmpty())
        return;

    auto overlay = std::make_unique<AiReasoningOverlay>();
    overlay->setText(text);
    juce::Component::SafePointer<TrackChainContent> safe(this);
    overlay->onDismiss = [safe]() {
        if (safe != nullptr)
            safe->aiReasoningOverlay_.reset();
    };
    addAndMakeVisible(*overlay);
    overlay->setBounds(getLocalBounds());
    overlay->toFront(false);
    aiReasoningOverlay_ = std::move(overlay);
}

void TrackChainContent::updateFromSelectedTrack() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID) {
        hideHeaderControls();
        noSelectionLabel_.setVisible(true);
        nodeComponents_.clear();
    } else {
        const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
        if (track) {
            trackNameLabel_.setText(track->name, juce::dontSendNotification);

            // Update mute/solo state
            muteButton_.setToggleState(track->muted, juce::dontSendNotification);
            syncMasterSpeakerButton(masterMuteButton_, track->muted);
            soloButton_.setToggleState(track->soloed, juce::dontSendNotification);

            // Convert linear gain to dB for volume slider
            float db = gainToDb(track->volume);
            volumeLabel_.setValue(db, juce::dontSendNotification);

            // Update pan slider
            panLabel_.setValue(track->pan, juce::dontSendNotification);

            // Bind automation targets so these labels mirror the track
            // header's purple/grey state via the AutomationManager observer.
            magda::AutomationTarget volTarget;
            volTarget.kind = magda::ControlTarget::Kind::TrackVolume;
            volTarget.devicePath = magda::ChainNodePath::trackLevel(selectedTrackId_);
            volumeLabel_.setAutomationTarget(volTarget);
            magda::AutomationTarget panTarget;
            panTarget.kind = magda::ControlTarget::Kind::TrackPan;
            panTarget.devicePath = magda::ChainNodePath::trackLevel(selectedTrackId_);
            panLabel_.setAutomationTarget(panTarget);

            // Check if any device in the chain is not bypassed
            const auto& elements =
                magda::TrackManager::getInstance().getChainElements(selectedTrackId_);
            bool anyActive = elements.empty();  // Empty chain = active (not bypassed)
            for (const auto& element : elements) {
                if (magda::isDevice(element) && !magda::getDevice(element).bypassed) {
                    anyActive = true;
                    break;
                }
                if (magda::isRack(element) && !magda::getRack(element).bypassed) {
                    anyActive = true;
                    break;
                }
            }
            chainBypassButton_->setToggleState(anyActive, juce::dontSendNotification);
            chainBypassButton_->setActive(anyActive);

            const bool isMaster = track->type == magda::TrackType::Master;
            const bool isChord = track->type == magda::TrackType::Chord;

            // Chord audition speaker + monitor mirror the chord track header.
            chordSpeakerButton_->setActive(!track->muted);
            switch (track->inputMonitor) {
                case magda::InputMonitorMode::In:
                    monitorButton_.setButtonText("I");
                    break;
                case magda::InputMonitorMode::Auto:
                    monitorButton_.setButtonText("A");
                    break;
                default:
                    monitorButton_.setButtonText("-");
                    break;
            }
            monitorButton_.setToggleState(track->inputMonitor != magda::InputMonitorMode::Off,
                                          juce::dontSendNotification);

            // Left chain action buttons + post-fx / analysis toggles are hidden
            // for the chord track (no modulation / macros / racks / analysis).
            const bool showChainTools = !isChord;
            globalModsButton_->setVisible(showChainTools);
            macroButton_->setVisible(showChainTools);
            addRackButton_->setVisible(showChainTools);
            treeViewButton_->setVisible(showChainTools);
            presetButton_->setVisible(showChainTools);
            gainStagingButton_->setVisible(showChainTools);
            postFxPanelButton_->setVisible(showChainTools);
            oscToggleButton_->setVisible(showChainTools);
            specToggleButton_->setVisible(showChainTools);
            levelsToggleButton_->setVisible(showChainTools);
            if (showChainTools) {
                refreshAnalysisToggles();
                refreshGainStagingButton();
            }
            trackNameLabel_.setVisible(true);
            soloButton_.setVisible(!isMaster);
            volumeLabel_.setVisible(true);
            panLabel_.setVisible(!isMaster && !isChord);
            chainBypassButton_->setVisible(!isMaster && !isChord);

            muteButton_.setVisible(!isMaster && !isChord);
            masterMuteButton_.setVisible(isMaster);
            chordSpeakerButton_->setVisible(isChord);
            monitorButton_.setVisible(isChord);

            noSelectionLabel_.setVisible(false);
            rebuildNodeComponents();

            // Update global mods/macros panels
            updateGlobalModsPanel();
            updateGlobalMacrosPanel();

            // Restore panel visibility from track state
            globalModsVisible_ = track->globalModsPanelOpen;
            globalModsButton_->setToggleState(globalModsVisible_, juce::dontSendNotification);
            globalModsButton_->setActive(globalModsVisible_);
            if (globalModsPanel_)
                globalModsPanel_->setVisible(globalModsVisible_);

            globalMacrosVisible_ = track->globalMacrosPanelOpen;
            macroButton_->setToggleState(globalMacrosVisible_, juce::dontSendNotification);
            macroButton_->setActive(globalMacrosVisible_);
            if (globalMacrosPanel_)
                globalMacrosPanel_->setVisible(globalMacrosVisible_);

            // Restore editor state
            if (globalModsVisible_ && track->selectedGlobalModIndex >= 0)
                showGlobalModEditor(track->selectedGlobalModIndex);
            else
                hideGlobalModEditor();

            if (globalMacrosVisible_ && track->selectedGlobalMacroIndex >= 0)
                showGlobalMacroEditor(track->selectedGlobalMacroIndex);
            else
                hideGlobalMacroEditor();
        } else {
            hideHeaderControls();
            noSelectionLabel_.setVisible(true);
            nodeComponents_.clear();
        }
    }

    resized();
    repaint();
}

void TrackChainContent::populateHeader(juce::Component& headerBar) {
    // Reparent all header controls into the centralised header bar
    headerBar.addAndMakeVisible(globalModsButton_.get());
    headerBar.addAndMakeVisible(macroButton_.get());
    headerBar.addAndMakeVisible(addRackButton_.get());
    headerBar.addAndMakeVisible(treeViewButton_.get());
    headerBar.addAndMakeVisible(presetButton_.get());
    headerBar.addAndMakeVisible(gainStagingButton_.get());
    headerBar.addAndMakeVisible(postFxPanelButton_.get());
    headerBar.addAndMakeVisible(oscToggleButton_.get());
    headerBar.addAndMakeVisible(specToggleButton_.get());
    headerBar.addAndMakeVisible(levelsToggleButton_.get());
    headerBar.addAndMakeVisible(trackNameLabel_);
    const auto* selTrack = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    const bool isMaster = selTrack && selTrack->type == magda::TrackType::Master;
    if (isMaster) {
        headerBar.addChildComponent(muteButton_);
        headerBar.addAndMakeVisible(masterMuteButton_);
    } else {
        headerBar.addAndMakeVisible(muteButton_);
        headerBar.addChildComponent(masterMuteButton_);
    }
    headerBar.addAndMakeVisible(soloButton_);
    headerBar.addChildComponent(*chordSpeakerButton_);
    headerBar.addChildComponent(monitorButton_);
    headerBar.addAndMakeVisible(volumeLabel_);
    headerBar.addAndMakeVisible(panLabel_);
    headerBar.addAndMakeVisible(chainBypassButton_.get());
    headerBar.addChildComponent(linkModeLabel_);
    headerBar.addChildComponent(gainStagingLabel_);

    // If no track selected, hide controls
    if (selectedTrackId_ == magda::INVALID_TRACK_ID)
        hideHeaderControls();
}

void TrackChainContent::depopulateHeader(juce::Component& /*headerBar*/) {
    // Reparent back to this content (hidden)
    addChildComponent(globalModsButton_.get());
    addChildComponent(macroButton_.get());
    addChildComponent(addRackButton_.get());
    addChildComponent(treeViewButton_.get());
    addChildComponent(presetButton_.get());
    addChildComponent(gainStagingButton_.get());
    addChildComponent(postFxPanelButton_.get());
    addChildComponent(oscToggleButton_.get());
    addChildComponent(specToggleButton_.get());
    addChildComponent(levelsToggleButton_.get());
    addChildComponent(&trackNameLabel_);
    addChildComponent(&muteButton_);
    addChildComponent(&masterMuteButton_);
    addChildComponent(&soloButton_);
    addChildComponent(*chordSpeakerButton_);
    addChildComponent(&monitorButton_);
    addChildComponent(&volumeLabel_);
    addChildComponent(&panLabel_);
    addChildComponent(chainBypassButton_.get());
    addChildComponent(&linkModeLabel_);
    addChildComponent(&gainStagingLabel_);
}

void TrackChainContent::layoutHeader(juce::Rectangle<int> headerBounds) {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID)
        return;

    // Chord track: no left chain tools; the right side mirrors the chord track
    // header (volume + audition speaker + solo + monitor). Name fills the rest.
    if (const auto* chordTrack = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
        chordTrack && chordTrack->type == magda::TrackType::Chord) {
        auto a = headerBounds.reduced(8, 4);
        const int gap = 4;
        monitorButton_.setBounds(a.removeFromRight(18));
        a.removeFromRight(gap);
        soloButton_.setBounds(a.removeFromRight(18));
        a.removeFromRight(gap);
        chordSpeakerButton_->setBounds(a.removeFromRight(22).withSizeKeepingCentre(22, 22));
        a.removeFromRight(gap);
        volumeLabel_.setBounds(a.removeFromRight(60));
        a.removeFromRight(8);
        trackNameLabel_.setBounds(a);

        if (linkModeLabel_.isVisible())
            linkModeLabel_.setBounds(headerBounds);
        if (gainStagingLabel_.isVisible())
            gainStagingLabel_.setBounds(headerBounds);
        return;
    }

    auto headerArea = headerBounds.reduced(8, 4);

    // LEFT SIDE - Action buttons
    macroButton_->setBounds(headerArea.removeFromLeft(20));
    headerArea.removeFromLeft(2);
    globalModsButton_->setBounds(headerArea.removeFromLeft(20));
    headerArea.removeFromLeft(8);
    addRackButton_->setBounds(headerArea.removeFromLeft(20));
    headerArea.removeFromLeft(4);
    treeViewButton_->setBounds(headerArea.removeFromLeft(20));
    headerArea.removeFromLeft(8);
    // Track-chain presets button — sits on the LEFT of the header (devices
    // and racks have theirs on the right inside their own node header).
    presetButton_->setBounds(headerArea.removeFromLeft(20));
    headerArea.removeFromLeft(8);
    gainStagingButton_->setBounds(headerArea.removeFromLeft(20));

    // RIGHT SIDE - Track info (from right to left)
    const auto* selTrack = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    bool isMaster = selTrack && selTrack->type == magda::TrackType::Master;

    if (!isMaster) {
        chainBypassButton_->setBounds(headerArea.removeFromRight(17));
        headerArea.removeFromRight(4);
    }
    if (!isMaster) {
        panLabel_.setBounds(headerArea.removeFromRight(30));
        headerArea.removeFromRight(4);
    }
    volumeLabel_.setBounds(headerArea.removeFromRight(60));
    headerArea.removeFromRight(4);
    if (!isMaster) {
        soloButton_.setBounds(headerArea.removeFromRight(18));
        headerArea.removeFromRight(2);
    }
    if (isMaster) {
        masterMuteButton_.setBounds(headerArea.removeFromRight(24).withSizeKeepingCentre(24, 24));
        masterMuteButton_.setVisible(true);
        muteButton_.setVisible(false);
    } else {
        muteButton_.setBounds(headerArea.removeFromRight(18));
        muteButton_.setVisible(true);
        masterMuteButton_.setVisible(false);
    }
    headerArea.removeFromRight(8);
    // Post-FX panel toggle + analysis-device toggles — grouped with the track's
    // output controls (solo/mute/volume) rather than the left chain buttons.
    levelsToggleButton_->setBounds(headerArea.removeFromRight(20));
    headerArea.removeFromRight(4);
    specToggleButton_->setBounds(headerArea.removeFromRight(20));
    headerArea.removeFromRight(4);
    oscToggleButton_->setBounds(headerArea.removeFromRight(20));
    headerArea.removeFromRight(4);
    postFxPanelButton_->setBounds(headerArea.removeFromRight(20));
    headerArea.removeFromRight(8);
    trackNameLabel_.setBounds(headerArea);  // Name takes remaining space

    // Hide solo/pan for master
    if (isMaster) {
        soloButton_.setVisible(false);
        panLabel_.setVisible(false);
    }

    // Link mode label - centered in header, overlays track name when visible
    if (linkModeLabel_.isVisible()) {
        linkModeLabel_.setBounds(headerBounds);
    }
    // Gain-staging banner - centered, same treatment as the link-mode label.
    if (gainStagingLabel_.isVisible()) {
        gainStagingLabel_.setBounds(headerBounds);
    }
}

void TrackChainContent::hideHeaderControls() {
    // Left side - action buttons
    globalModsButton_->setVisible(false);
    macroButton_->setVisible(false);
    addRackButton_->setVisible(false);
    treeViewButton_->setVisible(false);
    presetButton_->setVisible(false);
    gainStagingButton_->setVisible(false);
    gainStagingLabel_.setVisible(false);
    postFxPanelButton_->setVisible(false);
    oscToggleButton_->setVisible(false);
    specToggleButton_->setVisible(false);
    levelsToggleButton_->setVisible(false);
    // Hide panels
    if (globalModsPanel_)
        globalModsPanel_->setVisible(false);
    if (globalMacrosPanel_)
        globalMacrosPanel_->setVisible(false);
    hideGlobalModEditor();
    hideGlobalMacroEditor();
    globalModsVisible_ = false;
    globalMacrosVisible_ = false;
    globalModsButton_->setToggleState(false, juce::dontSendNotification);
    globalModsButton_->setActive(false);
    macroButton_->setToggleState(false, juce::dontSendNotification);
    macroButton_->setActive(false);
    // Right side - track info
    trackNameLabel_.setVisible(false);
    muteButton_.setVisible(false);
    masterMuteButton_.setVisible(false);
    chordSpeakerButton_->setVisible(false);
    monitorButton_.setVisible(false);
    soloButton_.setVisible(false);
    volumeLabel_.setVisible(false);
    panLabel_.setVisible(false);
    volumeLabel_.clearAutomationTarget();
    panLabel_.clearAutomationTarget();
    chainBypassButton_->setVisible(false);
    addDeviceButton_.setVisible(false);
}

void TrackChainContent::rebuildNodeComponents() {
    // Save node states (collapsed, expanded chains) BEFORE clearing components
    saveNodeStates();

    // Clear existing components
    unfocusAllComponents();
    nodeComponents_.clear();

    if (selectedTrackId_ == magda::INVALID_TRACK_ID) {
        return;
    }

    const auto& elements = magda::TrackManager::getInstance().getChainElements(selectedTrackId_);

    // Create a component for each chain element
    for (size_t i = 0; i < elements.size(); ++i) {
        const auto& element = elements[i];

        if (magda::isDevice(element)) {
            // Create device slot component
            const auto& device = magda::getDevice(element);
            auto slot = std::make_unique<DeviceSlotComponent>(device);
            slot->setNodePath(magda::ChainNodePath::topLevelDevice(selectedTrackId_, device.id));

            // Wire up device-specific callbacks
            slot->onDeviceLayoutChanged = [this]() {
                resized();
                repaint();
            };

            // Wire up drag-to-reorder callbacks
            slot->onDragStart = [this](NodeComponent* node, const juce::MouseEvent&) {
                draggedNode_ = node;
                dragOriginalIndex_ = findNodeIndex(node);
                dragInsertIndex_ = dragOriginalIndex_;
                // Capture ghost image and make original semi-transparent
                dragGhostImage_ = node->createComponentSnapshot(node->getLocalBounds());
                node->setAlpha(0.4f);
                startTimerHz(10);  // Start timer to detect stale drag state
                // Re-layout to add left padding for drop indicator
                resized();
            };

            slot->onDragMove = [this](NodeComponent*, const juce::MouseEvent& e) {
                auto pos = e.getEventRelativeTo(chainContainer_.get()).getPosition();
                dragInsertIndex_ = calculateInsertIndex(pos.x);
                dragMousePos_ = pos;
                chainContainer_->repaint();
            };

            slot->onDragEnd = [this](NodeComponent* node, const juce::MouseEvent&) {
                // Restore alpha and clear ghost
                node->setAlpha(1.0f);
                dragGhostImage_ = juce::Image();
                stopTimer();

                draggedNode_ = nullptr;
                dragOriginalIndex_ = -1;
                dragInsertIndex_ = -1;

                resized();
                chainContainer_->repaint();
            };

            chainContainer_->addAndMakeVisible(*slot);
            nodeComponents_.push_back(std::move(slot));

        } else if (magda::isRack(element)) {
            // Create rack component
            const auto& rack = magda::getRack(element);
            auto rackComp = std::make_unique<RackComponent>(selectedTrackId_, rack);
            rackComp->setNodePath(magda::ChainNodePath::rack(selectedTrackId_, rack.id));

            // Wire up callbacks
            rackComp->onSelected = [this]() { selectedDeviceId_ = magda::INVALID_DEVICE_ID; };
            rackComp->onLayoutChanged = [this]() {
                resized();
                repaint();
            };
            rackComp->onChainSelected = [this](magda::TrackId trackId, magda::RackId rId,
                                               magda::ChainId chainId) {
                onChainSelected(trackId, rId, chainId);
            };
            rackComp->onDeviceSelected = [this](magda::DeviceId deviceId) {
                if (deviceId != magda::INVALID_DEVICE_ID) {
                    selectedDeviceId_ = magda::INVALID_DEVICE_ID;
                    magda::SelectionManager::getInstance().selectDevice(
                        selectedTrackId_, selectedRackId_, selectedChainId_, deviceId);
                } else {
                    magda::SelectionManager::getInstance().clearDeviceSelection();
                }
            };

            // Wire up drag-to-reorder callbacks
            rackComp->onDragStart = [this](NodeComponent* node, const juce::MouseEvent&) {
                draggedNode_ = node;
                dragOriginalIndex_ = findNodeIndex(node);
                dragInsertIndex_ = dragOriginalIndex_;
                // Capture ghost image and make original semi-transparent
                dragGhostImage_ = node->createComponentSnapshot(node->getLocalBounds());
                node->setAlpha(0.4f);
                startTimerHz(10);  // Start timer to detect stale drag state
                // Re-layout to add left padding for drop indicator
                resized();
            };

            rackComp->onDragMove = [this](NodeComponent*, const juce::MouseEvent& e) {
                auto pos = e.getEventRelativeTo(chainContainer_.get()).getPosition();
                dragInsertIndex_ = calculateInsertIndex(pos.x);
                dragMousePos_ = pos;
                chainContainer_->repaint();
            };

            rackComp->onDragEnd = [this](NodeComponent* node, const juce::MouseEvent&) {
                // Restore alpha and clear ghost
                node->setAlpha(1.0f);
                dragGhostImage_ = juce::Image();
                stopTimer();

                draggedNode_ = nullptr;
                dragOriginalIndex_ = -1;
                dragInsertIndex_ = -1;
                resized();
                chainContainer_->repaint();
            };

            chainContainer_->addAndMakeVisible(*rackComp);
            nodeComponents_.push_back(std::move(rackComp));
        }
    }

    // Set frozen state on all nodes
    auto* trackInfo = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    bool trackFrozen = trackInfo && trackInfo->frozen;
    for (auto& node : nodeComponents_) {
        node->setFrozen(trackFrozen);
    }

    // Restore node states (collapsed, expanded chains) for ALL nodes
    restoreNodeStates();

    // Restore selection state from SelectionManager
    auto& sm = magda::SelectionManager::getInstance();
    const auto& selectedPath = sm.getSelectedChainNode();
    if (selectedPath.isValid() && selectedPath.trackId == selectedTrackId_) {
        for (auto& node : nodeComponents_) {
            if (node->getNodePath() == selectedPath) {
                node->setSelected(true);
                break;
            }
        }
    } else if (sm.getDeviceSelection().isValid()) {
        // Also restore from Device selection — check validity rather than
        // SelectionType, since selectTrack() changes type to Track before
        // rebuildNodeComponents runs.
        const auto& deviceSel = sm.getDeviceSelection();
        if (deviceSel.trackId == selectedTrackId_ &&
            deviceSel.deviceId != magda::INVALID_DEVICE_ID) {
            selectedDeviceId_ = deviceSel.deviceId;
            for (auto& node : nodeComponents_) {
                if (auto* slot = dynamic_cast<DeviceSlotComponent*>(node.get())) {
                    if (slot->getDeviceId() == deviceSel.deviceId) {
                        slot->setSelected(true);
                        break;
                    }
                }
            }
        }
    }

    resized();
    repaint();
}

void TrackChainContent::onChainSelected(magda::TrackId trackId, magda::RackId rackId,
                                        magda::ChainId chainId) {
    // Store selection locally
    selectedRackId_ = rackId;
    selectedChainId_ = chainId;
    (void)trackId;  // Already tracked via selectedTrackId_

    // Notify TrackManager of chain selection (for plugin browser)
    magda::TrackManager::getInstance().setSelectedChain(selectedTrackId_, rackId, chainId);

    // Clear selection in other racks (hide their chain panels)
    for (auto& node : nodeComponents_) {
        if (auto* rack = dynamic_cast<RackComponent*>(node.get())) {
            if (rack->getRackId() != rackId) {
                rack->clearChainSelection();
                rack->hideChainPanel();
            }
        }
    }

    // Relayout since rack widths may have changed
    resized();
    repaint();
}

bool TrackChainContent::hasSelectedTrack() const {
    return selectedTrackId_ != magda::INVALID_TRACK_ID;
}

bool TrackChainContent::hasSelectedChain() const {
    return selectedTrackId_ != magda::INVALID_TRACK_ID &&
           selectedRackId_ != magda::INVALID_RACK_ID && selectedChainId_ != magda::INVALID_CHAIN_ID;
}

void TrackChainContent::onAddDeviceClicked() {
    if (!hasSelectedTrack())
        return;

    juce::PopupMenu menu;

    auto internals = magda::daw::ui::PluginBrowserContent::getInternalPlugins();
    juce::PopupMenu internalMenu;
    int itemId = 1;
    for (const auto& entry : internals) {
        internalMenu.addItem(itemId++, entry.name);
    }
    menu.addSubMenu("Internal", internalMenu);

    juce::Array<juce::PluginDescription> externalPlugins;
    if (auto* engine = dynamic_cast<magda::TracktionEngineWrapper*>(
            magda::TrackManager::getInstance().getAudioEngine())) {
        auto& knownPlugins = engine->getKnownPluginList();
        externalPlugins = knownPlugins.getTypes();
    }

    if (!externalPlugins.isEmpty()) {
        std::map<juce::String, juce::PopupMenu> byManufacturer;
        for (int i = 0; i < externalPlugins.size(); ++i) {
            const auto& desc = externalPlugins[i];
            auto manufacturer = desc.manufacturerName.isEmpty() ? "Unknown" : desc.manufacturerName;
            byManufacturer[manufacturer].addItem(1000 + static_cast<int>(i), desc.name);
        }
        for (auto& [manufacturer, subMenu] : byManufacturer) {
            menu.addSubMenu(manufacturer, subMenu);
        }
    }

    auto safeThis = juce::Component::SafePointer<TrackChainContent>(this);
    auto trackId = selectedTrackId_;
    auto capturedPlugins =
        std::make_shared<juce::Array<juce::PluginDescription>>(std::move(externalPlugins));
    auto capturedInternals =
        std::make_shared<std::vector<magda::daw::ui::PluginBrowserInfo>>(std::move(internals));

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, trackId, capturedPlugins,
                                                    capturedInternals](int result) {
        if (result == 0)
            return;

        magda::DeviceInfo device;

        if (result >= 1 && result <= static_cast<int>(capturedInternals->size())) {
            const auto& entry = (*capturedInternals)[static_cast<size_t>(result - 1)];
            device.name = entry.name;
            device.manufacturer = "MAGDA";
            device.pluginId = entry.uniqueId;
            device.isInstrument = entry.category == "Instrument";
            if (entry.subcategory == "MIDI")
                device.deviceType = magda::DeviceType::MIDI;
            else if (entry.category == "Instrument")
                device.deviceType = magda::DeviceType::Instrument;
            device.format = magda::PluginFormat::Internal;
        } else if (result >= 1000) {
            int idx = result - 1000;
            if (idx < 0 || idx >= static_cast<int>(capturedPlugins->size()))
                return;
            const auto& desc = (*capturedPlugins)[idx];
            device.name = desc.name;
            device.manufacturer = desc.manufacturerName;
            device.pluginId = desc.createIdentifierString();
            device.isInstrument = desc.isInstrument;
            device.uniqueId = desc.createIdentifierString();
            device.fileOrIdentifier = desc.fileOrIdentifier;
            if (desc.pluginFormatName == "VST3")
                device.format = magda::PluginFormat::VST3;
            else if (desc.pluginFormatName == "AU" || desc.pluginFormatName == "AudioUnit")
                device.format = magda::PluginFormat::AU;
            else if (desc.pluginFormatName == "VST")
                device.format = magda::PluginFormat::VST;
            else
                device.format = magda::PluginFormat::Internal;
        } else {
            return;
        }

        if (safeThis != nullptr) {
            safeThis->scrollToEndAfterNextDeviceChange_ = true;
        }
        magda::TrackManager::getInstance().addDeviceToTrack(trackId, device);
    });
}

void TrackChainContent::addDeviceToSelectedTrack(const magda::DeviceInfo& device) {
    if (!hasSelectedTrack()) {
        return;
    }
    scrollToEndAfterNextDeviceChange_ = true;
    magda::TrackManager::getInstance().addDeviceToTrack(selectedTrackId_, device);
}

void TrackChainContent::addDeviceToSelectedChain(const magda::DeviceInfo& device) {
    if (!hasSelectedChain()) {
        return;
    }
    magda::TrackManager::getInstance().addDeviceToChain(selectedTrackId_, selectedRackId_,
                                                        selectedChainId_, device);
}

void TrackChainContent::scrollToEndAsync() {
    auto safeThis = juce::Component::SafePointer<TrackChainContent>(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (safeThis == nullptr || safeThis->chainViewport_ == nullptr ||
            safeThis->chainContainer_ == nullptr)
            return;

        safeThis->layoutChainContent();
        const int maxX = juce::jmax(0, safeThis->chainContainer_->getWidth() -
                                           safeThis->chainViewport_->getWidth());
        safeThis->chainViewport_->setViewPosition(maxX,
                                                  safeThis->chainViewport_->getViewPositionY());
    });
}

void TrackChainContent::clearDeviceSelection() {
    DBG("TrackChainContent::clearDeviceSelection");
    selectedDeviceId_ = magda::INVALID_DEVICE_ID;

    // Clear selection on all node components
    for (auto& node : nodeComponents_) {
        node->setSelected(false);
        // Also clear device selection in rack components (but keep chain panel open)
        if (auto* rack = dynamic_cast<RackComponent*>(node.get())) {
            rack->clearDeviceSelection();
        }
    }
    // Notify SelectionManager - this will update inspector
    magda::SelectionManager::getInstance().clearDeviceSelection();
}

void TrackChainContent::onDeviceSlotSelected(magda::DeviceId deviceId) {
    DBG("TrackChainContent::onDeviceSlotSelected deviceId=" + juce::String(deviceId));
    selectedDeviceId_ = deviceId;

    // Update selection state on all node components
    for (auto& node : nodeComponents_) {
        if (auto* slot = dynamic_cast<DeviceSlotComponent*>(node.get())) {
            bool shouldSelect = slot->getDeviceId() == deviceId;
            slot->setSelected(shouldSelect);
        } else if (auto* rack = dynamic_cast<RackComponent*>(node.get())) {
            // Clear device/chain selection in racks (but keep chain panel open)
            rack->clearDeviceSelection();
            rack->clearChainSelection();  // Clear chain row selection border
            rack->setSelected(false);     // Deselect the rack itself too
        }
    }
    // Notify SelectionManager - this will update inspector
    magda::SelectionManager::getInstance().selectDevice(selectedTrackId_, deviceId);
}

int TrackChainContent::findNodeIndex(NodeComponent* node) const {
    for (size_t i = 0; i < nodeComponents_.size(); ++i) {
        if (nodeComponents_[i].get() == node) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int TrackChainContent::calculateInsertIndex(int mouseX) const {
    // Find insert position based on mouse X and node midpoints
    for (size_t i = 0; i < nodeComponents_.size(); ++i) {
        int midX = nodeComponents_[i]->getX() + nodeComponents_[i]->getWidth() / 2;
        if (mouseX < midX) {
            return static_cast<int>(i);
        }
    }
    // After last element
    return static_cast<int>(nodeComponents_.size());
}

int TrackChainContent::calculateIndicatorX(int index) const {
    if (nodeComponents_.empty() && index == 0) {
        return calculateAppendZoneX();
    }

    // Before first element - center in the drag padding area
    if (index == 0) {
        return DRAG_LEFT_PADDING / 2;
    }

    if (index == static_cast<int>(nodeComponents_.size())) {
        return calculateAppendZoneX();
    }

    // After previous element
    if (index > 0 && index <= static_cast<int>(nodeComponents_.size())) {
        return nodeComponents_[index - 1]->getRight() + getScaledWidth(ARROW_WIDTH) / 2;
    }

    // Fallback
    return DRAG_LEFT_PADDING / 2;
}

int TrackChainContent::calculateAppendZoneX() const {
    // Pin the append zone to the right edge of the container. The container is
    // sized to max(content, viewport), so this puts the "+" at the far right of
    // the visible area when the chain is short, and right after the last node
    // once the content overflows (container width == content width then).
    const int scaledAppendWidth = getScaledWidth(APPEND_ZONE_WIDTH);
    if (chainContainer_ != nullptr)
        return juce::jmax(0, chainContainer_->getWidth() - scaledAppendWidth);
    return 0;
}

void TrackChainContent::saveNodeStates() {
    savedCollapsedStates_.clear();
    savedExpandedChains_.clear();
    savedParamPanelStates_.clear();
    savedCustomUITabStates_.clear();
    savedDrumPadCollapsedPlugins_.clear();

    for (const auto& node : nodeComponents_) {
        const auto& path = node->getNodePath();
        if (path.isValid()) {
            // Save collapsed state — but skip racks: their collapsed state is
            // persisted in RackInfo::expanded so a freshly-loaded rack preset
            // can drive the expanded/collapsed UI directly. Caching here
            // would shadow the preset value on rebuild.
            const bool isRack = dynamic_cast<RackComponent*>(node.get()) != nullptr;
            if (!isRack)
                savedCollapsedStates_[path.toString()] = node->isCollapsed();

            // Save param panel (macro panel) visible state
            savedParamPanelStates_[path.toString()] = node->isParamPanelVisible();

            // Save custom UI tab index (e.g., 4OSC tab selection)
            if (auto* device = dynamic_cast<DeviceSlotComponent*>(node.get())) {
                int tabIndex = device->getCustomUITabIndex();
                if (tabIndex > 0)
                    savedCustomUITabStates_[path.toString()] = tabIndex;

                // Save DrumGrid pad chain collapsed plugins
                auto collapsed = device->getDrumPadCollapsedPlugins();
                if (!collapsed.empty())
                    savedDrumPadCollapsedPlugins_[path.toString()] = std::move(collapsed);
            }

            // Save expanded chain for racks
            if (auto* rack = dynamic_cast<RackComponent*>(node.get())) {
                if (rack->isChainPanelVisible()) {
                    savedExpandedChains_[path.toString()] = rack->getSelectedChainId();
                }
            }
        }
    }
}

void TrackChainContent::restoreNodeStates() {
    for (auto& node : nodeComponents_) {
        const auto& path = node->getNodePath();
        if (path.isValid()) {
            // Restore collapsed state
            auto collapsedIt = savedCollapsedStates_.find(path.toString());
            if (collapsedIt != savedCollapsedStates_.end()) {
                node->setCollapsed(collapsedIt->second);
            }

            // Restore param panel (macro panel) visible state
            auto paramIt = savedParamPanelStates_.find(path.toString());
            if (paramIt != savedParamPanelStates_.end() && paramIt->second) {
                node->setParamPanelVisible(true);
            }

            // Restore custom UI tab index (e.g., 4OSC tab selection)
            if (auto* device = dynamic_cast<DeviceSlotComponent*>(node.get())) {
                auto tabIt = savedCustomUITabStates_.find(path.toString());
                if (tabIt != savedCustomUITabStates_.end()) {
                    device->setCustomUITabIndex(tabIt->second);
                }

                // Restore DrumGrid pad chain collapsed plugins
                auto collapsedIt = savedDrumPadCollapsedPlugins_.find(path.toString());
                if (collapsedIt != savedDrumPadCollapsedPlugins_.end()) {
                    device->setDrumPadCollapsedPlugins(collapsedIt->second);
                }
            }

            // Restore expanded chain for racks
            if (auto* rack = dynamic_cast<RackComponent*>(node.get())) {
                auto chainIt = savedExpandedChains_.find(path.toString());
                if (chainIt != savedExpandedChains_.end() &&
                    chainIt->second != magda::INVALID_CHAIN_ID) {
                    rack->showChainPanel(chainIt->second);
                }
            }
        }
    }
}

void TrackChainContent::timerCallback() {
    // Check if internal drag state is stale (drag was cancelled)
    if (dragInsertIndex_ >= 0 || draggedNode_ != nullptr) {
        // Check if any mouse button is still down - if not, the drag was cancelled
        if (!juce::Desktop::getInstance().getMainMouseSource().isDragging()) {
            if (draggedNode_) {
                draggedNode_->setAlpha(1.0f);
            }
            draggedNode_ = nullptr;
            dragOriginalIndex_ = -1;
            dragInsertIndex_ = -1;
            dragGhostImage_ = juce::Image();
            stopTimer();
            resized();
            chainContainer_->repaint();
            return;
        }
    }

    // Check if external drop state is stale (drag was cancelled)
    if (dropInsertIndex_ >= 0) {
        if (auto* container =
                juce::DragAndDropContainer::findParentDragContainerFor(chainContainer_.get())) {
            if (!container->isDragAndDropActive()) {
                dropInsertIndex_ = -1;
                stopTimer();
                resized();
                chainContainer_->repaint();
                return;
            }
        }
    }

    // No stale state, stop the timer
    if (dragInsertIndex_ < 0 && draggedNode_ == nullptr && dropInsertIndex_ < 0) {
        stopTimer();
    }
}

void TrackChainContent::setZoomLevel(float zoom) {
    DBG("TrackChainContent::setZoomLevel - requested=" << zoom << " current=" << zoomLevel_);
    float newZoom = juce::jlimit(MIN_ZOOM, MAX_ZOOM, zoom);
    if (std::abs(zoomLevel_ - newZoom) > 0.001f) {
        zoomLevel_ = newZoom;
        DBG("  -> Zoom changed to " << zoomLevel_);
        layoutChainContent();
        repaint();
    }
}

int TrackChainContent::getScaledWidth(int width) const {
    return static_cast<int>(std::round(width * zoomLevel_));
}

// =============================================================================
// MAGDA Track-Chain Presets — UI wiring for PresetManager::save/loadChainPreset
// Mirrors RackComponent's preset menu so the experience matches whether the
// user is loading a single rack or an entire track FX chain.
// =============================================================================

namespace {
void showChainPresetErrorAsync(const juce::String& title, const juce::String& message) {
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle(title)
                                     .withMessage(message)
                                     .withButton("OK"),
                                 nullptr);
}

void buildChainPresetSubmenu(juce::PopupMenu& menu, const juce::File& dir,
                             const juce::String& prefix, int idBase,
                             const juce::String& currentLoaded, juce::StringArray& outIndex) {
    if (!dir.isDirectory())
        return;
    auto subdirs = dir.findChildFiles(juce::File::findDirectories, false);
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.mps");
    subdirs.sort();
    files.sort();

    for (const auto& sub : subdirs) {
        juce::PopupMenu submenu;
        buildChainPresetSubmenu(submenu, sub, prefix + sub.getFileName() + "/", idBase,
                                currentLoaded, outIndex);
        menu.addSubMenu(sub.getFileName(), submenu);
    }
    for (const auto& f : files) {
        const auto displayName = f.getFileNameWithoutExtension();
        const auto relPath = prefix + displayName;
        outIndex.add(relPath);
        const bool ticked = (relPath == currentLoaded);
        menu.addItem(idBase + outIndex.size() - 1, displayName, /*isActive*/ true, ticked);
    }
}
}  // namespace

void TrackChainContent::showPresetMenu() {
    auto& pm = magda::PresetManager::getInstance();

    constexpr int kSaveOverwrite = 1;
    constexpr int kSaveAs = 2;
    constexpr int kRevealInFinder = 3;
    constexpr int kPresetIdBase = 1000;

    juce::PopupMenu menu;
    menu.addSectionHeader("MAGDA Track Presets");

    juce::StringArray index;
    buildChainPresetSubmenu(menu, pm.getChainsDirectory(), "", kPresetIdBase, currentPresetName_,
                            index);

    if (index.isEmpty())
        menu.addItem(kPresetIdBase, "(no presets yet)", /*isActive*/ false);

    menu.addSeparator();
    if (currentPresetName_.isNotEmpty())
        menu.addItem(kSaveOverwrite, "Save \"" + currentPresetName_ + "\"");
    menu.addItem(kSaveAs, "Save as MAGDA Track Preset...");
    menu.addItem(kRevealInFinder, "Reveal in Finder");

    const auto indexCopy = index;
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(presetButton_.get()),
        [this, indexCopy](int chosen) {
            if (chosen == 0)
                return;
            if (chosen == kSaveAs) {
                showSaveTrackPresetDialog();
            } else if (chosen == kSaveOverwrite) {
                saveCurrentTrackPreset();
            } else if (chosen == kRevealInFinder) {
                magda::PresetManager::getInstance().getChainsDirectory().revealToUser();
            } else if (chosen >= kPresetIdBase) {
                const int idx = chosen - kPresetIdBase;
                if (idx >= 0 && idx < indexCopy.size())
                    loadTrackPresetByName(indexCopy[idx]);
            }
        });
}

void TrackChainContent::showSaveTrackPresetDialog() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID)
        return;

    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    const juce::String defaultName =
        currentPresetName_.isNotEmpty() ? currentPresetName_ : (track ? track->name : "Track");

    auto* aw = new juce::AlertWindow(
        "Save MAGDA Track Preset",
        "Enter a name for this track preset (use \"/\" to nest, e.g. \"Bass/808 Stack\"):",
        juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor("name", defaultName, "Name:");
    aw->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<TrackChainContent> self(this);
    aw->enterModalState(
        true, juce::ModalCallbackFunction::create([aw, self](int result) {
            if (result != 1) {
                delete aw;
                return;
            }
            auto name = aw->getTextEditorContents("name").trim();
            delete aw;
            if (name.isEmpty() || self == nullptr)
                return;

            auto doSave = [name, self]() {
                if (self == nullptr)
                    return;
                if (self->selectedTrackId_ == magda::INVALID_TRACK_ID)
                    return;
                const auto& elements =
                    magda::TrackManager::getInstance().getChainElements(self->selectedTrackId_);
                auto& mgr = magda::PresetManager::getInstance();
                if (!mgr.saveChainPreset(elements, name)) {
                    showChainPresetErrorAsync("Save Track Preset Failed", mgr.getLastError());
                    return;
                }
                self->currentPresetName_ = name;
            };

            if (magda::PresetManager::getInstance().getChainPresets().contains(name)) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::QuestionIcon)
                        .withTitle("Overwrite Track Preset?")
                        .withMessage("\"" + name + "\" already exists. Overwrite?")
                        .withButton("Overwrite")
                        .withButton("Cancel"),
                    [doSave](int r) {
                        if (r == 1)
                            doSave();
                    });
            } else {
                doSave();
            }
        }));
}

void TrackChainContent::saveCurrentTrackPreset() {
    if (currentPresetName_.isEmpty() || selectedTrackId_ == magda::INVALID_TRACK_ID)
        return;
    const auto& elements = magda::TrackManager::getInstance().getChainElements(selectedTrackId_);
    auto& pm = magda::PresetManager::getInstance();
    if (!pm.saveChainPreset(elements, currentPresetName_))
        showChainPresetErrorAsync("Save Track Preset Failed", pm.getLastError());
}

void TrackChainContent::loadTrackPresetByName(const juce::String& presetName) {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID)
        return;
    auto& pm = magda::PresetManager::getInstance();
    std::vector<magda::ChainElement> elements;
    if (!pm.loadChainPreset(presetName, elements)) {
        showChainPresetErrorAsync("Load Track Preset Failed", pm.getLastError());
        return;
    }
    if (!magda::TrackManager::getInstance().applyChainPreset(selectedTrackId_,
                                                             std::move(elements))) {
        showChainPresetErrorAsync("Load Track Preset Failed", "Failed to apply preset to track.");
        return;
    }
    currentPresetName_ = presetName;
}

}  // namespace magda::daw::ui
