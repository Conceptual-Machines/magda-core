#include "SessionView.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <functional>
#include <set>

#include "../../audio/AudioBridge.hpp"
#include "../../audio/MeteringBuffer.hpp"
#include "../../engine/AudioEngine.hpp"
#include "../../engine/TracktionEngineWrapper.hpp"
#include "../components/common/TextSlider.hpp"
#include "../components/mixer/LevelMeter.hpp"
#include "../components/mixer/RoutingSelector.hpp"
#include "../components/mixer/RoutingSyncHelper.hpp"
#include "../panels/state/PanelController.hpp"
#include "../state/TimelineController.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "../themes/SmallButtonLookAndFeel.hpp"
#include "ClipSlotButton.hpp"
#include "core/ClipCommands.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

// dB conversion helpers for faders
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

// Custom grid content that draws track separators and empty cells
class SessionView::GridContent : public juce::Component {
  public:
    GridContent(int /*clipHeight*/, int separatorWidth, int /*clipMargin*/, int numScenes)
        : separatorWidth_(separatorWidth), numScenes_(numScenes) {
        setInterceptsMouseClicks(true, true);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onContextMenu)
            onContextMenu();
    }

    std::function<void()> onContextMenu;

    void setNumTracks(int numTracks) {
        numTracks_ = numTracks;
        repaint();
    }

    void setNumScenes(int numScenes) {
        numScenes_ = numScenes;
        repaint();
    }

    void setTrackWidths(const std::vector<int>& widths) {
        trackWidths_ = widths;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        // Draw vertical separators between tracks (after each clip slot)
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        int x = 0;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            x += trackWidths_[i];
            g.fillRect(x, 0, separatorWidth_, getHeight());
            x += separatorWidth_;
        }
    }

  private:
    int numTracks_ = 0;
    std::vector<int> trackWidths_;
    int separatorWidth_;
    int numScenes_;
};

// Custom viewport that draws track separators in the background area
class SessionView::GridViewport : public juce::Viewport {
  public:
    GridViewport() {
        setInterceptsMouseClicks(true, true);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onContextMenu)
            onContextMenu();
        else
            juce::Viewport::mouseDown(e);
    }

    std::function<void()> onContextMenu;

    void setTrackLayout(int numTracks, const std::vector<int>& trackWidths, int separatorWidth) {
        numTracks_ = numTracks;
        trackWidths_ = trackWidths;
        separatorWidth_ = separatorWidth;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        // Draw vertical separators in the background (visible when content is shorter than
        // viewport)
        int scrollX = getViewPositionX();
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        int x = 0;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            x += trackWidths_[i];
            g.fillRect(x - scrollX, 0, separatorWidth_, getHeight());
            x += separatorWidth_;
        }
    }

  private:
    int numTracks_ = 0;
    std::vector<int> trackWidths_;
    int separatorWidth_ = 3;
};

// Container for per-track stop buttons (pinned between grid and faders)
class SessionView::StopButtonContainer : public juce::Component {
  public:
    StopButtonContainer() {
        setInterceptsMouseClicks(true, true);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onContextMenu)
            onContextMenu();
    }

    std::function<void()> onContextMenu;

    void setTrackLayout(int numTracks, const std::vector<int>& trackWidths, int separatorWidth,
                        int scrollOffset) {
        numTracks_ = numTracks;
        trackWidths_ = trackWidths;
        separatorWidth_ = separatorWidth;
        scrollOffset_ = scrollOffset;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));

        // Top border
        g.fillRect(0, 0, getWidth(), 1);

        // Draw vertical separators between tracks
        int x = 0;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            x += trackWidths_[i];
            g.fillRect(x - scrollOffset_, 0, separatorWidth_, getHeight());
            x += separatorWidth_;
        }
    }

  private:
    int numTracks_ = 0;
    std::vector<int> trackWidths_;
    int separatorWidth_ = 3;
    int scrollOffset_ = 0;
};

// Container for track headers with clipping
class SessionView::HeaderContainer : public juce::Component {
  public:
    HeaderContainer() {
        setInterceptsMouseClicks(false, true);
    }

    void setTrackLayout(int numTracks, const std::vector<int>& trackWidths, int separatorWidth,
                        int scrollOffset) {
        numTracks_ = numTracks;
        trackWidths_ = trackWidths;
        separatorWidth_ = separatorWidth;
        scrollOffset_ = scrollOffset;
        repaint();
    }

    std::function<void(juce::Graphics&)> onPaintOverChildren;

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        // Draw vertical separators between tracks
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        int x = 0;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            x += trackWidths_[i];
            g.fillRect(x - scrollOffset_, 0, separatorWidth_, getHeight());
            x += separatorWidth_;
        }
    }

    void paintOverChildren(juce::Graphics& g) override {
        if (onPaintOverChildren)
            onPaintOverChildren(g);
    }

  private:
    int numTracks_ = 0;
    std::vector<int> trackWidths_;
    int separatorWidth_ = 3;
    int scrollOffset_ = 0;
};

// Container for scene buttons with clipping
class SessionView::SceneContainer : public juce::Component {
  public:
    SceneContainer() {
        setInterceptsMouseClicks(false, true);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
    }
};

// ResizeHandle for dragging to resize areas
class SessionView::ResizeHandle : public juce::Component {
  public:
    enum Direction { Horizontal, Vertical };

    ResizeHandle(Direction dir) : direction(dir) {
        setMouseCursor(direction == Horizontal ? juce::MouseCursor::LeftRightResizeCursor
                                               : juce::MouseCursor::UpDownResizeCursor);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(DarkTheme::getColour(DarkTheme::RESIZE_HANDLE));
        g.fillAll();
    }

    void mouseDown(const juce::MouseEvent& event) override {
        if (event.mods.isPopupMenu()) {
            if (onContextMenu)
                onContextMenu();
            return;
        }
        if (onResizeStart) {
            onResizeStart();
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        auto delta = direction == Horizontal ? event.getDistanceFromDragStartX()
                                             : event.getDistanceFromDragStartY();

        if (onResize) {
            onResize(delta);
        }
    }

    std::function<void()> onResizeStart;
    std::function<void(int)> onResize;
    std::function<void()> onContextMenu;

  private:
    Direction direction;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResizeHandle)
};

// Container for track faders at the bottom
class SessionView::FaderContainer : public juce::Component {
  public:
    FaderContainer() {
        setInterceptsMouseClicks(false, true);
    }

    void setTrackLayout(int numTracks, const std::vector<int>& trackWidths, int separatorWidth,
                        int scrollOffset) {
        numTracks_ = numTracks;
        trackWidths_ = trackWidths;
        separatorWidth_ = separatorWidth;
        scrollOffset_ = scrollOffset;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        // Top border
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        g.fillRect(0, 0, getWidth(), 1);

        // Draw vertical separators between tracks
        int x = 0;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            x += trackWidths_[i];
            g.fillRect(x - scrollOffset_, 1, separatorWidth_, getHeight() - 1);
            x += separatorWidth_;
        }
    }

  private:
    int numTracks_ = 0;
    std::vector<int> trackWidths_;
    int separatorWidth_ = 3;
    int scrollOffset_ = 0;
};

// Container for I/O routing row (between stop buttons and fader row)
class SessionView::IOContainer : public juce::Component {
  public:
    IOContainer() {
        setInterceptsMouseClicks(true, true);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onContextMenu)
            onContextMenu();
    }

    std::function<void()> onContextMenu;

    void setTrackLayout(int numTracks, const std::vector<int>& trackWidths, int separatorWidth,
                        int scrollOffset) {
        numTracks_ = numTracks;
        trackWidths_ = trackWidths;
        separatorWidth_ = separatorWidth;
        scrollOffset_ = scrollOffset;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        g.fillRect(0, 0, getWidth(), 1);

        int x = 0;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            x += trackWidths_[i];
            g.fillRect(x - scrollOffset_, 1, separatorWidth_, getHeight() - 1);
            x += separatorWidth_;
        }
    }

  private:
    int numTracks_ = 0;
    std::vector<int> trackWidths_;
    int separatorWidth_ = 3;
    int scrollOffset_ = 0;
};

// Container for send section (between stop buttons and IO row)
class SessionView::SendSectionContainer : public juce::Component {
  public:
    SendSectionContainer() {
        setInterceptsMouseClicks(true, true);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onContextMenu)
            onContextMenu();
    }

    std::function<void()> onContextMenu;

    void setTrackLayout(int numTracks, const std::vector<int>& trackWidths, int separatorWidth,
                        int scrollOffset) {
        numTracks_ = numTracks;
        trackWidths_ = trackWidths;
        separatorWidth_ = separatorWidth;
        scrollOffset_ = scrollOffset;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        g.fillRect(0, 0, getWidth(), 1);

        int x = 0;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            x += trackWidths_[i];
            g.fillRect(x - scrollOffset_, 1, separatorWidth_, getHeight() - 1);
            x += separatorWidth_;
        }
    }

  private:
    int numTracks_ = 0;
    std::vector<int> trackWidths_;
    int separatorWidth_ = 3;
    int scrollOffset_ = 0;
};

