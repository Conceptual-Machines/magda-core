#include "ExternalInsertUI.hpp"

#include <juce_audio_devices/juce_audio_devices.h>

#include <vector>

#include "BinaryData.h"
#include "audio/controllers/ControllerRouter.hpp"
#include "audio/io/AudioIOControl.hpp"
#include "audio/plugins/InternalPluginRegistry.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "engine/AudioEngine.hpp"
#include "themes/ActiveTheme.hpp"
#include "themes/FontManager.hpp"
#include "themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {

using Endpoint = magda::InsertConfig::Endpoint;
using Geometry = ExternalInsertGeometry;

/// Write @p edit into the insert at @p path, as one undoable step; the engines rebuild it (#2279).
template <typename Edit> void editInsert(const magda::ChainNodePath& path, Edit&& edit) {
    if (!path.isValid())
        return;
    magda::UndoManager::getInstance().executeCommand(std::make_unique<magda::EditInsertCommand>(
        path, "Edit External Insert",
        [edit = std::forward<Edit>(edit)](magda::DeviceInfo& device) { edit(device.insert); }));
}

// The picker id whose mapped name matches the device's current port, else 0 ("None").
int idForName(const std::map<int, juce::String>& names, const juce::String& current) {
    for (const auto& [id, name] : names)
        if (name == current)
            return id;
    return 0;
}

/// The hardware channels an engine has open, by the names saved routes use, in channel order.
juce::StringArray routeNames(bool inputs) {
    juce::StringArray names;
    auto* engine = magda::TrackManager::getInstance().getAudioEngine();
    auto* io = engine != nullptr ? engine->getAudioIO() : nullptr;
    if (io == nullptr)
        return names;

    for (const auto& [channel, name] : inputs ? io->inputs().routeNames : io->outputs().routeNames)
        names.addIfNotAlreadyThere(name);
    return names;
}

juce::StringArray midiOutputNames() {
    juce::StringArray names;
    for (const auto& device : juce::MidiOutput::getAvailableDevices())
        names.addIfNotAlreadyThere(device.name);
    return names;
}

std::vector<magda::RoutingSelector::RoutingOption> buildOptions(
    const juce::StringArray& devices, std::map<int, juce::String>& names) {
    std::vector<magda::RoutingSelector::RoutingOption> options;
    options.push_back({0, "None", false});
    names.clear();

    if (!devices.isEmpty())
        options.push_back({-1, "", true});

    for (int i = 0; i < devices.size(); ++i) {
        const int id = i + 1;
        options.push_back({id, devices[i], false});
        names[id] = devices[i];
    }
    return options;
}

std::vector<magda::RoutingSelector::RoutingOption> numberedOptions(const juce::String& prefix,
                                                                   int first, int last) {
    std::vector<magda::RoutingSelector::RoutingOption> options;
    for (int value = first; value <= last; ++value)
        options.push_back({value + 1, prefix + juce::String(value), false});
    return options;
}

// Feedback-port guard (#1623): two inserts driving the same hardware send, or
// pulling the same return, silently double signals / cross-feed. Returns a
// warning string when another enabled external insert shares one of this
// insert's ports, empty otherwise.
juce::String findPortConflict(const magda::ChainNodePath& myPath, const magda::InsertConfig& mine) {
    const auto& mySend = mine.sendDevice;
    const auto& myReturn = mine.returnDevice;
    if (mySend.isEmpty() && myReturn.isEmpty())
        return {};
    if (mySend.isNotEmpty() && mySend == myReturn)
        return "Return uses the same port as the send";

    for (const auto& track : magda::TrackManager::getInstance().getTracks()) {
        for (const auto& element : track.chain.fxChainElements) {
            if (!magda::isDevice(element))
                continue;
            const auto& device = magda::getDevice(element);
            if (device.bypassed ||
                !magda::daw::audio::internalPluginHasTag(device.pluginId, "external-insert"))
                continue;
            if (magda::ChainNodePath::topLevelDevice(track.id, device.id) == myPath)
                continue;
            if (mySend.isNotEmpty() && device.insert.sendDevice == mySend)
                return "Send port also used on " + track.name;
            if (myReturn.isNotEmpty() && device.insert.returnDevice == myReturn)
                return "Return port also used on " + track.name;
        }
    }
    return {};
}

void styleCaption(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(12.0f));
    label.setColour(juce::Label::textColourId, ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
    label.setJustificationType(juce::Justification::centredLeft);
}