// Per-track send strip (shows send rows inside a viewport)
class SessionView::MiniSendStrip : public juce::Component {
  public:
    MiniSendStrip(TrackId trackId) : trackId_(trackId) {
        setInterceptsMouseClicks(true, true);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) {
            showSendContextMenu();
        }
    }

    void paint(juce::Graphics& /*g*/) override {}

    void resized() override {
        layoutSlots();
    }

    void updateFromTrack() {
        const auto* track = TrackManager::getInstance().getTrack(trackId_);
        if (!track)
            return;

        bool countChanged = slots_.size() != track->sends.size();
        if (countChanged) {
            rebuildSlots(track->sends);
        } else {
            for (size_t i = 0; i < slots_.size(); ++i) {
                auto& slot = slots_[i];
                const auto& send = track->sends[i];
                if (slot.levelSlider && !slot.levelSlider->isBeingDragged())
                    slot.levelSlider->setValue(send.level, juce::dontSendNotification);
                if (slot.nameLabel && send.destTrackId != INVALID_TRACK_ID) {
                    if (auto* destTrack = TrackManager::getInstance().getTrack(send.destTrackId))
                        slot.nameLabel->setText(destTrack->name, juce::dontSendNotification);
                }
            }
        }
    }

    TrackId getTrackId() const {
        return trackId_;
    }

  private:
    static constexpr int SEND_SLOT_HEIGHT = 18;

    struct SendSlot {
        int busIndex;
        std::unique_ptr<juce::Label> nameLabel;
        std::unique_ptr<daw::ui::TextSlider> levelSlider;
        std::unique_ptr<juce::TextButton> removeButton;
    };

    TrackId trackId_;
    std::vector<SendSlot> slots_;

    void rebuildSlots(const std::vector<SendInfo>& sends) {
        for (auto& slot : slots_) {
            removeChildComponent(slot.nameLabel.get());
            removeChildComponent(slot.levelSlider.get());
            removeChildComponent(slot.removeButton.get());
        }
        slots_.clear();

        for (const auto& send : sends) {
            SendSlot slot;
            slot.busIndex = send.busIndex;

            // Dest name label
            slot.nameLabel = std::make_unique<juce::Label>();
            juce::String destName = "Bus " + juce::String(send.busIndex);
            if (send.destTrackId != INVALID_TRACK_ID) {
                if (auto* destTrack = TrackManager::getInstance().getTrack(send.destTrackId))
                    destName = destTrack->name;
            }
            slot.nameLabel->setText(destName, juce::dontSendNotification);
            slot.nameLabel->setFont(FontManager::getInstance().getUIFont(9.0f));
            slot.nameLabel->setColour(juce::Label::textColourId,
                                      DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            slot.nameLabel->setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(*slot.nameLabel);

            // Level slider (horizontal, 0-1)
            slot.levelSlider =
                std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decimal);
            slot.levelSlider->setOrientation(daw::ui::TextSlider::Orientation::Horizontal);
            slot.levelSlider->setRange(0.0, 1.0, 0.01);
            slot.levelSlider->setValue(send.level, juce::dontSendNotification);
            slot.levelSlider->setFont(FontManager::getInstance().getUIFont(9.0f));
            int busIdx = send.busIndex;
            slot.levelSlider->onValueChanged = [this, busIdx](double val) {
                UndoManager::getInstance().executeCommand(std::make_unique<SetSendLevelCommand>(
                    trackId_, busIdx, static_cast<float>(val)));
            };
            addAndMakeVisible(*slot.levelSlider);

            // Remove button
            slot.removeButton = std::make_unique<juce::TextButton>("x");
            slot.removeButton->setConnectedEdges(
                juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
            slot.removeButton->setColour(juce::TextButton::buttonColourId,
                                         DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
            slot.removeButton->setColour(juce::TextButton::textColourOffId,
                                         DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            slot.removeButton->onClick = [this, busIdx]() {
                TrackManager::getInstance().removeSend(trackId_, busIdx);
            };
            addAndMakeVisible(*slot.removeButton);

            slots_.push_back(std::move(slot));
        }

        layoutSlots();
    }

    void layoutSlots() {
        int w = getWidth();
        int y = 0;
        for (auto& slot : slots_) {
            auto row = juce::Rectangle<int>(0, y, w, SEND_SLOT_HEIGHT);
            slot.nameLabel->setBounds(row.removeFromLeft(row.getWidth() * 40 / 100));
            auto removeArea = row.removeFromRight(16);
            slot.removeButton->setBounds(removeArea);
            slot.levelSlider->setBounds(row);
            y += SEND_SLOT_HEIGHT + 1;
        }
        int totalH = juce::jmax(1, y);
        if (getHeight() != totalH)
            setSize(getWidth(), totalH);
    }

    void showSendContextMenu() {
        juce::PopupMenu menu;

        // Add Send submenu
        juce::PopupMenu sendSubMenu;
        const auto& tracks = TrackManager::getInstance().getTracks();
        std::set<TrackId> existingSendDests;
        if (auto* thisTrack = TrackManager::getInstance().getTrack(trackId_)) {
            for (const auto& send : thisTrack->sends)
                existingSendDests.insert(send.destTrackId);
        }
        for (const auto& t : tracks) {
            if (t.id != trackId_ && t.type != TrackType::Master &&
                existingSendDests.find(t.id) == existingSendDests.end()) {
                sendSubMenu.addItem(1000 + t.id, t.name);
            }
        }
        if (sendSubMenu.getNumItems() == 0) {
            sendSubMenu.addItem(-1, "(No tracks available)", false);
        }
        menu.addSubMenu("Add Send", sendSubMenu);

        auto safeThis = juce::Component::SafePointer<MiniSendStrip>(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result) {
            if (safeThis && result >= 1000) {
                TrackManager::getInstance().addSend(safeThis->trackId_,
                                                    static_cast<TrackId>(result - 1000));
            }
        });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniSendStrip)
};

// Mini I/O routing strip per track (2x2 grid: AudioIn/Out, MidiIn/Out)
class SessionView::MiniIOStrip : public juce::Component {
  public:
    MiniIOStrip(TrackId trackId, AudioEngine* audioEngine)
        : trackId_(trackId), audioEngine_(audioEngine) {
        audioInSelector_ = std::make_unique<RoutingSelector>(RoutingSelector::Type::AudioIn);
        audioOutSelector_ = std::make_unique<RoutingSelector>(RoutingSelector::Type::AudioOut);
        midiInSelector_ = std::make_unique<RoutingSelector>(RoutingSelector::Type::MidiIn);
        midiOutSelector_ = std::make_unique<RoutingSelector>(RoutingSelector::Type::MidiOut);

        addAndMakeVisible(*audioInSelector_);
        addAndMakeVisible(*audioOutSelector_);
        addAndMakeVisible(*midiInSelector_);
        addAndMakeVisible(*midiOutSelector_);

        populateOptions();
        setupRoutingCallbacks();
    }

    void resized() override {
        auto bounds = getLocalBounds();
        int halfH = bounds.getHeight() / 2;
        int halfW = bounds.getWidth() / 2;

        audioInSelector_->setBounds(0, 0, halfW, halfH);
        audioOutSelector_->setBounds(halfW, 0, bounds.getWidth() - halfW, halfH);
        midiInSelector_->setBounds(0, halfH, halfW, bounds.getHeight() - halfH);
        midiOutSelector_->setBounds(halfW, halfH, bounds.getWidth() - halfW,
                                    bounds.getHeight() - halfH);
    }

    void updateFromTrack() {
        const auto* track = TrackManager::getInstance().getTrack(trackId_);
        if (!track)
            return;

        auto* deviceManager = audioEngine_ ? audioEngine_->getDeviceManager() : nullptr;
        auto* device = deviceManager ? deviceManager->getCurrentAudioDevice() : nullptr;
        auto* midiBridge = audioEngine_ ? audioEngine_->getMidiBridge() : nullptr;

        juce::BigInteger enabledInputChannels, enabledOutputChannels;
        std::map<int, juce::String> teInputDeviceNames;
        if (auto* bridge = audioEngine_->getAudioBridge()) {
            enabledInputChannels = bridge->getEnabledInputChannels();
            enabledOutputChannels = bridge->getEnabledOutputChannels();
            teInputDeviceNames = bridge->getInputDeviceNamesByChannel();
        }

        RoutingSyncHelper::syncSelectorsFromTrack(
            *track, audioInSelector_.get(), midiInSelector_.get(), audioOutSelector_.get(),
            midiOutSelector_.get(), midiBridge, device, trackId_, outputTrackMapping_,
            midiOutputTrackMapping_, &inputTrackMapping_, enabledInputChannels,
            enabledOutputChannels, nullptr, teInputDeviceNames);
    }

    TrackId getTrackId() const {
        return trackId_;
    }

  private:
    TrackId trackId_;
    AudioEngine* audioEngine_;
    std::unique_ptr<RoutingSelector> audioInSelector_;
    std::unique_ptr<RoutingSelector> audioOutSelector_;
    std::unique_ptr<RoutingSelector> midiInSelector_;
    std::unique_ptr<RoutingSelector> midiOutSelector_;
    std::map<int, TrackId> inputTrackMapping_;
    std::map<int, TrackId> outputTrackMapping_;
    std::map<int, TrackId> midiOutputTrackMapping_;

    void populateOptions() {
        if (!audioEngine_)
            return;

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

        RoutingSyncHelper::populateAudioInputOptions(audioInSelector_.get(), device, trackId_,
                                                     &inputTrackMapping_, enabledInputChannels,
                                                     nullptr, teInputDeviceNames);
        RoutingSyncHelper::populateAudioOutputOptions(audioOutSelector_.get(), trackId_, device,
                                                      outputTrackMapping_, enabledOutputChannels);
        RoutingSyncHelper::populateMidiInputOptions(midiInSelector_.get(), midiBridge);
        RoutingSyncHelper::populateMidiOutputOptions(midiOutSelector_.get(), midiBridge,
                                                     midiOutputTrackMapping_);

        // Sync current track state into selectors
        updateFromTrack();
    }

    void setupRoutingCallbacks() {
        auto* midiBridge = audioEngine_ ? audioEngine_->getMidiBridge() : nullptr;

        audioInSelector_->onEnabledChanged = [this](bool enabled) {
            if (enabled) {
                midiInSelector_->setEnabled(false);
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

        audioInSelector_->onSelectionChanged = [this](int selectedId) {
            if (selectedId == 1) {
                TrackManager::getInstance().setTrackAudioInput(trackId_, "");
            } else if (selectedId >= 200) {
                auto it = inputTrackMapping_.find(selectedId);
                if (it != inputTrackMapping_.end())
                    TrackManager::getInstance().setTrackAudioInput(
                        trackId_, "track:" + juce::String(it->second));
            } else if (selectedId >= 10) {
                TrackManager::getInstance().setTrackAudioInput(trackId_, "default");
            }
        };

        midiInSelector_->onEnabledChanged = [this, midiBridge](bool enabled) {
            if (enabled) {
                audioInSelector_->setEnabled(false);
                TrackManager::getInstance().setTrackAudioInput(trackId_, "");
                int selectedId = midiInSelector_->getSelectedId();
                if (selectedId == 1) {
                    TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
                } else if (selectedId >= 10 && midiBridge) {
                    auto midiInputs = midiBridge->getAvailableMidiInputs();
                    int deviceIndex = selectedId - 10;
                    if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size()))
                        TrackManager::getInstance().setTrackMidiInput(trackId_,
                                                                      midiInputs[deviceIndex].id);
                    else
                        TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
                } else {
                    TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
                }
            } else {
                TrackManager::getInstance().setTrackMidiInput(trackId_, "");
            }
        };

        midiInSelector_->onSelectionChanged = [this, midiBridge](int selectedId) {
            if (selectedId == 2) {
                TrackManager::getInstance().setTrackMidiInput(trackId_, "");
            } else if (selectedId == 1) {
                TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
            } else if (selectedId >= 10 && midiBridge) {
                auto midiInputs = midiBridge->getAvailableMidiInputs();
                int deviceIndex = selectedId - 10;
                if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size()))
                    TrackManager::getInstance().setTrackMidiInput(trackId_,
                                                                  midiInputs[deviceIndex].id);
            }
        };

        audioOutSelector_->onEnabledChanged = [this](bool enabled) {
            if (enabled)
                TrackManager::getInstance().setTrackAudioOutput(trackId_, "master");
            else
                TrackManager::getInstance().setTrackAudioOutput(trackId_, "");
        };

        audioOutSelector_->onSelectionChanged = [this](int selectedId) {
            if (selectedId == 1) {
                TrackManager::getInstance().setTrackAudioOutput(trackId_, "master");
            } else if (selectedId == 2) {
                TrackManager::getInstance().setTrackAudioOutput(trackId_, "");
            } else if (selectedId >= 200) {
                auto it = outputTrackMapping_.find(selectedId);
                if (it != outputTrackMapping_.end())
                    TrackManager::getInstance().setTrackAudioOutput(
                        trackId_, "track:" + juce::String(it->second));
            } else if (selectedId >= 10) {
                TrackManager::getInstance().setTrackAudioOutput(trackId_, "master");
            }
        };

        midiOutSelector_->onEnabledChanged = [this](bool enabled) {
            if (!enabled)
                TrackManager::getInstance().setTrackMidiOutput(trackId_, "");
        };

        midiOutSelector_->onSelectionChanged = [this, midiBridge](int selectedId) {
            if (selectedId == 1) {
                TrackManager::getInstance().setTrackMidiOutput(trackId_, "");
            } else if (selectedId >= 200) {
                auto it = midiOutputTrackMapping_.find(selectedId);
                if (it != midiOutputTrackMapping_.end())
                    TrackManager::getInstance().setTrackMidiOutput(
                        trackId_, "track:" + juce::String(it->second));
            } else if (selectedId >= 10 && midiBridge) {
                auto midiOutputs = midiBridge->getAvailableMidiOutputs();
                int deviceIndex = selectedId - 10;
                if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiOutputs.size()))
                    TrackManager::getInstance().setTrackMidiOutput(trackId_,
                                                                   midiOutputs[deviceIndex].id);
            }
        };
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniIOStrip)
};

// MiniDbScale defined in ClipSlotButton.hpp

// Mini channel strip for session view fader row
class SessionView::MiniChannelStrip : public juce::Component {
  public:
    MiniChannelStrip(TrackId trackId, const TrackInfo& track) : trackId_(trackId) {
        trackColour_ = track.colour;

        // Volume fader (vertical TextSlider with dB format)
        volumeSlider_ =
            std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decibels);
        volumeSlider_->setOrientation(daw::ui::TextSlider::Orientation::Vertical);
        volumeSlider_->setRange(-60.0, 6.0, 0.1);
        volumeSlider_->setFont(FontManager::getInstance().getUIFont(9.0f));
        float db = gainToDb(track.volume);
        volumeSlider_->setValue(db, juce::dontSendNotification);
        volumeSlider_->onValueChanged = [this](double newValue) {
            float gain = dbToGain(static_cast<float>(newValue));
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackVolumeCommand>(trackId_, gain));
        };
        addAndMakeVisible(*volumeSlider_);

        // dB scale labels (between fader and meter)
        dbScale_ = std::make_unique<MiniDbScale>();
        addAndMakeVisible(*dbScale_);

        // Level meter
        levelMeter_ = std::make_unique<LevelMeter>();
        addAndMakeVisible(*levelMeter_);

        // Mute button (square corners, toggle)
        muteButton_ = std::make_unique<juce::TextButton>("M");
        muteButton_->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        muteButton_->setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        muteButton_->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFAA8855));
        muteButton_->setColour(juce::TextButton::textColourOffId,
                               DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        muteButton_->setColour(juce::TextButton::textColourOnId,
                               DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        muteButton_->setClickingTogglesState(true);
        muteButton_->setToggleState(track.muted, juce::dontSendNotification);
        muteButton_->onClick = [this]() {
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackMuteCommand>(trackId_, muteButton_->getToggleState()));
        };
        addAndMakeVisible(*muteButton_);

        // Solo button (square corners, toggle)
        soloButton_ = std::make_unique<juce::TextButton>("S");
        soloButton_->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        soloButton_->setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        soloButton_->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFAAAA55));
        soloButton_->setColour(juce::TextButton::textColourOffId,
                               DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        soloButton_->setColour(juce::TextButton::textColourOnId,
                               DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        soloButton_->setClickingTogglesState(true);
        soloButton_->setToggleState(track.soloed, juce::dontSendNotification);
        soloButton_->onClick = [this]() {
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackSoloCommand>(trackId_, soloButton_->getToggleState()));
        };
        addAndMakeVisible(*soloButton_);

        // Record arm button (square corners, toggle)
        recordButton_ = std::make_unique<juce::TextButton>("R");
        recordButton_->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        recordButton_->setColour(juce::TextButton::buttonColourId,
                                 DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        recordButton_->setColour(juce::TextButton::buttonOnColourId,
                                 DarkTheme::getColour(DarkTheme::STATUS_ERROR));
        recordButton_->setColour(juce::TextButton::textColourOffId,
                                 DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        recordButton_->setColour(juce::TextButton::textColourOnId,
                                 DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        recordButton_->setClickingTogglesState(true);
        recordButton_->setToggleState(track.recordArmed, juce::dontSendNotification);
        recordButton_->onClick = [this]() {
            TrackManager::getInstance().setTrackRecordArmed(trackId_,
                                                            recordButton_->getToggleState());
        };
        addAndMakeVisible(*recordButton_);

        // Monitor button (3-state: Off → In → Auto → Off)
        monitorButton_ = std::make_unique<juce::TextButton>("-");
        monitorButton_->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        monitorButton_->setColour(juce::TextButton::buttonColourId,
                                  DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        monitorButton_->setColour(juce::TextButton::buttonOnColourId,
                                  DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
        monitorButton_->setColour(juce::TextButton::textColourOffId,
                                  DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        monitorButton_->setColour(juce::TextButton::textColourOnId,
                                  DarkTheme::getColour(DarkTheme::BACKGROUND));
        monitorButton_->setTooltip("Input monitoring (Off/In/Auto)");
        monitorButton_->onClick = [this]() {
            auto* t = TrackManager::getInstance().getTrack(trackId_);
            if (!t)
                return;
            InputMonitorMode nextMode;
            switch (t->inputMonitor) {
                case InputMonitorMode::Off:
                    nextMode = InputMonitorMode::In;
                    break;
                case InputMonitorMode::In:
                    nextMode = InputMonitorMode::Auto;
                    break;
                case InputMonitorMode::Auto:
                    nextMode = InputMonitorMode::Off;
                    break;
            }
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackInputMonitorCommand>(trackId_, nextMode));
        };
        addAndMakeVisible(*monitorButton_);
        updateMonitorVisual(track.inputMonitor);

        // Pan slider (horizontal, compact)
        panSlider_ = std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Pan);
        panSlider_->setOrientation(daw::ui::TextSlider::Orientation::Horizontal);
        panSlider_->setRange(-1.0, 1.0, 0.01);
        panSlider_->setFont(FontManager::getInstance().getUIFont(8.0f));
        panSlider_->setValue(track.pan, juce::dontSendNotification);
        panSlider_->onValueChanged = [this](double newValue) {
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackPanCommand>(trackId_, static_cast<float>(newValue)));
        };
        addAndMakeVisible(*panSlider_);

        // Listen for mouse events on all children so we can intercept right-clicks
        volumeSlider_->addMouseListener(this, false);
        dbScale_->addMouseListener(this, false);
        levelMeter_->addMouseListener(this, false);
        muteButton_->addMouseListener(this, false);
        soloButton_->addMouseListener(this, false);
        recordButton_->addMouseListener(this, false);
        monitorButton_->addMouseListener(this, false);
        panSlider_->addMouseListener(this, false);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onContextMenu)
            onContextMenu();
    }

    std::function<void()> onContextMenu;

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        // Track colour bar at top (3px)
        g.setColour(trackColour_);
        g.fillRect(bounds.removeFromTop(3));
    }

    void setShowRecordMonitor(bool show) {
        recordButton_->setVisible(show);
        monitorButton_->setVisible(show);
        resized();
    }

    void resized() override {
        auto bounds = getLocalBounds();
        bounds.removeFromTop(3);  // colour bar

        // Button rows at bottom
        auto msRow = bounds.removeFromBottom(18);
        int halfW = msRow.getWidth() / 2;
        muteButton_->setBounds(msRow.removeFromLeft(halfW));
        soloButton_->setBounds(msRow);

        if (recordButton_->isVisible()) {
            auto rmRow = bounds.removeFromBottom(18);
            halfW = rmRow.getWidth() / 2;
            recordButton_->setBounds(rmRow.removeFromLeft(halfW));
            monitorButton_->setBounds(rmRow);
        }

        auto panRow = bounds.removeFromBottom(14);
        panSlider_->setBounds(panRow);

        // Layout: fader | dbScale | meter
        int meterW = juce::jmax(8, bounds.getWidth() * 30 / 100);
        auto meterBounds = bounds.removeFromRight(meterW);
        levelMeter_->setBounds(meterBounds.reduced(1, 2));

        // dB scale labels — narrow column between fader and meter
        static constexpr int DB_SCALE_WIDTH = 16;
        if (bounds.getWidth() > DB_SCALE_WIDTH + 20) {
            auto scaleBounds = bounds.removeFromRight(DB_SCALE_WIDTH);
            dbScale_->setBounds(scaleBounds.withTrimmedTop(2).withTrimmedBottom(2));
            dbScale_->setVisible(true);
        } else {
            dbScale_->setVisible(false);
        }

        volumeSlider_->setBounds(bounds.reduced(1, 0));
    }

    void setMeterLevels(float left, float right) {
        levelMeter_->setLevels(left, right);
    }

    void updateFromTrack(const TrackInfo& track) {
        float db = gainToDb(track.volume);
        volumeSlider_->setValue(db, juce::dontSendNotification);
        panSlider_->setValue(track.pan, juce::dontSendNotification);
        muteButton_->setToggleState(track.muted, juce::dontSendNotification);
        soloButton_->setToggleState(track.soloed, juce::dontSendNotification);
        recordButton_->setToggleState(track.recordArmed, juce::dontSendNotification);
        updateMonitorVisual(track.inputMonitor);
        trackColour_ = track.colour;
        repaint();
    }

    TrackId getTrackId() const {
        return trackId_;
    }

  private:
    TrackId trackId_;
    juce::Colour trackColour_;
    std::unique_ptr<daw::ui::TextSlider> volumeSlider_;
    std::unique_ptr<daw::ui::TextSlider> panSlider_;
    std::unique_ptr<MiniDbScale> dbScale_;
    std::unique_ptr<LevelMeter> levelMeter_;
    std::unique_ptr<juce::TextButton> muteButton_;
    std::unique_ptr<juce::TextButton> soloButton_;
    std::unique_ptr<juce::TextButton> recordButton_;
    std::unique_ptr<juce::TextButton> monitorButton_;

    void updateMonitorVisual(InputMonitorMode mode) {
        switch (mode) {
            case InputMonitorMode::Off:
                monitorButton_->setButtonText("-");
                break;
            case InputMonitorMode::In:
                monitorButton_->setButtonText("I");
                break;
            case InputMonitorMode::Auto:
                monitorButton_->setButtonText("A");
                break;
        }
        monitorButton_->setToggleState(mode != InputMonitorMode::Off, juce::dontSendNotification);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniChannelStrip)
};

// Mini master strip for session view (TextSlider + LevelMeter, orange accent)
class SessionView::MiniMasterStrip : public juce::Component {
  public:
    MiniMasterStrip() {
        volumeSlider_ =
            std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decibels);
        volumeSlider_->setOrientation(daw::ui::TextSlider::Orientation::Vertical);
        volumeSlider_->setRange(-60.0, 6.0, 0.1);
        volumeSlider_->setFont(FontManager::getInstance().getUIFont(9.0f));

        const auto& master = TrackManager::getInstance().getMasterChannel();
        float db = gainToDb(master.volume);
        volumeSlider_->setValue(db, juce::dontSendNotification);

        volumeSlider_->onValueChanged = [](double newValue) {
            float gain = dbToGain(static_cast<float>(newValue));
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetMasterVolumeCommand>(gain));
        };
        addAndMakeVisible(*volumeSlider_);

        levelMeter_ = std::make_unique<LevelMeter>();
        addAndMakeVisible(*levelMeter_);

        dbScale_ = std::make_unique<MiniDbScale>();
        addAndMakeVisible(*dbScale_);

        // Listen for mouse events on children for right-click context menu
        volumeSlider_->addMouseListener(this, false);
        levelMeter_->addMouseListener(this, false);
        dbScale_->addMouseListener(this, false);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onContextMenu)
            onContextMenu();
        else
            SelectionManager::getInstance().selectTrack(MASTER_TRACK_ID);
    }

    std::function<void()> onContextMenu;

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        // Orange accent bar at top
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.fillRect(bounds.removeFromTop(3));
    }

    void resized() override {
        auto bounds = getLocalBounds();
        bounds.removeFromTop(3);

        int meterW = juce::jmax(8, bounds.getWidth() * 40 / 100);
        auto meterBounds = bounds.removeFromRight(meterW);
        levelMeter_->setBounds(meterBounds.reduced(1, 2));

        // dB scale labels — narrow column between fader and meter
        static constexpr int DB_SCALE_WIDTH = 16;
        if (bounds.getWidth() > DB_SCALE_WIDTH + 20) {
            auto scaleBounds = bounds.removeFromRight(DB_SCALE_WIDTH);
            dbScale_->setBounds(scaleBounds.withTrimmedTop(2).withTrimmedBottom(2));
            dbScale_->setVisible(true);
        } else {
            dbScale_->setVisible(false);
        }

        volumeSlider_->setBounds(bounds.reduced(1, 0));
    }

    void updateVolume(float volume) {
        float db = gainToDb(volume);
        volumeSlider_->setValue(db, juce::dontSendNotification);
    }

    void setMeterLevels(float left, float right) {
        levelMeter_->setLevels(left, right);
    }

  private:
    std::unique_ptr<daw::ui::TextSlider> volumeSlider_;
    std::unique_ptr<MiniDbScale> dbScale_;
    std::unique_ptr<LevelMeter> levelMeter_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniMasterStrip)
};

SessionView::SessionView() {
    // Get current view mode
    currentViewMode_ = ViewModeController::getInstance().getViewMode();

    // Create header container for clipping
    headerContainer = std::make_unique<HeaderContainer>();
    headerContainer->onPaintOverChildren = [this](juce::Graphics& g) {
        paintHeaderDragFeedback(g);
    };
    addAndMakeVisible(*headerContainer);

    // Create scene container for clipping
    sceneContainer = std::make_unique<SceneContainer>();
    addAndMakeVisible(*sceneContainer);

    // Create viewport for scrollable grid with custom grid content
    gridContent = std::make_unique<GridContent>(CLIP_SLOT_HEIGHT, TRACK_SEPARATOR_WIDTH,
                                                CLIP_SLOT_MARGIN, numScenes_);
    gridContent->onContextMenu = [this]() {
        juce::PopupMenu menu;
        menu.addItem(1, "Add Scene");
        menu.addItem(2, "Remove Scene");
        auto safeThis = juce::Component::SafePointer<SessionView>(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result) {
            if (!safeThis)
                return;
            if (result == 1)
                safeThis->addScene();
            else if (result == 2)
                safeThis->removeScene();
        });
    };
    gridViewport = std::make_unique<GridViewport>();
    gridViewport->onContextMenu = [this]() {
        juce::PopupMenu menu;
        menu.addItem(1, "Add Scene");
        menu.addItem(2, "Remove Scene");
        auto safeThis = juce::Component::SafePointer<SessionView>(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result) {
            if (!safeThis)
                return;
            if (result == 1)
                safeThis->addScene();
            else if (result == 2)
                safeThis->removeScene();
        });
    };
    gridViewport->setViewedComponent(gridContent.get(), false);
    gridViewport->setScrollBarsShown(true, true);
    gridViewport->getHorizontalScrollBar().addListener(this);
    gridViewport->getVerticalScrollBar().addListener(this);
    addAndMakeVisible(*gridViewport);

    // Create per-track stop button container (pinned between grid and faders)
    stopButtonContainer = std::make_unique<StopButtonContainer>();
    stopButtonContainer->onContextMenu = [this]() { showMixerContextMenu(); };
    addAndMakeVisible(*stopButtonContainer);

    // Create I/O routing container (between stop buttons and faders, hidden by default)
    ioContainer_ = std::make_unique<IOContainer>();
    ioContainer_->onContextMenu = [this]() { showMixerContextMenu(); };
    addChildComponent(*ioContainer_);

    // Create send section container (between stop buttons and IO row, hidden by default)
    sendSectionContainer_ = std::make_unique<SendSectionContainer>();
    sendSectionContainer_->onContextMenu = [this]() { showMixerContextMenu(); };
    addChildComponent(*sendSectionContainer_);

    // Create send resize handle (top edge of send section)
    sendResizeHandle_ = std::make_unique<ResizeHandle>(ResizeHandle::Vertical);
    sendResizeHandle_->onResizeStart = [this]() { dragStartSendHeight_ = sendSectionHeight_; };
    sendResizeHandle_->onResize = [this](int delta) {
        sendSectionHeight_ = juce::jlimit(MIN_SEND_SECTION_HEIGHT, MAX_SEND_SECTION_HEIGHT,
                                          dragStartSendHeight_ - delta);
        resized();
    };
    sendResizeHandle_->onContextMenu = [this]() { showMixerContextMenu(); };
    addChildComponent(*sendResizeHandle_);

    // Create fader container at the bottom
    faderContainer = std::make_unique<FaderContainer>();
    addAndMakeVisible(*faderContainer);

    // Create resize handle between stop button row and fader row
    faderResizeHandle_ = std::make_unique<ResizeHandle>(ResizeHandle::Vertical);
    faderResizeHandle_->onResizeStart = [this]() { dragStartFaderHeight_ = faderRowHeight_; };
    faderResizeHandle_->onResize = [this](int delta) {
        faderRowHeight_ =
            juce::jlimit(MIN_FADER_ROW_HEIGHT, MAX_FADER_ROW_HEIGHT, dragStartFaderHeight_ - delta);
        resized();
    };
    faderResizeHandle_->onContextMenu = [this]() { showMixerContextMenu(); };
    addAndMakeVisible(*faderResizeHandle_);

    setupSceneButtons();

    // Master label (top-right corner, above scene buttons)
    masterLabel_ = std::make_unique<juce::TextButton>("Master");
    masterLabel_->setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    masterLabel_->setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    masterLabel_->setLookAndFeel(&daw::ui::SmallButtonLookAndFeel::getInstance());
    masterLabel_->onClick = [this]() {
        SelectionManager::getInstance().selectTrack(MASTER_TRACK_ID);
    };
    addAndMakeVisible(*masterLabel_);

    // Create master strip in the fader row (scene column area)
    masterStrip_ = std::make_unique<MiniMasterStrip>();
    masterStrip_->onContextMenu = [this]() { showMixerContextMenu(); };
    addAndMakeVisible(*masterStrip_);

    // Create drag ghost label for file drag preview (added to grid content)
    dragGhostLabel_ = std::make_unique<juce::Label>();
    dragGhostLabel_->setFont(FontManager::getInstance().getUIFontBold(11.0f));
    dragGhostLabel_->setJustificationType(juce::Justification::centred);
    dragGhostLabel_->setColour(juce::Label::backgroundColourId,
                               DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.6f));
    dragGhostLabel_->setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    dragGhostLabel_->setColour(juce::Label::outlineColourId,
                               DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    dragGhostLabel_->setVisible(false);
    gridContent->addAndMakeVisible(*dragGhostLabel_);

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Register as ClipManager listener
    ClipManager::getInstance().addListener(this);

    // Register as ViewModeController listener
    ViewModeController::getInstance().addListener(this);

    // Build tracks from TrackManager
    rebuildTracks();
}

SessionView::~SessionView() {
    stopTimer();
    TrackManager::getInstance().removeListener(this);
    ClipManager::getInstance().removeListener(this);
    ViewModeController::getInstance().removeListener(this);
    gridViewport->getHorizontalScrollBar().removeListener(this);
    gridViewport->getVerticalScrollBar().removeListener(this);
}

void SessionView::tracksChanged() {
    rebuildTracks();
}

void SessionView::trackPropertyChanged(int trackId) {
    // Find the track in our visible list
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (!track)
        return;

    // Find index in visible track IDs
    int index = -1;
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        if (visibleTrackIds_[i] == trackId) {
            index = static_cast<int>(i);
            break;
        }
    }

    if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
        // Update header text with collapse indicator for groups
        juce::String headerText = track->name;
        if (track->isGroup()) {
            bool collapsed = track->isCollapsedIn(currentViewMode_);
            headerText = (collapsed ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6 "))
                                    : juce::String(juce::CharPointer_UTF8("\xe2\x96\xbc "))) +
                         track->name;
        }
        trackHeaders[index]->setButtonText(headerText);

        // Sync strip state (volume, mute, solo, colour)
        if (index < static_cast<int>(trackMiniStrips_.size())) {
            trackMiniStrips_[index]->updateFromTrack(*track);
        }

        // Sync IO strip routing state
        if (index < static_cast<int>(trackIOStrips_.size())) {
            trackIOStrips_[index]->updateFromTrack();
        }

        // Sync send strip state
        if (index < static_cast<int>(trackSendStrips_.size())) {
            trackSendStrips_[index]->updateFromTrack();
        }
    }
}