void styleButton(juce::TextButton& button, const juce::String& text) {
    button.setButtonText(text);
    button.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    button.setColour(juce::ComboBox::outlineColourId,
                     ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    button.setColour(juce::TextButton::buttonColourId,
                     ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
    button.setColour(juce::TextButton::buttonOnColourId,
                     ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
    button.setColour(juce::TextButton::textColourOffId,
                     ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    button.setColour(juce::TextButton::textColourOnId,
                     ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
}

}  // namespace

ExternalInsertUI::ExternalInsertUI()
    : sendSelector_(magda::RoutingSelector::Type::AudioOut),
      channelSelector_(magda::RoutingSelector::Type::MidiOut),
      returnSelector_(magda::RoutingSelector::Type::AudioIn),
      ccPortSelector_(magda::RoutingSelector::Type::MidiOut),
      ccChannelSelector_(magda::RoutingSelector::Type::MidiOut),
      knobCcSelector_(magda::RoutingSelector::Type::MidiOut) {
    styleCaption(sendLabel_, "Send to");
    styleCaption(returnLabel_, "Return from");
    styleCaption(latencyLabel_, "Latency");
    styleCaption(ccLabel_, "MIDI CC");
    ccLabel_.setColour(juce::Label::textColourId, ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    ccTarget_.setFont(FontManager::getInstance().getUIFont(11.0f));
    ccTarget_.setColour(juce::Label::textColourId, ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    for (auto* label : {&sendLabel_, &returnLabel_, &latencyLabel_, &ccLabel_, &ccTarget_})
        addAndMakeVisible(*label);

    for (auto* selector : {&sendSelector_, &channelSelector_, &returnSelector_, &ccPortSelector_,
                           &ccChannelSelector_, &knobCcSelector_}) {
        selector->setFieldStyle(true);
        addAndMakeVisible(*selector);
    }
    channelSelector_.setOptions(numberedOptions("Ch ", 1, 16));
    ccChannelSelector_.setOptions(numberedOptions("Ch ", 1, 16));
    knobCcSelector_.setOptions(numberedOptions("CC ", 0, 127));

    channelSelector_.onSelectionChanged = [this](int id) {
        editInsert(path_, [id](magda::InsertConfig& insert) { insert.midiChannel = id - 1; });
    };
    ccChannelSelector_.onSelectionChanged = channelSelector_.onSelectionChanged;
    ccPortSelector_.onSelectionChanged = [this](int id) {
        const auto name = id <= 0 ? juce::String() : ccPortNames_[id];
        editInsert(path_, [name](magda::InsertConfig& insert) { insert.ccDevice = name; });
    };
    knobCcSelector_.onSelectionChanged = [this](int id) {
        const int paramIndex = selectedKnob();
        editKnobs("Set CC Number", [paramIndex, cc = id - 1](magda::DeviceInfo& device) {
            insert_cc::setKnobCc(device, paramIndex, cc);
        });
    };

    latencyValue_.setEditable(true);
    latencyValue_.setFont(FontManager::getInstance().getUIFontMedium(13.0f));
    latencyValue_.setColour(juce::Label::backgroundColourId,
                            ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
    latencyValue_.setColour(juce::Label::outlineColourId,
                            ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    latencyValue_.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
    latencyValue_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(latencyValue_);
    latencyValue_.onTextChange = [this] {
        const auto ms = latencyValue_.getText().getDoubleValue();
        editInsert(path_, [ms](magda::InsertConfig& insert) { insert.manualAdjustMs = ms; });
    };

    learnButton_ = std::make_unique<magda::SvgButton>("Learn", BinaryData::learn_svg,
                                                      BinaryData::learn_svgSize);
    learnButton_->setTooltip("Learn: click a knob, then turn a hardware control");
    learnButton_->setActiveColor(ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
    learnButton_->onClick = [this] { setLearning(!learning_); };
    prevPage_.onClick = [this] { showPage(page_ - 1); };
    nextPage_.onClick = [this] { showPage(page_ + 1); };
    pageLabel_.setFont(FontManager::getInstance().getMonoFont(10.0f));
    pageLabel_.setColour(juce::Label::textColourId,
                         ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    pageLabel_.setJustificationType(juce::Justification::centred);
    styleButton(removeKnob_, "Remove");
    removeKnob_.onClick = [this] {
        const int paramIndex = selectedKnob();
        editKnobs("Remove CC Knob", [paramIndex](magda::DeviceInfo& device) {
            insert_cc::removeKnob(device, paramIndex);
        });
    };
    for (auto* button : std::initializer_list<juce::Component*>{learnButton_.get(), &prevPage_,
                                                                &nextPage_, &removeKnob_})
        addAndMakeVisible(*button);
    addAndMakeVisible(pageLabel_);

    addKnob_.setTooltip("Add a CC knob");
    addKnob_.onClick = [this] {
        const int page = static_cast<int>(device_.insert.ccSlots.size()) / insert_cc::kKnobsPerPage;
        editKnobs("Add CC Knob", [](magda::DeviceInfo& device) { insert_cc::addKnob(device); });
        // Once the grid has counted the new knob's page.
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer(this), page] {
            if (safe != nullptr)
                safe->showPage(page);
        });
    };
    addAndMakeVisible(addKnob_);

    style_ = resolveControlStyle({});
    magda::Config::getInstance().addListener(this);
    magda::SelectionManager::getInstance().addListener(this);
}

ExternalInsertUI::~ExternalInsertUI() {
    removeKnob_.setLookAndFeel(nullptr);
    if (learning_)
        magda::ControllerRouter::getInstance().cancelLearnSession();
    magda::SelectionManager::getInstance().removeListener(this);
    magda::Config::getInstance().removeListener(this);
}

void ExternalInsertUI::configChanged() {
    if (const auto style = resolveControlStyle(device_.controlStyle); style != style_) {
        style_ = style;
        resized();
        repaint();
    }
}

void ExternalInsertUI::updateFromDevice(const magda::DeviceInfo& device) {
    device_ = device;
    page_ = juce::jlimit(0, pageCount() - 1, device.currentParameterPage);
    syncPickers();
    refreshKnobEditor();
    configChanged();
    resized();
    repaint();
}

void ExternalInsertUI::updateFromDevice(const magda::DeviceInfo& device,
                                        const ParamLinkContext* link) {
    if (link != nullptr && link->devicePath.isValid() && link->devicePath != path_) {
        path_ = link->devicePath;
        device_.isInstrument = device.isInstrument;
        buildPickerOptions();
    }
    updateFromDevice(device);
}

void ExternalInsertUI::buildPickerOptions() {
    const auto sendType = isInstrument() ? Endpoint::MIDI : Endpoint::Audio;
    sendLabel_.setText(isInstrument() ? "MIDI to" : "Send to", juce::dontSendNotification);
    sendSelector_.setOptions(
        buildOptions(isInstrument() ? midiOutputNames() : routeNames(false), sendNames_));
    returnSelector_.setOptions(buildOptions(routeNames(true), returnNames_));
    ccPortSelector_.setOptions(buildOptions(midiOutputNames(), ccPortNames_));

    sendSelector_.onSelectionChanged = [this, sendType](int id) {
        const auto name = id <= 0 ? juce::String() : sendNames_[id];
        editInsert(path_, [name, sendType](magda::InsertConfig& insert) {
            insert.sendDevice = name;
            insert.sendType = name.isEmpty() ? Endpoint::None : sendType;
        });
    };

    returnSelector_.onSelectionChanged = [this](int id) {
        const auto name = id <= 0 ? juce::String() : returnNames_[id];
        editInsert(path_, [name](magda::InsertConfig& insert) {
            insert.returnDevice = name;
            insert.returnType = name.isEmpty() ? Endpoint::None : Endpoint::Audio;
        });
    };
}

void ExternalInsertUI::syncPickers() {
    const auto& insert = device_.insert;
    sendSelector_.setSelectedId(idForName(sendNames_, insert.sendDevice));
    returnSelector_.setSelectedId(idForName(returnNames_, insert.returnDevice));
    ccPortSelector_.setSelectedId(idForName(ccPortNames_, insert.ccDevice));
    channelSelector_.setSelectedId(insert.midiChannel + 1);
    ccChannelSelector_.setSelectedId(insert.midiChannel + 1);

    const bool routed = insert.sendDevice.isNotEmpty();
    channelSelector_.setReadOnly(!routed, "Ch " + juce::String(insert.midiChannel));
    ccTarget_.setText(routed ? juce::String::fromUTF8("\xe2\x86\x92 ") + insert.sendDevice +
                                   juce::String::fromUTF8(" \xc2\xb7 Ch ") +
                                   juce::String(insert.midiChannel)
                             : juce::String(),
                      juce::dontSendNotification);

    if (!latencyValue_.isBeingEdited())
        latencyValue_.setText(juce::String(insert.manualAdjustMs, 1) + " ms",
                              juce::dontSendNotification);
}

int ExternalInsertUI::selectedKnob() const {
    return insert_cc::findKnob(device_, selectedParam_) != nullptr ? selectedParam_ : -1;
}

void ExternalInsertUI::refreshKnobEditor() {
    const int paramIndex = selectedKnob();
    const auto* knob = insert_cc::findKnob(device_, paramIndex);
    knobCcSelector_.setVisible(knob != nullptr);
    removeKnob_.setVisible(knob != nullptr);
    if (knob != nullptr)
        knobCcSelector_.setSelectedId(knob->ccNumber + 1);
    if (learning_ && knob == nullptr && device_.insert.ccSlots.empty())
        setLearning(false);
}

int ExternalInsertUI::pageCount() const {
    return ExternalInsertLayout::pagesFor(static_cast<int>(device_.insert.ccSlots.size()));
}

void ExternalInsertUI::showPage(int page) {
    page = juce::jlimit(0, pageCount() - 1, page);
    page_ = page;
    if (onPageRequested_)
        onPageRequested_(page);
    resized();
    repaint();
}

void ExternalInsertUI::editKnobs(const juce::String& description,
                                 std::function<void(magda::DeviceInfo&)> edit) {
    if (!path_.isValid())
        return;
    magda::UndoManager::getInstance().executeCommand(
        std::make_unique<magda::EditInsertCommand>(path_, description, std::move(edit)));
}

void ExternalInsertUI::paramSelectionChanged(const magda::ParamSelection& selection) {
    selectedParam_ = selection.devicePath == path_ ? selection.paramIndex : -1;
    refreshKnobEditor();
    resized();
    repaint();
}

void ExternalInsertUI::setLearning(bool learning) {
    learning_ = learning;
    learnButton_->setActive(learning);
    if (learning)
        listenForCc();
    else
        magda::ControllerRouter::getInstance().cancelLearnSession();
    repaint();
}

void ExternalInsertUI::listenForCc() {
    // The next CC from any controller goes to the selected knob; until one is selected, keep
    // listening.
    magda::ControllerRouter::getInstance().beginLearnSession(
        {}, [this, alive = std::weak_ptr<bool>(alive_)](const magda::LearnCapture& capture) {
            if (alive.expired() || !learning_)
                return;
            const int paramIndex = selectedKnob();
            if (capture.msgType != magda::BindingMsgType::CC || paramIndex < 0) {
                listenForCc();
                return;
            }
            editKnobs("Learn CC Number", [paramIndex, cc = capture.number](magda::DeviceInfo& d) {
                insert_cc::setKnobCc(d, paramIndex, cc);
            });
            setLearning(false);
        });
}

void ExternalInsertUI::resized() {
    const auto g = Geometry::of(getLocalBounds(), style_);

    const auto layoutRow = [](juce::Rectangle<int> row, juce::Label& label) {
        label.setBounds(row.removeFromLeft(Geometry::kLabelWidth));
        return row;
    };

    auto send = layoutRow(g.rows[0], sendLabel_);
    channelSelector_.setVisible(isInstrument());
    if (isInstrument()) {
        channelSelector_.setBounds(send.removeFromRight(64));
        send.removeFromRight(Geometry::kGap);
    }
    sendSelector_.setBounds(send.withTrimmedRight(isInstrument() ? 0 : 68));
    returnSelector_.setBounds(layoutRow(g.rows[1], returnLabel_).withTrimmedRight(68));
    latencyValue_.setBounds(layoutRow(g.rows[2], latencyLabel_).withWidth(90));

    auto header = g.ccHeader;
    ccLabel_.setBounds(header.removeFromLeft(Geometry::kLabelWidth));
    const bool paged = pageCount() > 1;
    for (auto* component :
         std::initializer_list<juce::Component*>{&prevPage_, &nextPage_, &pageLabel_})
        component->setVisible(paged);
    if (paged) {
        nextPage_.setBounds(header.removeFromRight(14).withSizeKeepingCentre(10, 10));
        pageLabel_.setBounds(header.removeFromRight(32));
        prevPage_.setBounds(header.removeFromRight(14).withSizeKeepingCentre(10, 10));
        pageLabel_.setText(juce::String(page_ + 1) + "/" + juce::String(pageCount()),
                           juce::dontSendNotification);
        header.removeFromRight(Geometry::kGap);
    }
    learnButton_->setBounds(header.removeFromRight(20).withSizeKeepingCentre(18, 18));
    header.removeFromRight(Geometry::kGap * 2);
    ccTarget_.setVisible(isInstrument());
    ccPortSelector_.setVisible(!isInstrument());
    ccChannelSelector_.setVisible(!isInstrument());
    if (isInstrument()) {
        ccTarget_.setBounds(header);
    } else {
        ccChannelSelector_.setBounds(header.removeFromRight(56));
        header.removeFromRight(Geometry::kGap);
        ccPortSelector_.setBounds(header);
    }

    // The add tile follows the last knob, on the last page.
    const int knobs = static_cast<int>(device_.insert.ccSlots.size());
    const int onPage = knobs - page_ * insert_cc::kKnobsPerPage;
    const bool addShown =
        knobs < insert_cc::kMaxKnobs && onPage >= 0 && onPage < insert_cc::kKnobsPerPage;
    addKnob_.setVisible(addShown);
    if (addShown)
        addKnob_.setBounds(g.cells[static_cast<size_t>(onPage)]);

    auto status = g.status;
    if (knobCcSelector_.isVisible()) {
        removeKnob_.setBounds(status.removeFromRight(60).reduced(0, 1));
        status.removeFromRight(Geometry::kGap);
        knobCcSelector_.setBounds(status.removeFromRight(72).reduced(0, 1));
    }
}

void ExternalInsertUI::paint(juce::Graphics& g) {
    const auto geometry = Geometry::of(getLocalBounds(), style_);
    auto& fonts = FontManager::getInstance();

    // The CC row and everything under it stand apart from the routing above.
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
    g.drawHorizontalLine(geometry.ccHeader.getY() - Geometry::kGap, float(geometry.rows[0].getX()),
                         float(geometry.rows[0].getRight()));
    g.drawHorizontalLine(geometry.status.getY() - 2, float(geometry.status.getX()),
                         float(geometry.status.getRight()));

    const auto& insert = device_.insert;
    const bool hasSend = insert.sendDevice.isNotEmpty();
    const bool hasReturn = insert.returnDevice.isNotEmpty();
    const auto conflict = path_.isValid() ? findPortConflict(path_, insert) : juce::String();

    juce::String text;
    auto colour = ActiveTheme::getColour(ActiveTheme::DEVICE_DIM);
    if (learning_) {
        text = selectedKnob() >= 0 ? juce::String::fromUTF8("Turn a hardware control\xe2\x80\xa6")
                                   : "Click a knob, then turn a hardware control";
        colour = ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE);
    } else if (conflict.isNotEmpty()) {
        text = conflict;
        colour = ActiveTheme::getColour(ActiveTheme::DEVICE_AMBER);
    } else if (hasSend && hasReturn) {
        text = insert.sendDevice + juce::String::fromUTF8(" \xe2\x86\x92 ") + insert.returnDevice;
        colour = ActiveTheme::getColour(ActiveTheme::DEVICE_GREEN);
    } else if (hasSend || hasReturn) {
        text = hasSend ? "No return chosen"
                       : (isInstrument() ? "No MIDI port chosen" : "No send chosen");
        colour = ActiveTheme::getColour(ActiveTheme::DEVICE_AMBER);
    } else {
        text = "Not routed";
    }

    auto status = geometry.status;
    if (knobCcSelector_.isVisible())
        status.removeFromRight(60 + 72 + 2 * Geometry::kGap);
    g.setColour(colour);
    g.fillEllipse(status.removeFromLeft(8).toFloat().withSizeKeepingCentre(6.0f, 6.0f));
    status.removeFromLeft(6);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
    g.setFont(fonts.getUIFont(11.0f));
    g.drawText(text, status, juce::Justification::centredLeft, true);
}

const CompiledPresentationSpec& getExternalInsertPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = "insert",
        .layoutCellCount = 0,
        .layoutCellsPerRow = 0,
        .createPanel = [](juce::String) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<ExternalInsertUI>();
        },
        .preferredSlotWidth = 460,
        .createLayout = []() -> std::unique_ptr<DeviceParamLayout> {
            return std::make_unique<ExternalInsertLayout>();
        },
    };
    return kSpec;
}

}  // namespace magda::daw::ui