void SessionView::trackDevicesChanged(TrackId trackId) {
    // Sends are notified via trackDevicesChanged — forward to trackPropertyChanged
    trackPropertyChanged(trackId);
}

void SessionView::viewModeChanged(ViewMode mode, const AudioEngineProfile& /*profile*/) {
    currentViewMode_ = mode;
    rebuildTracks();
}

void SessionView::masterChannelChanged() {
    // Update master strip from master channel state
    if (masterStrip_) {
        const auto& master = TrackManager::getInstance().getMasterChannel();
        masterStrip_->updateVolume(master.volume);
    }
}

int SessionView::getTrackX(int trackIndex) const {
    int x = 0;
    for (int i = 0; i < trackIndex && i < static_cast<int>(trackColumnWidths_.size()); ++i) {
        x += trackColumnWidths_[i] + TRACK_SEPARATOR_WIDTH;
    }
    return x;
}

int SessionView::getTotalTracksWidth() const {
    int total = 0;
    for (size_t i = 0; i < trackColumnWidths_.size(); ++i) {
        total += trackColumnWidths_[i];
        if (i < trackColumnWidths_.size() - 1)
            total += TRACK_SEPARATOR_WIDTH;
    }
    // Add final separator if there are tracks
    if (!trackColumnWidths_.empty())
        total += TRACK_SEPARATOR_WIDTH;
    return total;
}

int SessionView::getTrackIndexAtX(int x) const {
    int cumX = 0;
    for (int i = 0; i < static_cast<int>(trackColumnWidths_.size()); ++i) {
        cumX += trackColumnWidths_[i] + TRACK_SEPARATOR_WIDTH;
        if (x < cumX)
            return i;
    }
    return -1;
}

void SessionView::rebuildTracks() {
    // Clear existing track headers, clip slots, stop buttons, strips, IO strips, and send strips
    trackHeaders.clear();
    clipSlots.clear();
    trackStopButtons.clear();
    trackMiniStrips_.clear();
    trackIOStrips_.clear();
    trackSendViewports_.clear();
    trackSendStrips_.clear();
    trackResizeHandles_.clear();
    visibleTrackIds_.clear();

    auto& trackManager = TrackManager::getInstance();

    // Build hierarchical list of visible track IDs (respecting collapse state)
    std::function<void(TrackId)> addTrackRecursive = [&](TrackId trackId) {
        const auto* track = trackManager.getTrack(trackId);
        if (!track || !track->isVisibleIn(currentViewMode_))
            return;

        visibleTrackIds_.push_back(trackId);

        // Add children if group is not collapsed
        if (track->isGroup() && !track->isCollapsedIn(currentViewMode_)) {
            for (auto childId : track->childIds) {
                addTrackRecursive(childId);
            }
        }
    };

    // Start with visible top-level tracks
    auto topLevelTracks = trackManager.getVisibleTopLevelTracks(currentViewMode_);
    for (auto trackId : topLevelTracks) {
        addTrackRecursive(trackId);
    }

    int numTracks = static_cast<int>(visibleTrackIds_.size());

    // Initialize per-track widths (preserve existing widths where possible)
    std::vector<int> oldWidths = trackColumnWidths_;
    trackColumnWidths_.resize(numTracks, DEFAULT_CLIP_SLOT_WIDTH);
    for (int i = 0; i < numTracks && i < static_cast<int>(oldWidths.size()); ++i) {
        trackColumnWidths_[i] = oldWidths[i];
    }

    // Create per-track resize handles
    trackResizeHandles_.clear();
    for (int i = 0; i < numTracks; ++i) {
        auto handle = std::make_unique<ResizeHandle>(ResizeHandle::Horizontal);
        int trackIdx = i;
        handle->onResizeStart = [this, trackIdx]() {
            dragStartTrackWidth_ = trackColumnWidths_[trackIdx];
        };
        handle->onResize = [this, trackIdx](int delta) {
            trackColumnWidths_[trackIdx] =
                juce::jlimit(MIN_TRACK_WIDTH, MAX_TRACK_WIDTH, dragStartTrackWidth_ + delta);
            resized();
        };
        headerContainer->addAndMakeVisible(*handle);
        trackResizeHandles_.push_back(std::move(handle));
    }

    // Update grid content track count
    gridContent->setNumTracks(numTracks);

    // Create track headers for visible tracks only
    for (int i = 0; i < numTracks; ++i) {
        const auto* track = trackManager.getTrack(visibleTrackIds_[i]);
        if (!track)
            continue;

        auto header = std::make_unique<TrackHeaderButton>();

        // Show collapse indicator for groups
        juce::String headerText = track->name;
        if (track->isGroup()) {
            bool collapsed = track->isCollapsedIn(currentViewMode_);
            headerText = (collapsed ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6 "))   // ▶
                                    : juce::String(juce::CharPointer_UTF8("\xe2\x96\xbc ")))  // ▼
                         + track->name;
        }
        header->setColour(juce::TextButton::buttonColourId, track->colour.withAlpha(0.5f));

        header->setButtonText(headerText);
        header->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        header->setLookAndFeel(&daw::ui::SmallButtonLookAndFeel::getInstance());

        // Click handler - select track and toggle collapse for groups
        TrackId trackId = track->id;
        header->onClick = [this, trackId]() {
            // Always select the track
            selectTrack(trackId);

            // Additionally toggle collapse for groups
            const auto* t = TrackManager::getInstance().getTrack(trackId);
            if (t && t->isGroup()) {
                bool collapsed = t->isCollapsedIn(currentViewMode_);
                TrackManager::getInstance().setTrackCollapsed(trackId, !collapsed);
            }
        };

        header->onDeleteTrack = [trackId]() {
            UndoManager::getInstance().executeCommand(
                std::make_unique<DeleteTrackCommand>(trackId));
        };

        int headerIdx = i;
        header->onHeaderMouseDown = [this, headerIdx](const juce::MouseEvent&) {
            headerDragIndex_ = headerIdx;
            headerDragStartX_ = getTrackX(headerIdx) + trackColumnWidths_[headerIdx] / 2;
        };
        header->onHeaderMouseDrag = [this](const juce::MouseEvent& e) {
            if (headerDragIndex_ < 0)
                return;
            auto localE = e.getEventRelativeTo(headerContainer.get());
            int dx = std::abs(localE.x - (headerDragStartX_ - trackHeaderScrollOffset));
            if (!headerIsDragging_ && dx > HEADER_DRAG_THRESHOLD)
                headerIsDragging_ = true;
            if (headerIsDragging_) {
                calculateHeaderDropTarget(localE.x + trackHeaderScrollOffset);
                headerContainer->repaint();
            }
        };
        header->onHeaderMouseUp = [this](const juce::MouseEvent&) {
            if (headerIsDragging_)
                executeHeaderDrop();
            resetHeaderDragState();
        };

        headerContainer->addAndMakeVisible(*header);
        trackHeaders.push_back(std::move(header));
    }

    // Create clip slots for each visible track
    for (int track = 0; track < numTracks; ++track) {
        std::vector<std::unique_ptr<juce::TextButton>> trackSlots;
        TrackId slotTrackId = visibleTrackIds_[track];
        const auto* slotTrack = trackManager.getTrack(slotTrackId);
        bool isGroup = slotTrack && slotTrack->isGroup();

        for (int scene = 0; scene < numScenes_; ++scene) {
            auto slot = std::make_unique<ClipSlotButton>();

            slot->setButtonText("");
            slot->isGroupSlot = isGroup;
            slot->setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
            slot->setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

            wireClipSlotCallbacks(*slot, track, scene);

            gridContent->addAndMakeVisible(*slot);
            trackSlots.push_back(std::move(slot));
        }

        clipSlots.push_back(std::move(trackSlots));
    }

    // Create mini channel strips (TextSlider + LevelMeter + M/S buttons)
    for (int i = 0; i < numTracks; ++i) {
        TrackId trackId = visibleTrackIds_[i];
        const auto* track = trackManager.getTrack(trackId);
        if (!track)
            continue;

        auto strip = std::make_unique<MiniChannelStrip>(trackId, *track);
        strip->onContextMenu = [this]() { showMixerContextMenu(); };
        strip->setShowRecordMonitor(recordMonitorVisible_);
        faderContainer->addAndMakeVisible(*strip);
        trackMiniStrips_.push_back(std::move(strip));
    }

    // Create mini I/O routing strips per track
    for (int i = 0; i < numTracks; ++i) {
        TrackId trackId = visibleTrackIds_[i];
        auto ioStrip = std::make_unique<MiniIOStrip>(trackId, audioEngine_);
        ioContainer_->addAndMakeVisible(*ioStrip);
        trackIOStrips_.push_back(std::move(ioStrip));
    }

    // Create mini send strips per track (each in a viewport for scrolling)
    for (int i = 0; i < numTracks; ++i) {
        TrackId trackId = visibleTrackIds_[i];
        auto strip = std::make_unique<MiniSendStrip>(trackId);
        auto viewport = std::make_unique<juce::Viewport>();
        viewport->setViewedComponent(strip.get(), false);
        viewport->setScrollBarsShown(true, false);
        sendSectionContainer_->addAndMakeVisible(*viewport);
        strip->updateFromTrack();
        trackSendViewports_.push_back(std::move(viewport));
        trackSendStrips_.push_back(std::move(strip));
    }

    // Create per-track stop buttons
    for (int i = 0; i < numTracks; ++i) {
        auto stopBtn = std::make_unique<juce::TextButton>();
        stopBtn->setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x96\xa0")));  // ■
        stopBtn->setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
        stopBtn->setColour(juce::TextButton::textColourOffId,
                           DarkTheme::getColour(DarkTheme::STATUS_ERROR));
        stopBtn->setLookAndFeel(&daw::ui::SmallButtonLookAndFeel::getInstance());

        TrackId trackId = visibleTrackIds_[i];
        stopBtn->onClick = [this, trackId]() {
            auto& clipManager = ClipManager::getInstance();
            // Stop all session clips on this track by iterating scene slots
            for (int scene = 0; scene < numScenes_; ++scene) {
                ClipId clipId = clipManager.getClipInSlot(trackId, scene);
                if (clipId != INVALID_CLIP_ID) {
                    clipManager.stopClip(clipId);
                }
            }
        };

        stopButtonContainer->addAndMakeVisible(*stopBtn);
        trackStopButtons.push_back(std::move(stopBtn));
    }

    resized();
    updateHeaderSelectionVisuals();

    // Populate all clip slots with their current clip data
    updateAllClipSlots();
}

void SessionView::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

    // Fill the fader row background in the master fader area (scene column)
    auto faderBounds = faderContainer->getBounds();
    g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    g.fillRect(faderBounds.getRight(), faderBounds.getY(), getWidth() - faderBounds.getRight(),
               faderBounds.getHeight());
}

void SessionView::paintOverChildren(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));

    // Vertical separator on left edge of scene column
    auto sceneBounds = sceneContainer->getBounds();
    g.fillRect(sceneBounds.getX() - 1, 0, 1, getHeight());

    // Horizontal separator on top of stop button row (full width)
    auto stopContainerBounds = stopButtonContainer->getBounds();
    g.fillRect(0, stopContainerBounds.getY(), getWidth(), 1);

    // Plugin drag overlay
    if (showPluginDropOverlay_) {
        if (pluginDropTrackIndex_ >= 0 &&
            pluginDropTrackIndex_ < static_cast<int>(visibleTrackIds_.size())) {
            // Highlight the specific track column
            int trackX = getTrackX(pluginDropTrackIndex_) - trackHeaderScrollOffset;
            int trackW = (pluginDropTrackIndex_ < static_cast<int>(trackColumnWidths_.size()))
                             ? trackColumnWidths_[pluginDropTrackIndex_]
                             : DEFAULT_CLIP_SLOT_WIDTH;
            auto vpBounds = gridViewport->getBounds();
            auto colBounds = juce::Rectangle<int>(trackX, 0, trackW, vpBounds.getBottom());
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.2f));
            g.fillRect(colBounds);
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.5f));
            g.drawRect(colBounds, 2);
        } else {
            // Past last track — show "new track" indicator
            auto vpBounds = gridViewport->getBounds();
            int lastTrackEnd = getTotalTracksWidth() - trackHeaderScrollOffset;
            int indicatorW = DEFAULT_CLIP_SLOT_WIDTH;
            auto indicatorBounds =
                juce::Rectangle<int>(lastTrackEnd, 0, indicatorW, vpBounds.getBottom());
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.12f));
            g.fillRect(indicatorBounds);
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.35f));
            g.drawRect(indicatorBounds, 2);

            // Draw "+" icon
            auto centre = indicatorBounds.getCentre().toFloat();
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.6f));
            g.drawLine(centre.getX() - 8, centre.getY(), centre.getX() + 8, centre.getY(), 2.0f);
            g.drawLine(centre.getX(), centre.getY() - 8, centre.getX(), centre.getY() + 8, 2.0f);
        }
    }

    // File drag "new track" overlay (when dragging audio files past last track)
    if (dragHoverTrackIndex_ == -1 && dragHoverSceneIndex_ >= 0) {
        auto vpBounds = gridViewport->getBounds();
        int lastTrackEnd = getTotalTracksWidth() - trackHeaderScrollOffset;
        int indicatorW = DEFAULT_CLIP_SLOT_WIDTH;
        auto indicatorBounds =
            juce::Rectangle<int>(lastTrackEnd, 0, indicatorW, vpBounds.getBottom());
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.12f));
        g.fillRect(indicatorBounds);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.35f));
        g.drawRect(indicatorBounds, 2);

        // Draw "+" icon
        auto centre = indicatorBounds.getCentre().toFloat();
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.6f));
        g.drawLine(centre.getX() - 8, centre.getY(), centre.getX() + 8, centre.getY(), 2.0f);
        g.drawLine(centre.getX(), centre.getY() - 8, centre.getX(), centre.getY() + 8, 2.0f);
    }
}

void SessionView::resized() {
    auto bounds = getLocalBounds();

    int numTracks = static_cast<int>(trackHeaders.size());
    int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;

    // Fader row at the bottom (tracks area + master strip in scene column)
    auto faderRow = bounds.removeFromBottom(faderRowHeight_);
    auto masterFaderArea = faderRow.removeFromRight(SCENE_BUTTON_WIDTH);
    if (masterStrip_)
        masterStrip_->setBounds(masterFaderArea.reduced(2));
    faderContainer->setBounds(faderRow);
    faderContainer->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                   trackHeaderScrollOffset);

    // Position mini channel strips within fader container (synced with grid horizontal scroll)
    for (int i = 0; i < numTracks && i < static_cast<int>(trackMiniStrips_.size()); ++i) {
        int x = getTrackX(i) - trackHeaderScrollOffset;
        int w = trackColumnWidths_[i];
        trackMiniStrips_[i]->setBounds(x + 1, 1, w - 2, faderRowHeight_ - 2);
    }

    // Resize handle between IO/stop row and fader row
    auto resizeHandleRow = bounds.removeFromBottom(4);
    faderResizeHandle_->setBounds(resizeHandleRow);

    // I/O routing row (conditional, between stop buttons and resize handle)
    if (ioRowVisible_) {
        auto ioRow = bounds.removeFromBottom(IO_ROW_HEIGHT);
        ioContainer_->setBounds(ioRow);
        ioContainer_->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                     trackHeaderScrollOffset);
        ioContainer_->setVisible(true);

        for (int i = 0; i < numTracks && i < static_cast<int>(trackIOStrips_.size()); ++i) {
            int x = getTrackX(i) - trackHeaderScrollOffset;
            int w = trackColumnWidths_[i];
            trackIOStrips_[i]->setBounds(x + 1, 1, w - 2, IO_ROW_HEIGHT - 2);
        }
    } else {
        ioContainer_->setVisible(false);
    }

    // Send section (conditional, between stop buttons and IO row)
    if (sendRowVisible_) {
        auto sendRow = bounds.removeFromBottom(sendSectionHeight_);
        auto sendHandleRow = bounds.removeFromBottom(4);
        sendResizeHandle_->setBounds(sendHandleRow);
        sendResizeHandle_->setVisible(true);
        sendSectionContainer_->setBounds(sendRow);
        sendSectionContainer_->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                              trackHeaderScrollOffset);
        sendSectionContainer_->setVisible(true);

        for (int i = 0; i < numTracks && i < static_cast<int>(trackSendViewports_.size()); ++i) {
            int x = getTrackX(i) - trackHeaderScrollOffset;
            int w = trackColumnWidths_[i];
            trackSendViewports_[i]->setBounds(x + 1, 1, w - 2, sendSectionHeight_ - 2);
            if (i < static_cast<int>(trackSendStrips_.size())) {
                trackSendStrips_[i]->setSize(w - 2, trackSendStrips_[i]->getHeight());
            }
        }
    } else {
        sendSectionContainer_->setVisible(false);
        sendResizeHandle_->setVisible(false);
    }

    // Stop button row (full width: per-track stops + Stop All in scene column)
    auto stopRow = bounds.removeFromBottom(STOP_BUTTON_ROW_HEIGHT);
    auto stopAllArea = stopRow.removeFromRight(SCENE_BUTTON_WIDTH);
    stopAllButton->setBounds(stopAllArea.reduced(2));
    stopButtonContainer->setBounds(stopRow);
    stopButtonContainer->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                        trackHeaderScrollOffset);

    // Position per-track stop buttons (synced with grid horizontal scroll)
    for (int i = 0; i < numTracks && i < static_cast<int>(trackStopButtons.size()); ++i) {
        int x = getTrackX(i) - trackHeaderScrollOffset;
        int w = trackColumnWidths_[i];
        trackStopButtons[i]->setBounds(x + 2, 2, w - 4, STOP_BUTTON_ROW_HEIGHT - 4);
    }

    // Top row: Master label in scene column corner, headers in tracks area
    auto topRow = bounds.removeFromTop(TRACK_HEADER_HEIGHT);
    auto cornerArea = topRow.removeFromRight(SCENE_BUTTON_WIDTH);
    masterLabel_->setBounds(cornerArea.reduced(2));
    headerContainer->setBounds(topRow);
    headerContainer->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                    trackHeaderScrollOffset);

    // Position track headers and resize handles within header container (synced with grid scroll)
    for (int i = 0; i < numTracks; ++i) {
        int x = getTrackX(i) - trackHeaderScrollOffset;
        int w = trackColumnWidths_[i];
        trackHeaders[i]->setBounds(x + 2, 2, w - 4, TRACK_HEADER_HEIGHT - 4);

        // Position resize handle at right edge of header
        if (i < static_cast<int>(trackResizeHandles_.size())) {
            trackResizeHandles_[i]->setBounds(x + w - 2, 0, 4, TRACK_HEADER_HEIGHT);
        }
    }

    // Scene container on the right of remaining area
    auto sceneArea = bounds.removeFromRight(SCENE_BUTTON_WIDTH);
    sceneContainer->setBounds(sceneArea);

    // Position scene buttons within scene container (synced with grid scroll)
    for (int i = 0; i < static_cast<int>(sceneButtons.size()); ++i) {
        int y = i * sceneRowHeight - sceneButtonScrollOffset;
        sceneButtons[i]->setBounds(2, y, SCENE_BUTTON_WIDTH - 4, CLIP_SLOT_HEIGHT);
    }

    // Grid viewport takes remaining space (below headers, above stop buttons)
    gridViewport->setBounds(bounds);
    gridViewport->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH);

    // Size the grid content to fit the scenes
    int gridWidth = getTotalTracksWidth();
    int gridHeight = numScenes_ * sceneRowHeight;
    gridContent->setSize(gridWidth, gridHeight);
    gridContent->setTrackWidths(trackColumnWidths_);

    // Position clip slots within grid content
    for (int track = 0; track < numTracks; ++track) {
        int trackX = getTrackX(track);
        int w = trackColumnWidths_[track];
        int numSlotsForTrack = static_cast<int>(clipSlots[track].size());
        for (int scene = 0; scene < numSlotsForTrack; ++scene) {
            int y = scene * sceneRowHeight;
            clipSlots[track][scene]->setBounds(trackX, y, w, CLIP_SLOT_HEIGHT);
        }
    }
}

void SessionView::scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) {
    int numTracks = static_cast<int>(trackHeaders.size());

    if (scrollBar == &gridViewport->getHorizontalScrollBar()) {
        trackHeaderScrollOffset = static_cast<int>(newRangeStart);
        // Reposition headers and resize handles
        for (int i = 0; i < numTracks; ++i) {
            int x = getTrackX(i) - trackHeaderScrollOffset;
            int w = trackColumnWidths_[i];
            trackHeaders[i]->setBounds(x + 2, 2, w - 4, TRACK_HEADER_HEIGHT - 4);
            if (i < static_cast<int>(trackResizeHandles_.size())) {
                trackResizeHandles_[i]->setBounds(x + w - 2, 0, 4, TRACK_HEADER_HEIGHT);
            }
        }
        headerContainer->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                        trackHeaderScrollOffset);

        // Reposition mini channel strips to sync with horizontal scroll
        for (int i = 0; i < numTracks && i < static_cast<int>(trackMiniStrips_.size()); ++i) {
            int x = getTrackX(i) - trackHeaderScrollOffset;
            int w = trackColumnWidths_[i];
            trackMiniStrips_[i]->setBounds(x + 1, 1, w - 2, faderRowHeight_ - 2);
        }
        faderContainer->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                       trackHeaderScrollOffset);

        // Reposition stop + arrangement buttons to sync with horizontal scroll
        for (int i = 0; i < numTracks && i < static_cast<int>(trackStopButtons.size()); ++i) {
            int x = getTrackX(i) - trackHeaderScrollOffset;
            int w = trackColumnWidths_[i];
            trackStopButtons[i]->setBounds(x + 2, 2, w - 4, STOP_BUTTON_ROW_HEIGHT - 4);
        }
        stopButtonContainer->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                            trackHeaderScrollOffset);

        // Reposition IO strips to sync with horizontal scroll
        if (ioRowVisible_) {
            for (int i = 0; i < numTracks && i < static_cast<int>(trackIOStrips_.size()); ++i) {
                int x = getTrackX(i) - trackHeaderScrollOffset;
                int w = trackColumnWidths_[i];
                trackIOStrips_[i]->setBounds(x + 1, 1, w - 2, IO_ROW_HEIGHT - 2);
            }
            ioContainer_->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                         trackHeaderScrollOffset);
        }

        // Reposition send section viewports to sync with horizontal scroll
        if (sendRowVisible_) {
            for (int i = 0; i < numTracks && i < static_cast<int>(trackSendViewports_.size());
                 ++i) {
                int x = getTrackX(i) - trackHeaderScrollOffset;
                int w = trackColumnWidths_[i];
                trackSendViewports_[i]->setBounds(x + 1, 1, w - 2, sendSectionHeight_ - 2);
                if (i < static_cast<int>(trackSendStrips_.size())) {
                    trackSendStrips_[i]->setSize(w - 2, trackSendStrips_[i]->getHeight());
                }
            }
            sendSectionContainer_->setTrackLayout(numTracks, trackColumnWidths_,
                                                  TRACK_SEPARATOR_WIDTH, trackHeaderScrollOffset);
        }

        // Update viewport background separators
        gridViewport->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH);
    } else if (scrollBar == &gridViewport->getVerticalScrollBar()) {
        sceneButtonScrollOffset = static_cast<int>(newRangeStart);
        // Reposition scene buttons
        int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;
        for (int i = 0; i < static_cast<int>(sceneButtons.size()); ++i) {
            int y = i * sceneRowHeight - sceneButtonScrollOffset;
            sceneButtons[i]->setBounds(2, y, SCENE_BUTTON_WIDTH - 4, CLIP_SLOT_HEIGHT);
        }
        sceneContainer->repaint();
    }
}

void SessionView::setupSceneButtons() {
    sceneButtons.clear();

    for (int i = 0; i < numScenes_; ++i) {
        auto btn = std::make_unique<juce::TextButton>();
        btn->setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6")));  // ▶
        btn->setColour(juce::TextButton::buttonColourId,
                       DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        btn->setColour(juce::TextButton::textColourOffId,
                       DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        btn->setLookAndFeel(&daw::ui::SmallButtonLookAndFeel::getInstance());
        btn->onClick = [this, i]() { onSceneLaunched(i); };
        sceneContainer->addAndMakeVisible(*btn);
        sceneButtons.push_back(std::move(btn));
    }

    // Stop all button (pinned in stop button row, not in scene container)
    stopAllButton = std::make_unique<juce::TextButton>();
    stopAllButton->setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x96\xa0")));  // ■
    stopAllButton->setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    stopAllButton->setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    stopAllButton->setLookAndFeel(&daw::ui::SmallButtonLookAndFeel::getInstance());
    stopAllButton->onClick = [this]() { onStopAllClicked(); };
    addAndMakeVisible(*stopAllButton);
}

void SessionView::addScene() {
    numScenes_++;
    gridContent->setNumScenes(numScenes_);

    // Add a new scene button
    int sceneIndex = numScenes_ - 1;
    auto btn = std::make_unique<juce::TextButton>();
    btn->setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6")));  // ▶
    btn->setColour(juce::TextButton::buttonColourId,
                   DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    btn->setColour(juce::TextButton::textColourOffId,
                   DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    btn->setLookAndFeel(&daw::ui::SmallButtonLookAndFeel::getInstance());
    btn->onClick = [this, sceneIndex]() { onSceneLaunched(sceneIndex); };
    sceneContainer->addAndMakeVisible(*btn);
    sceneButtons.push_back(std::move(btn));

    // Add new clip slots for each track
    int numTracks = static_cast<int>(visibleTrackIds_.size());
    for (int track = 0; track < numTracks; ++track) {
        auto slot = std::make_unique<ClipSlotButton>();
        slot->setButtonText("");
        slot->setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        slot->setColour(juce::TextButton::textColourOffId,
                        DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

        wireClipSlotCallbacks(*slot, track, sceneIndex);

        gridContent->addAndMakeVisible(*slot);
        clipSlots[track].push_back(std::move(slot));
    }

    resized();
    updateAllClipSlots();

    // Scroll to show the newly added scene
    int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;
    int newSceneBottom = numScenes_ * sceneRowHeight;
    int viewportHeight = gridViewport->getViewHeight();
    if (newSceneBottom > viewportHeight) {
        gridViewport->setViewPosition(gridViewport->getViewPositionX(),
                                      newSceneBottom - viewportHeight);
    }
}

void SessionView::removeScene() {
    if (numScenes_ <= 1)
        return;

    int lastScene = numScenes_ - 1;

    // Check if any clips exist in the last scene
    auto& clipManager = ClipManager::getInstance();
    bool hasClips = false;
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        ClipId clipId = clipManager.getClipInSlot(visibleTrackIds_[i], lastScene);
        if (clipId != INVALID_CLIP_ID) {
            hasClips = true;
            break;
        }
    }

    if (hasClips) {
        // Show confirmation dialog before deleting a scene with clips
        auto options = juce::MessageBoxOptions()
                           .withIconType(juce::MessageBoxIconType::WarningIcon)
                           .withTitle("Delete Scene")
                           .withMessage("Scene " + juce::String(lastScene + 1) +
                                        " contains clips. Are you sure you want to delete it?")
                           .withButton("Delete")
                           .withButton("Cancel");

        auto safeThis = juce::Component::SafePointer<SessionView>(this);
        juce::AlertWindow::showAsync(options, [safeThis](int result) {
            if (safeThis && result == 1) {
                safeThis->removeSceneAsync(safeThis->numScenes_ - 1);
            }
        });
    } else {
        removeSceneAsync(lastScene);
    }
}

void SessionView::removeSceneAsync(int sceneIndex) {
    // Re-validate: the scene must still be the last one and within bounds
    if (sceneIndex != numScenes_ - 1 || numScenes_ <= 1)
        return;

    // Stop and delete any clips in this scene
    auto& clipManager = ClipManager::getInstance();
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        ClipId clipId = clipManager.getClipInSlot(visibleTrackIds_[i], sceneIndex);
        if (clipId != INVALID_CLIP_ID) {
            clipManager.stopClip(clipId);
            clipManager.deleteClip(clipId);
        }
    }

    // Remove the last scene button
    sceneButtons.pop_back();

    // Remove the last clip slot from each track
    for (auto& trackSlots : clipSlots) {
        if (!trackSlots.empty()) {
            trackSlots.pop_back();
        }
    }

    numScenes_--;
    gridContent->setNumScenes(numScenes_);

    resized();
    updateAllClipSlots();
}

void SessionView::wireClipSlotCallbacks(ClipSlotButton& slot, int trackIndex, int sceneIndex) {
    if (slot.isGroupSlot) {
        // Group slots: play button triggers/stops all descendant clips in this scene
        slot.onPlayButtonClick = [this, trackIndex, sceneIndex]() {
            TrackId groupId = visibleTrackIds_[trackIndex];
            triggerGroupScene(groupId, sceneIndex);
        };
        slot.onAddScene = [this]() { addScene(); };
        slot.onRemoveScene = [this]() { removeScene(); };
        return;
    }

    slot.onSingleClick = [this, trackIndex, sceneIndex]() {
        onClipSlotClicked(trackIndex, sceneIndex);
    };
    slot.onPlayButtonClick = [this, trackIndex, sceneIndex]() {
        onPlayButtonClicked(trackIndex, sceneIndex);
    };
    slot.onDoubleClick = [this, trackIndex, sceneIndex]() {
        openClipEditor(trackIndex, sceneIndex);
    };
    slot.onCreateMidiClip = [this, trackIndex, sceneIndex]() {
        onCreateMidiClipClicked(trackIndex, sceneIndex);
    };
    slot.onDeleteClip = [this, trackIndex, sceneIndex]() {
        TrackId tId = visibleTrackIds_[trackIndex];
        ClipId cId = ClipManager::getInstance().getClipInSlot(tId, sceneIndex);
        if (cId != INVALID_CLIP_ID) {
            UndoManager::getInstance().executeCommand(std::make_unique<DeleteClipCommand>(cId));
        }
    };
    slot.onCopyClip = [this, trackIndex, sceneIndex]() {
        TrackId tId = visibleTrackIds_[trackIndex];
        ClipId cId = ClipManager::getInstance().getClipInSlot(tId, sceneIndex);
        if (cId != INVALID_CLIP_ID) {
            ClipManager::getInstance().copyToClipboard({cId});
        }
    };
    slot.onCutClip = [this, trackIndex, sceneIndex]() {
        TrackId tId = visibleTrackIds_[trackIndex];
        ClipId cId = ClipManager::getInstance().getClipInSlot(tId, sceneIndex);
        if (cId != INVALID_CLIP_ID) {
            ClipManager::getInstance().copyToClipboard({cId});
            UndoManager::getInstance().executeCommand(std::make_unique<DeleteClipCommand>(cId));
        }
    };
    slot.onPasteClip = [this, trackIndex, sceneIndex]() {
        if (!ClipManager::getInstance().hasClipsInClipboard())
            return;
        TrackId tId = visibleTrackIds_[trackIndex];
        auto cmd = std::make_unique<PasteClipCommand>(0.0, tId, ClipView::Session, sceneIndex);
        UndoManager::getInstance().executeCommand(std::move(cmd));
    };
    slot.onDuplicateClip = [this, trackIndex, sceneIndex]() {
        TrackId tId = visibleTrackIds_[trackIndex];
        ClipId cId = ClipManager::getInstance().getClipInSlot(tId, sceneIndex);
        if (cId != INVALID_CLIP_ID) {
            int targetScene = sceneIndex + 1;
            if (targetScene >= numScenes_)
                addScene();
            if (ClipManager::getInstance().getClipInSlot(tId, targetScene) != INVALID_CLIP_ID)
                return;
            auto cmd = std::make_unique<DuplicateClipCommand>(cId);
            auto* cmdPtr = cmd.get();
            UndoManager::getInstance().executeCommand(std::move(cmd));
            ClipId newClipId = cmdPtr->getDuplicatedClipId();
            if (newClipId != INVALID_CLIP_ID) {
                ClipManager::getInstance().setClipSceneIndex(newClipId, targetScene);
            }
        }
    };
    slot.onAddScene = [this]() { addScene(); };
    slot.onRemoveScene = [this]() { removeScene(); };
}

void SessionView::onClipSlotClicked(int trackIndex, int sceneIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        return;
    }

    TrackId trackId = visibleTrackIds_[trackIndex];
    ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);

    if (clipId != INVALID_CLIP_ID) {
        // Select the clip (update inspector) - no playback change
        SelectionManager::getInstance().selectClip(clipId);
        ClipManager::getInstance().setSelectedClip(clipId);
    } else {
        // Empty slot - select the track
        selectTrack(trackId);
    }
}

void SessionView::onPlayButtonClicked(int trackIndex, int sceneIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        return;
    }

    TrackId trackId = visibleTrackIds_[trackIndex];
    ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);

    if (clipId != INVALID_CLIP_ID) {
        // Select the clip so the inspector shows it
        SelectionManager::getInstance().selectClip(clipId);

        // Check current play state — stop if playing/queued, play if stopped
        auto playState = audioEngine_ ? audioEngine_->getSessionClipPlayState(clipId)
                                      : SessionClipPlayState::Stopped;
        if (playState == SessionClipPlayState::Playing ||
            playState == SessionClipPlayState::Queued) {
            ClipManager::getInstance().stopClip(clipId);
        } else {
            ClipManager::getInstance().triggerClip(clipId);
        }
    }
}

void SessionView::onSceneLaunched(int sceneIndex) {
    auto& cm = ClipManager::getInstance();
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        TrackId trackId = visibleTrackIds_[i];
        ClipId clipId = cm.getClipInSlot(trackId, sceneIndex);
        if (clipId != INVALID_CLIP_ID) {
            cm.triggerClip(clipId);
        } else if (audioEngine_) {
            // Empty slot: stop the active clip on this track
            audioEngine_->stopSessionTrack(trackId);
        }
    }
}

void SessionView::onStopAllClicked() {
    ClipManager::getInstance().stopAllClips();
}

void SessionView::triggerGroupScene(TrackId groupId, int sceneIndex) {
    // Collect all descendant track IDs recursively
    std::vector<TrackId> descendants;
    std::function<void(TrackId)> collectDescendants = [&](TrackId tid) {
        const auto* t = TrackManager::getInstance().getTrack(tid);
        if (!t)
            return;
        if (t->isGroup()) {
            for (auto childId : t->childIds)
                collectDescendants(childId);
        } else {
            descendants.push_back(tid);
        }
    };
    collectDescendants(groupId);

    // Check if any descendant clip in this scene is playing — if so, stop all; else trigger all
    auto& cm = ClipManager::getInstance();
    bool anyPlaying = false;
    for (auto tid : descendants) {
        ClipId cid = cm.getClipInSlot(tid, sceneIndex);
        if (cid != INVALID_CLIP_ID && audioEngine_) {
            auto state = audioEngine_->getSessionClipPlayState(cid);
            if (state == SessionClipPlayState::Playing || state == SessionClipPlayState::Queued) {
                anyPlaying = true;
                break;
            }
        }
    }

    for (auto tid : descendants) {
        ClipId cid = cm.getClipInSlot(tid, sceneIndex);
        if (anyPlaying) {
            // Stop mode: stop all clips in this scene
            if (cid != INVALID_CLIP_ID)
                cm.stopClip(cid);
        } else {
            if (cid != INVALID_CLIP_ID) {
                cm.triggerClip(cid);
            } else if (audioEngine_) {
                // Empty slot: schedule a quantized stop for this track
                audioEngine_->stopSessionTrack(tid);
            }
        }
    }
}

void SessionView::openClipEditor(int trackIndex, int sceneIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        return;
    }

    TrackId trackId = visibleTrackIds_[trackIndex];
    ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);

    if (clipId == INVALID_CLIP_ID) {
        // Empty slot — create a new MIDI clip
        onCreateMidiClipClicked(trackIndex, sceneIndex);
        return;
    }

    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip) {
        // Select the clip so the bottom panel picks it up
        ClipManager::getInstance().setSelectedClip(clipId);

        auto& panelController = daw::ui::PanelController::getInstance();

        // Expand bottom panel if collapsed
        bool isCollapsed = panelController.getPanelState(daw::ui::PanelLocation::Bottom).collapsed;
        if (isCollapsed) {
            panelController.setCollapsed(daw::ui::PanelLocation::Bottom, false);
        }

        // For audio clips, explicitly switch to waveform editor.
        // For MIDI clips, BottomPanel's clipSelectionChanged handles the
        // PianoRoll vs DrumGrid choice, respecting the user's preference.
        if (clip->type == ClipType::Audio) {
            panelController.setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                               daw::ui::PanelContentType::WaveformEditor);
        }
    }
}

void SessionView::onCreateMidiClipClicked(int trackIndex, int sceneIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size()))
        return;
    if (sceneIndex < 0 || sceneIndex >= numScenes_)
        return;

    TrackId trackId = visibleTrackIds_[trackIndex];

    // Don't create if slot already has a clip
    if (ClipManager::getInstance().getClipInSlot(trackId, sceneIndex) != INVALID_CLIP_ID)
        return;

    // Create clip through command system for proper undo support
    auto cmd = std::make_unique<CreateClipCommand>(ClipType::MIDI, trackId, 0.0, 4.0, "",
                                                   ClipView::Session);

    // Get raw pointer before moving to UndoManager
    auto* cmdPtr = cmd.get();
    UndoManager::getInstance().executeCommand(std::move(cmd));

    // Get the created clip ID and set its scene index
    ClipId clipId = cmdPtr->getCreatedClipId();
    if (clipId != INVALID_CLIP_ID) {
        ClipManager::getInstance().setClipSceneIndex(clipId, sceneIndex);
        updateClipSlotAppearance(trackIndex, sceneIndex);
    }
}

void SessionView::trackSelectionChanged(TrackId trackId) {
    juce::ignoreUnused(trackId);
    updateHeaderSelectionVisuals();
}

// =============================================================================
// Track Header Drag-and-Drop (reorder / drop into group)
// =============================================================================

void SessionView::calculateHeaderDropTarget(int mouseX) {
    headerDropType_ = HeaderDropType::None;
    headerDropIndex_ = -1;
    int numTracks = static_cast<int>(visibleTrackIds_.size());
    for (int i = 0; i < numTracks; ++i) {
        if (i == headerDragIndex_)
            continue;
        int x = getTrackX(i);
        int w = trackColumnWidths_[i];
        if (mouseX >= x && mouseX < x + w + TRACK_SEPARATOR_WIDTH) {
            int quarter = w / 4;
            if (mouseX < x + quarter) {
                headerDropType_ = HeaderDropType::BetweenTracks;
                headerDropIndex_ = i;
            } else if (mouseX > x + w - quarter) {
                headerDropType_ = HeaderDropType::BetweenTracks;
                headerDropIndex_ = i + 1;
            } else if (canDropIntoGroup(headerDragIndex_, i)) {
                headerDropType_ = HeaderDropType::OntoGroup;
                headerDropIndex_ = i;
            }
            return;
        }
    }
    if (mouseX > getTotalTracksWidth()) {
        headerDropType_ = HeaderDropType::BetweenTracks;
        headerDropIndex_ = numTracks;
    }
}

bool SessionView::canDropIntoGroup(int draggedIndex, int targetIndex) const {
    if (draggedIndex < 0 || targetIndex < 0 ||
        draggedIndex >= static_cast<int>(visibleTrackIds_.size()) ||
        targetIndex >= static_cast<int>(visibleTrackIds_.size()))
        return false;
    if (draggedIndex == targetIndex)
        return false;
    auto& tm = TrackManager::getInstance();
    const auto* target = tm.getTrack(visibleTrackIds_[targetIndex]);
    if (!target || !target->isGroup())
        return false;
    const auto* dragged = tm.getTrack(visibleTrackIds_[draggedIndex]);
    if (dragged && dragged->isGroup()) {
        auto desc = tm.getAllDescendants(dragged->id);
        if (std::find(desc.begin(), desc.end(), target->id) != desc.end())
            return false;
    }
    return true;
}

void SessionView::executeHeaderDrop() {
    if (headerDragIndex_ < 0 || headerDropType_ == HeaderDropType::None)
        return;
    auto& tm = TrackManager::getInstance();
    TrackId draggedId = visibleTrackIds_[headerDragIndex_];
    if (headerDropType_ == HeaderDropType::OntoGroup && headerDropIndex_ >= 0) {
        TrackId groupId = visibleTrackIds_[headerDropIndex_];
        tm.addTrackToGroup(draggedId, groupId);
    } else if (headerDropType_ == HeaderDropType::BetweenTracks && headerDropIndex_ >= 0) {
        TrackId targetParent = INVALID_TRACK_ID;
        if (headerDropIndex_ < static_cast<int>(visibleTrackIds_.size())) {
            const auto* t = tm.getTrack(visibleTrackIds_[headerDropIndex_]);
            if (t)
                targetParent = t->parentId;
        } else if (!visibleTrackIds_.empty()) {
            const auto* t = tm.getTrack(visibleTrackIds_.back());
            if (t)
                targetParent = t->parentId;
        }
        const auto* dragged = tm.getTrack(draggedId);
        if (dragged && dragged->parentId != targetParent) {
            tm.removeTrackFromGroup(draggedId);
            if (targetParent != INVALID_TRACK_ID)
                tm.addTrackToGroup(draggedId, targetParent);
        }
        int targetIdx = headerDropIndex_ < static_cast<int>(visibleTrackIds_.size())
                            ? tm.getTrackIndex(visibleTrackIds_[headerDropIndex_])
                            : tm.getNumTracks();
        int currentIdx = tm.getTrackIndex(draggedId);
        if (currentIdx < targetIdx)
            targetIdx--;
        tm.moveTrack(draggedId, targetIdx);
    }
}

void SessionView::resetHeaderDragState() {
    headerIsDragging_ = false;
    headerDragIndex_ = -1;
    headerDropType_ = HeaderDropType::None;
    headerDropIndex_ = -1;
    headerContainer->repaint();
}

void SessionView::paintHeaderDragFeedback(juce::Graphics& g) {
    if (!headerIsDragging_ || headerDragIndex_ < 0)
        return;
    // Highlight dragged header
    int dx = getTrackX(headerDragIndex_) - trackHeaderScrollOffset;
    int dw = trackColumnWidths_[headerDragIndex_];
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
    g.fillRect(dx, 0, dw, headerContainer->getHeight());

    if (headerDropType_ == HeaderDropType::BetweenTracks && headerDropIndex_ >= 0) {
        int lineX;
        if (headerDropIndex_ >= static_cast<int>(visibleTrackIds_.size()))
            lineX = getTotalTracksWidth() - trackHeaderScrollOffset;
        else
            lineX = getTrackX(headerDropIndex_) - trackHeaderScrollOffset;
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        g.fillRect(lineX - 2, 0, 4, headerContainer->getHeight());
    } else if (headerDropType_ == HeaderDropType::OntoGroup && headerDropIndex_ >= 0) {
        int gx = getTrackX(headerDropIndex_) - trackHeaderScrollOffset;
        int gw = trackColumnWidths_[headerDropIndex_];
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.drawRect(gx, 0, gw, headerContainer->getHeight(), 3);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.15f));
        g.fillRect(gx, 0, gw, headerContainer->getHeight());
    }
}

void SessionView::selectTrack(TrackId trackId) {
    SelectionManager::getInstance().selectTrack(trackId);
}

void SessionView::updateHeaderSelectionVisuals() {
    auto selectedId = TrackManager::getInstance().getSelectedTrack();

    for (size_t i = 0; i < visibleTrackIds_.size() && i < trackHeaders.size(); ++i) {
        bool isSelected = visibleTrackIds_[i] == selectedId;
        auto* header = trackHeaders[i].get();

        // Get track info for proper coloring
        const auto* track = TrackManager::getInstance().getTrack(visibleTrackIds_[i]);
        if (!track)
            continue;

        if (isSelected) {
            // Selected: white text on black background
            header->setColour(juce::TextButton::buttonColourId, juce::Colours::black);
            header->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        } else {
            // Unselected: track colour background
            header->setColour(juce::TextButton::buttonColourId, track->colour.withAlpha(0.5f));
            header->setColour(juce::TextButton::textColourOffId,
                              DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        }
    }
    // Master label selection
    if (masterLabel_) {
        bool masterSelected = selectedId == MASTER_TRACK_ID;
        if (masterSelected) {
            masterLabel_->setColour(juce::TextButton::buttonColourId, juce::Colours::black);
            masterLabel_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        } else {
            masterLabel_->setColour(juce::TextButton::buttonColourId,
                                    DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
            masterLabel_->setColour(juce::TextButton::textColourOffId,
                                    DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        }
    }

    repaint();
}

void SessionView::showMixerContextMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, "Show I/O", true, ioRowVisible_);
    menu.addItem(2, "Show Sends", true, sendRowVisible_);
    menu.addItem(3, "Show Record/Monitor", true, recordMonitorVisible_);

    auto safeThis = juce::Component::SafePointer<SessionView>(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result) {
        if (!safeThis)
            return;
        if (result == 1) {
            safeThis->ioRowVisible_ = !safeThis->ioRowVisible_;
            safeThis->resized();
        } else if (result == 2) {
            safeThis->sendRowVisible_ = !safeThis->sendRowVisible_;
            safeThis->resized();
        } else if (result == 3) {
            safeThis->recordMonitorVisible_ = !safeThis->recordMonitorVisible_;
            for (auto& strip : safeThis->trackMiniStrips_)
                strip->setShowRecordMonitor(safeThis->recordMonitorVisible_);
        }
    });
}

// ============================================================================
// ClipManagerListener
// ============================================================================

void SessionView::clipsChanged() {
    // Clear any stale drag overlay state — structural changes (add/remove clip)
    // can interrupt drag operations without proper exit callbacks.
    showPluginDropOverlay_ = false;
    pluginDropTrackIndex_ = -1;
    clearDragHighlight();

    updateAllClipSlots();
}

void SessionView::clipPropertyChanged(ClipId clipId) {
    // Find clip and update its slot
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip || clip->sceneIndex < 0)
        return;

    // Find track index
    int trackIndex = -1;
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        if (visibleTrackIds_[i] == clip->trackId) {
            trackIndex = static_cast<int>(i);
            break;
        }
    }

    if (trackIndex >= 0) {
        updateClipSlotAppearance(trackIndex, clip->sceneIndex);
    }

    // Also update parent group slot
    const auto* track = TrackManager::getInstance().getTrack(clip->trackId);
    if (track && track->hasParent()) {
        for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
            if (visibleTrackIds_[i] == track->parentId) {
                updateClipSlotAppearance(static_cast<int>(i), clip->sceneIndex);
                break;
            }
        }
    }
}

void SessionView::clipSelectionChanged(ClipId /*clipId*/) {
    // Refresh all slots to update selection highlight
    updateAllClipSlots();
}

void SessionView::clipPlaybackStateChanged(ClipId clipId) {
    // Update slot appearance when playback state changes
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip || clip->sceneIndex < 0) {
        DBG("SessionView::clipPlaybackStateChanged: clip " << clipId << " not found or no scene");
        return;
    }

    auto playState = audioEngine_ ? audioEngine_->getSessionClipPlayState(clipId)
                                  : SessionClipPlayState::Stopped;
    DBG("SessionView::clipPlaybackStateChanged: clip "
        << clipId << " playState=" << (int)playState
        << " sessionPlayheadPos=" << clip->sessionPlayheadPos);

    // Find track index
    int trackIndex = -1;
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        if (visibleTrackIds_[i] == clip->trackId) {
            trackIndex = static_cast<int>(i);
            break;
        }
    }

    if (trackIndex >= 0) {
        updateClipSlotAppearance(trackIndex, clip->sceneIndex);
    }

    // Also update parent group slot if this track has a parent
    const auto* track = TrackManager::getInstance().getTrack(clip->trackId);
    if (track && track->hasParent()) {
        for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
            if (visibleTrackIds_[i] == track->parentId) {
                updateClipSlotAppearance(static_cast<int>(i), clip->sceneIndex);
                break;
            }
        }
    }
}

void SessionView::updateClipSlotAppearance(int trackIndex, int sceneIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(clipSlots.size()))
        return;
    if (trackIndex >= static_cast<int>(visibleTrackIds_.size()))
        return;
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(clipSlots[trackIndex].size()))
        return;

    auto* slot = static_cast<ClipSlotButton*>(clipSlots[trackIndex][sceneIndex].get());
    if (!slot)
        return;

    TrackId trackId = visibleTrackIds_[trackIndex];

    // Always set slot identity for drag-and-drop
    slot->trackId = trackId;
    slot->sceneIndex = sceneIndex;

    // Group slot: check if any descendant has a clip in this scene
    if (slot->isGroupSlot) {
        bool anyClips = false;
        bool anyPlaying = false;

        std::function<void(TrackId)> checkDescendants = [&](TrackId tid) {
            const auto* t = TrackManager::getInstance().getTrack(tid);
            if (!t)
                return;
            if (t->isGroup()) {
                for (auto childId : t->childIds)
                    checkDescendants(childId);
            } else {
                ClipId cid = ClipManager::getInstance().getClipInSlot(tid, sceneIndex);
                if (cid != INVALID_CLIP_ID) {
                    anyClips = true;
                    if (audioEngine_) {
                        auto state = audioEngine_->getSessionClipPlayState(cid);
                        if (state == SessionClipPlayState::Playing ||
                            state == SessionClipPlayState::Queued)
                            anyPlaying = true;
                    }
                }
            }
        };
        checkDescendants(trackId);

        slot->hasChildClips = anyClips;
        slot->childClipIsPlaying = anyPlaying;
        slot->hasClip = false;
        slot->setButtonText("");
        slot->setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        slot->repaint();
        return;
    }

    ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);
    ClipId selectedClipId = ClipManager::getInstance().getSelectedClip();

    if (clipId != INVALID_CLIP_ID) {
        const auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip) {
            // Query play state from the scheduler (single source of truth)
            auto playState = audioEngine_ ? audioEngine_->getSessionClipPlayState(clipId)
                                          : SessionClipPlayState::Stopped;

            // Update slot state for custom painting
            slot->hasClip = true;
            slot->clipId = clipId;
            slot->clipIsPlaying = (playState == SessionClipPlayState::Playing);
            slot->clipIsQueued = (playState == SessionClipPlayState::Queued);
            slot->isSelected = (clipId == selectedClipId);
            slot->clipLength = clip->length;
            {
                auto posIt = clipPlayheadPositions_.find(clipId);
                slot->sessionPlayheadPos =
                    (slot->clipIsPlaying && posIt != clipPlayheadPositions_.end()) ? posIt->second
                                                                                   : -1.0;
            }

            slot->setButtonText(clip->name);

            // Clip always shows its own colour; play state is shown via the play/stop icon
            slot->setColour(juce::TextButton::buttonColourId, clip->colour.withAlpha(0.7f));
            slot->setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        }
    } else {
        // Empty slot
        slot->hasClip = false;
        slot->clipId = INVALID_CLIP_ID;
        slot->clipIsPlaying = false;
        slot->isSelected = false;
        slot->clipLength = 0.0;
        slot->sessionPlayheadPos = -1.0;
        slot->setButtonText("");
        slot->setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        slot->setColour(juce::TextButton::textColourOffId,
                        DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    }

    slot->repaint();
}

void SessionView::updateAllClipSlots() {
    int numTracks = static_cast<int>(visibleTrackIds_.size());
    for (int trackIndex = 0; trackIndex < numTracks; ++trackIndex) {
        int numSlots = static_cast<int>(clipSlots[trackIndex].size());
        for (int sceneIndex = 0; sceneIndex < numSlots; ++sceneIndex) {
            updateClipSlotAppearance(trackIndex, sceneIndex);
        }
    }
}

// ============================================================================
// Session Playhead
// ============================================================================

void SessionView::setSessionPlayheadPositions(const std::unordered_map<ClipId, double>& positions) {
    clipPlayheadPositions_ = positions;

    // Update playhead positions and reset slots that stopped playing
    for (auto& trackSlots : clipSlots) {
        for (auto& slotBtn : trackSlots) {
            auto* slot = dynamic_cast<ClipSlotButton*>(slotBtn.get());
            if (!slot || !slot->hasClip)
                continue;

            double prev = slot->sessionPlayheadPos;
            if (slot->clipIsPlaying) {
                auto it = positions.find(slot->clipId);
                slot->sessionPlayheadPos = (it != positions.end()) ? it->second : -1.0;
            } else {
                slot->sessionPlayheadPos = -1.0;
            }

            if (slot->sessionPlayheadPos != prev)
                slot->repaint();
        }
    }
}

// ============================================================================
// Audio Engine & Metering
// ============================================================================

void SessionView::setAudioEngine(AudioEngine* engine) {
    audioEngine_ = engine;
    if (audioEngine_) {
        startTimerHz(30);  // 30Hz meter refresh
    } else {
        stopTimer();
    }
}

void SessionView::timerCallback() {
    if (!audioEngine_)
        return;

    auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(audioEngine_);
    if (!teWrapper)
        return;

    auto* bridge = teWrapper->getAudioBridge();
    if (!bridge)
        return;

    auto& meteringBuffer = bridge->getMeteringBuffer();

    // Update track strip meters (peek, don't consume — MixerView also reads these)
    for (auto& strip : trackMiniStrips_) {
        int trackId = strip->getTrackId();
        MeterData data;
        if (meteringBuffer.peekLatest(trackId, data)) {
            strip->setMeterLevels(data.peakL, data.peakR);
        }
    }

    // Update blink state for queued clips — blink on the beat
    {
        bool newBlinkOn = false;
        if (auto* edit = teWrapper->getEdit()) {
            auto& transport = edit->getTransport();
            if (transport.isPlaying()) {
                double bpm = edit->tempoSequence.getBpmAt(tracktion::TimePosition());
                double pos = transport.getPosition().inSeconds();
                double beatDuration = 60.0 / (bpm > 0.0 ? bpm : 120.0);
                double beatPhase = std::fmod(pos, beatDuration) / beatDuration;
                newBlinkOn = (beatPhase < 0.5);
            }
        }
        for (auto& trackSlots : clipSlots) {
            for (auto& slotBtn : trackSlots) {
                auto* slot = dynamic_cast<ClipSlotButton*>(slotBtn.get());
                if (slot && slot->clipIsQueued) {
                    slot->blinkOn = newBlinkOn;
                    slot->repaint();
                }
            }
        }
    }

    // Update master strip meters
    if (masterStrip_) {
        float masterPeakL = bridge->getMasterPeakL();
        float masterPeakR = bridge->getMasterPeakR();
        masterStrip_->setMeterLevels(masterPeakL, masterPeakR);
    }
}

// ============================================================================
// File Drag & Drop
// ============================================================================

bool SessionView::isInterestedInFileDrag(const juce::StringArray& files) {
    // Accept if at least one file is an audio file
    for (const auto& file : files) {
        if (isAudioFile(file)) {
            return true;
        }
    }
    return false;
}

void SessionView::fileDragEnter(const juce::StringArray& files, int x, int y) {
    updateDragHighlight(x, y);

    // Show ghost preview if hovering over valid slot or new-track area
    if (dragHoverSceneIndex_ >= 0) {
        updateDragGhost(files, dragHoverTrackIndex_, dragHoverSceneIndex_);
    }
}

void SessionView::fileDragMove(const juce::StringArray& files, int x, int y) {
    int oldTrackIndex = dragHoverTrackIndex_;
    int oldSceneIndex = dragHoverSceneIndex_;

    updateDragHighlight(x, y);

    // Update ghost if slot changed
    if (dragHoverTrackIndex_ != oldTrackIndex || dragHoverSceneIndex_ != oldSceneIndex) {
        if (dragHoverSceneIndex_ >= 0) {
            updateDragGhost(files, dragHoverTrackIndex_, dragHoverSceneIndex_);
        } else {
            clearDragGhost();
        }
    }
}

void SessionView::fileDragExit(const juce::StringArray& /*files*/) {
    clearDragHighlight();
    clearDragGhost();
}

void SessionView::filesDropped(const juce::StringArray& files, int x, int y) {
    clearDragHighlight();
    clearDragGhost();

    // Convert screen coordinates to grid viewport coordinates
    auto gridLocalPoint = gridViewport->getLocalPoint(this, juce::Point<int>(x, y));

    // Add viewport scroll offset
    gridLocalPoint +=
        juce::Point<int>(gridViewport->getViewPositionX(), gridViewport->getViewPositionY());

    // Calculate which slot was dropped on
    int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;

    int trackIndex = getTrackIndexAtX(gridLocalPoint.getX());
    int sceneIndex = gridLocalPoint.getY() / sceneRowHeight;

    // Validate scene index
    if (sceneIndex < 0 || sceneIndex >= numScenes_)
        return;

    TrackId targetTrackId = INVALID_TRACK_ID;

    if (trackIndex >= 0 && trackIndex < static_cast<int>(visibleTrackIds_.size())) {
        targetTrackId = visibleTrackIds_[trackIndex];
    } else {
        // Dropped past last track — create a new audio track
        // Derive name from first audio file
        juce::String trackName = "Audio";
        for (const auto& f : files) {
            if (isAudioFile(f)) {
                trackName = juce::File(f).getFileNameWithoutExtension();
                break;
            }
        }
        auto cmd = std::make_unique<CreateTrackCommand>(TrackType::Audio, trackName);
        auto* cmdPtr = cmd.get();
        UndoManager::getInstance().executeCommand(std::move(cmd));
        targetTrackId = cmdPtr->getCreatedTrackId();
        if (targetTrackId == INVALID_TRACK_ID)
            return;
    }

    // Create clips for each audio file dropped
    auto& clipManager = ClipManager::getInstance();
    int currentSceneIndex = sceneIndex;

    // Create format manager once for all dropped files
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    for (const auto& filePath : files) {
        if (!isAudioFile(filePath))
            continue;

        // Skip to next empty slot
        while (currentSceneIndex < numScenes_ &&
               clipManager.getClipInSlot(targetTrackId, currentSceneIndex) != INVALID_CLIP_ID) {
            currentSceneIndex++;
        }

        // Don't exceed scene bounds
        if (currentSceneIndex >= numScenes_)
            break;

        // Get audio file duration
        juce::File audioFile(filePath);
        double fileDuration = 4.0;  // fallback
        {
            std::unique_ptr<juce::AudioFormatReader> reader(
                formatManager.createReaderFor(audioFile));
            if (reader) {
                fileDuration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
            }
        }

        // Create audio clip for session view (not arrangement)
        double bpm = 120.0;
        if (timelineController_) {
            bpm = timelineController_->getState().tempo.bpm;
        }
        ClipId newClipId = clipManager.createAudioClip(targetTrackId, 0.0, fileDuration, filePath,
                                                       ClipView::Session, bpm);
        if (newClipId != INVALID_CLIP_ID) {
            UndoManager::getInstance().executeCommand(std::make_unique<SetClipNameCommand>(
                newClipId, audioFile.getFileNameWithoutExtension()));

            // Session clips default to looping, with source end matching clip duration
            clipManager.setClipLoopEnabled(newClipId, true, bpm);
            // Set loopLength to match file duration (source region = entire file)
            clipManager.setLoopLength(newClipId, fileDuration);

            // Assign to session view slot (triggers proper notification)
            clipManager.setClipSceneIndex(newClipId, currentSceneIndex);
        }

        currentSceneIndex++;  // Move to next scene for multi-file drop
    }
}

void SessionView::updateDragHighlight(int x, int y) {
    // Convert to grid coordinates
    auto gridLocalPoint = gridViewport->getLocalPoint(this, juce::Point<int>(x, y));
    gridLocalPoint +=
        juce::Point<int>(gridViewport->getViewPositionX(), gridViewport->getViewPositionY());

    int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;

    int trackIndex = getTrackIndexAtX(gridLocalPoint.getX());
    int sceneIndex = gridLocalPoint.getY() / sceneRowHeight;

    // Validate scene index
    if (sceneIndex < 0 || sceneIndex >= numScenes_) {
        sceneIndex = -1;
    }

    // trackIndex == -1 is valid: means "past last track" (create new track zone)
    // Only clamp to -1 if truly out of bounds on the left
    if (trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        trackIndex = -1;
    }

    // Update highlight if slot changed
    if (trackIndex != dragHoverTrackIndex_ || sceneIndex != dragHoverSceneIndex_) {
        // Clear old highlight on previous slot
        if (dragHoverTrackIndex_ >= 0 && dragHoverSceneIndex_ >= 0) {
            updateClipSlotAppearance(dragHoverTrackIndex_, dragHoverSceneIndex_);
        }

        // Set new highlight
        dragHoverTrackIndex_ = trackIndex;
        dragHoverSceneIndex_ = sceneIndex;

        if (dragHoverTrackIndex_ >= 0 && dragHoverSceneIndex_ >= 0 &&
            dragHoverTrackIndex_ < static_cast<int>(clipSlots.size()) &&
            dragHoverSceneIndex_ < static_cast<int>(clipSlots[dragHoverTrackIndex_].size())) {
            auto* slot = clipSlots[dragHoverTrackIndex_][dragHoverSceneIndex_].get();
            if (slot) {
                // Highlight with accent color
                slot->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.5f));
            }
        }

        // Repaint to update the "new track" overlay when hovering past last track
        if (dragHoverTrackIndex_ == -1)
            repaint();
    }
}

void SessionView::clearDragHighlight() {
    if (dragHoverTrackIndex_ >= 0 && dragHoverSceneIndex_ >= 0) {
        updateClipSlotAppearance(dragHoverTrackIndex_, dragHoverSceneIndex_);
    }
    dragHoverTrackIndex_ = -1;
    dragHoverSceneIndex_ = -1;

    // Always repaint — the "new track" overlay may have been painted in a
    // previous frame even if the current drag state doesn't show it (the mouse
    // can move from past-last-track to on-a-track between paint frames).
    repaint();
    if (gridViewport)
        gridViewport->repaint();
}

void SessionView::updateDragGhost(const juce::StringArray& files, int trackIndex, int sceneIndex) {
    if (files.isEmpty() || sceneIndex < 0) {
        clearDragGhost();
        return;
    }

    // Get first audio file from the list
    juce::String firstAudioFile;
    for (const auto& file : files) {
        if (isAudioFile(file)) {
            firstAudioFile = file;
            break;
        }
    }

    if (firstAudioFile.isEmpty()) {
        clearDragGhost();
        return;
    }

    // Extract filename without extension
    juce::File audioFile(firstAudioFile);
    juce::String filename = audioFile.getFileNameWithoutExtension();

    // Add count indicator if multiple files
    int audioFileCount = 0;
    for (const auto& file : files) {
        if (isAudioFile(file))
            audioFileCount++;
    }

    if (audioFileCount > 1) {
        filename += juce::String(" (+") + juce::String(audioFileCount - 1) + ")";
    }

    // Position ghost at the target slot (in grid coordinates)
    int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;

    int ghostX, ghostW;
    if (trackIndex >= 0) {
        ghostX = getTrackX(trackIndex);
        ghostW = (trackIndex < static_cast<int>(trackColumnWidths_.size()))
                     ? trackColumnWidths_[trackIndex]
                     : DEFAULT_CLIP_SLOT_WIDTH;
    } else {
        // Past last track — position ghost in "new track" column
        ghostX = getTotalTracksWidth();
        ghostW = DEFAULT_CLIP_SLOT_WIDTH;
    }
    int ghostY = sceneIndex * sceneRowHeight;

    // Update ghost label
    dragGhostLabel_->setText(filename, juce::dontSendNotification);
    dragGhostLabel_->setBounds(ghostX, ghostY, ghostW, CLIP_SLOT_HEIGHT);
    dragGhostLabel_->setVisible(true);
    dragGhostLabel_->toFront(false);
}

void SessionView::clearDragGhost() {
    if (dragGhostLabel_) {
        dragGhostLabel_->setVisible(false);
    }
}

bool SessionView::isAudioFile(const juce::String& filename) const {
    static const juce::StringArray audioExtensions = {".wav",  ".aiff", ".aif", ".mp3", ".ogg",
                                                      ".flac", ".m4a",  ".wma", ".opus"};

    for (const auto& ext : audioExtensions) {
        if (filename.endsWithIgnoreCase(ext)) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// DragAndDropTarget implementation (internal JUCE drags: plugins, clip slots)
// ============================================================================

bool SessionView::isInterestedInDragSource(const SourceDetails& details) {
    if (auto* obj = details.description.getDynamicObject()) {
        auto type = obj->getProperty("type").toString();
        return type == "plugin" || type == "sessionClip";
    }
    return false;
}

void SessionView::itemDragEnter(const SourceDetails& details) {
    auto* obj = details.description.getDynamicObject();
    if (!obj)
        return;

    auto type = obj->getProperty("type").toString();
    if (type == "plugin") {
        showPluginDropOverlay_ = true;
        // Determine which track column is being hovered
        auto gridLocalPoint = gridViewport->getLocalPoint(this, details.localPosition);
        int hitX = gridLocalPoint.getX() + gridViewport->getViewPositionX();
        pluginDropTrackIndex_ = getTrackIndexAtX(hitX);
        repaint();
    } else if (type == "sessionClip") {
        // Highlight target slot
        updateDragHighlight(details.localPosition.getX(), details.localPosition.getY());
    }
}

void SessionView::itemDragMove(const SourceDetails& details) {
    auto* obj = details.description.getDynamicObject();
    if (!obj)
        return;

    auto type = obj->getProperty("type").toString();
    if (type == "plugin") {
        auto gridLocalPoint = gridViewport->getLocalPoint(this, details.localPosition);
        int hitX = gridLocalPoint.getX() + gridViewport->getViewPositionX();
        int oldIndex = pluginDropTrackIndex_;
        pluginDropTrackIndex_ = getTrackIndexAtX(hitX);
        if (pluginDropTrackIndex_ != oldIndex)
            repaint();
    } else if (type == "sessionClip") {
        updateDragHighlight(details.localPosition.getX(), details.localPosition.getY());
    }
}

void SessionView::itemDragExit(const SourceDetails& /*details*/) {
    showPluginDropOverlay_ = false;
    pluginDropTrackIndex_ = -1;
    clearDragHighlight();
    repaint();
}

void SessionView::itemDropped(const SourceDetails& details) {
    showPluginDropOverlay_ = false;
    pluginDropTrackIndex_ = -1;
    clearDragHighlight();
    repaint();

    auto* obj = details.description.getDynamicObject();
    if (!obj)
        return;

    auto type = obj->getProperty("type").toString();

    if (type == "plugin") {
        auto device = TrackManager::deviceInfoFromPluginObject(*obj);

        // Determine target track from drop position
        auto gridLocalPoint = gridViewport->getLocalPoint(this, details.localPosition);
        int hitX = gridLocalPoint.getX() + gridViewport->getViewPositionX();
        int trackIndex = getTrackIndexAtX(hitX);

        if (trackIndex >= 0 && trackIndex < static_cast<int>(visibleTrackIds_.size())) {
            // Drop on existing track — add plugin to chain
            TrackId trackId = visibleTrackIds_[trackIndex];
            TrackManager::getInstance().addDeviceToTrack(trackId, device);
        } else {
            // Drop past last track — create new track with plugin
            TrackType trackType = TrackType::Audio;
            juce::String pluginName = obj->getProperty("name").toString();
            auto cmd =
                std::make_unique<CreateTrackWithDeviceCommand>(pluginName, trackType, device);
            UndoManager::getInstance().executeCommand(std::move(cmd));
        }
    } else if (type == "sessionClip") {
        // Clip slot drag — handle in Phase 3
        ClipId clipId = static_cast<ClipId>(static_cast<int>(obj->getProperty("clipId")));
        TrackId sourceTrackId = static_cast<TrackId>(static_cast<int>(obj->getProperty("trackId")));
        int sourceSceneIndex = static_cast<int>(obj->getProperty("sceneIndex"));

        // Calculate target from drop coordinates
        auto gridLocalPoint = gridViewport->getLocalPoint(this, details.localPosition);
        gridLocalPoint +=
            juce::Point<int>(gridViewport->getViewPositionX(), gridViewport->getViewPositionY());

        int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;
        int targetTrackIndex = getTrackIndexAtX(gridLocalPoint.getX());
        int targetSceneIndex = gridLocalPoint.getY() / sceneRowHeight;

        // Validate
        if (targetTrackIndex < 0 || targetTrackIndex >= static_cast<int>(visibleTrackIds_.size()))
            return;
        if (targetSceneIndex < 0 || targetSceneIndex >= numScenes_)
            return;

        TrackId targetTrackId = visibleTrackIds_[targetTrackIndex];

        // Skip if dropping on same slot
        if (targetTrackId == sourceTrackId && targetSceneIndex == sourceSceneIndex)
            return;

        // Check if target slot is occupied
        auto& clipManager = ClipManager::getInstance();
        if (clipManager.getClipInSlot(targetTrackId, targetSceneIndex) != INVALID_CLIP_ID)
            return;  // Target occupied, reject

        bool isAltHeld = juce::ModifierKeys::getCurrentModifiers().isAltDown();
        if (isAltHeld) {
            // Alt+drag = duplicate clip to target slot
            auto cmd = std::make_unique<DuplicateClipCommand>(clipId);
            auto* cmdPtr = cmd.get();
            UndoManager::getInstance().executeCommand(std::move(cmd));
            ClipId newClipId = cmdPtr->getDuplicatedClipId();
            if (newClipId != INVALID_CLIP_ID) {
                clipManager.moveClipToTrack(newClipId, targetTrackId);
                clipManager.setClipSceneIndex(newClipId, targetSceneIndex);
            }
        } else {
            // Regular drag = move clip to target slot
            auto cmd =
                std::make_unique<MoveSessionClipCommand>(clipId, targetTrackId, targetSceneIndex);
            UndoManager::getInstance().executeCommand(std::move(cmd));
        }
    }
}

}  // namespace magda
