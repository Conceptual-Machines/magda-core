#include "custom_ui/SamplerUI.hpp"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>

#include "DisplayListGraphics.hpp"
#include "core/GestureRouter.hpp"
#include "devices/faust/effects/FilterEngines.hpp"
#include "sampling/SamplerImport.hpp"
#include "ui/components/chain/layout/NodeHeaderStyles.hpp"
#include "ui/components/common/InternalFileDrag.hpp"
#include "ui/state/ExpandedFaceplate.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/CursorManager.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SdkColourRoles.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"
#include "ui/utils/AudioFileTypes.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kSpace = 8;
constexpr int kNameRowH = 24;
constexpr int kTitleH = 22;
constexpr int kTileW = 64;
constexpr int kTileGap = 4;
constexpr int kMaxTileH = 48;  ///< The grid's text tile, so these read as its cells.
constexpr int kZonePanelW = 220;
constexpr int kFaceplateW =
    660;  ///< The tabs' columns and a waveform that still fits its view row.
constexpr float kEnvHandleRadius = 4.5f;
constexpr float kEnvHandleGrab = 9.0f;

/// A value box that sits inside a painted tile: no frame or fill of its own.
void styleTileValue(TextSlider& slider) {
    slider.setDrawFrame(false);
    slider.setShowFillIndicator(false);
    slider.setBackgroundColour(juce::Colours::transparentBlack);
    slider.setFont(FontManager::getInstance().getUIFontMedium(15.0f));
    slider.setTextColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
    slider.setJustification(juce::Justification::centredLeft);
}

void styleTileValue(LinkableTextSlider& slider) {
    styleTileValue(slider.getSlider());
}

// The sampler's Filter Type list: Off, then the Filter device's engines.
const juce::StringArray kFilterTypes{"Off",      "SVF",        "Ladder", "Korg 35",
                                     "Oberheim", "Sallen-Key", "Diode",  "Comb"};

/// What a type plays for each Filter Mode step; past its own modes it plays its first.
juce::StringArray filterModeLabels(int type) {
    using magda::devices::faust::FilterFamily;
    if (type <= 0)
        return {"LP", "BP", "HP", "Notch", "Notch+"};
    const auto family = static_cast<FilterFamily>(type - 1);
    if (family == FilterFamily::Comb)
        return {"Comb+", "Comb-"};
    juce::StringArray all{"LP", "BP", "HP", "Notch", "Notch+"};
    all.removeRange(magda::devices::faust::filterModeCount(family), all.size());
    return all;
}
}  // namespace

SamplerUI::FrameBox::FrameBox()
    : up_("up", 0.75f, ActiveTheme::getColour(ActiveTheme::DEVICE_DIM)),
      down_("down", 0.25f, ActiveTheme::getColour(ActiveTheme::DEVICE_DIM)) {
    box_.setFont(FontManager::getInstance().getMonoFont(11.0f));
    box_.setJustification(juce::Justification::centredLeft);
    box_.setInputRestrictions(10, "0123456789");
    box_.setIndents(6, 4);
    box_.setColour(juce::TextEditor::backgroundColourId,
                   ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
    box_.setColour(juce::TextEditor::outlineColourId,
                   ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    box_.setColour(juce::TextEditor::focusedOutlineColourId,
                   ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
    box_.setColour(juce::TextEditor::textColourId,
                   ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
    box_.onReturnKey = [this] { commit(); };
    box_.onFocusLost = [this] { commit(); };
    box_.onEscapeKey = [this] { setFrames(frames_); };
    addAndMakeVisible(box_);
    // A click on a component that takes no focus hands it to the first editor JUCE finds
    // above it; as a container, the box stays out of that search and only takes a direct click.
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);

    // One frame per click; held, it repeats.
    for (auto [button, step] : {std::pair{&up_, 1}, std::pair{&down_, -1}}) {
        button->setRepeatSpeed(350, 40);
        button->onClick = [this, step = step] {
            frames_ = std::max<juce::int64>(0, frames_ + step);
            setFrames(frames_);
            if (onCommit)
                onCommit(frames_);
        };
        addAndMakeVisible(*button);
    }
}

void SamplerUI::FrameBox::setFrames(juce::int64 frames) {
    frames_ = frames;
    box_.setText(juce::String(frames), false);
}

void SamplerUI::FrameBox::commit() {
    const auto typed = box_.getText().getLargeIntValue();
    if (typed == frames_)
        return;
    frames_ = std::max<juce::int64>(0, typed);
    if (onCommit)
        onCommit(frames_);
}

void SamplerUI::FrameBox::resized() {
    auto area = getLocalBounds();
    auto arrows = area.removeFromRight(12).reduced(0, 2);
    up_.setBounds(arrows.removeFromTop(arrows.getHeight() / 2).reduced(1));
    down_.setBounds(arrows.reduced(1));
    area.removeFromRight(2);
    box_.setBounds(area);
}

SamplerUI::SamplerUI() {
    // Sample name label
    sampleNameLabel_.setText("No sample loaded", juce::dontSendNotification);
    sampleNameLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    sampleNameLabel_.setColour(juce::Label::textColourId,
                               ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
    sampleNameLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(sampleNameLabel_);

    // Load button (folder icon)
    loadButton_ = std::make_unique<magda::SvgButton>("Load Sample", BinaryData::folderopen_svg,
                                                     BinaryData::folderopen_svgSize);
    node_header::applyDeviceIconStyle(*loadButton_, node_header::DeviceIcon::Action);
    loadButton_->setTooltip("Load a sample");
    loadButton_->onClick = [this]() {
        if (onLoadSampleRequested)
            onLoadSampleRequested();
    };
    addAndMakeVisible(*loadButton_);

    // Root note slider (MIDI note 0-127, displayed as note name)
    rootNoteSlider_.setRange(0, 127, 1);
    rootNoteSlider_.setValue(60, juce::dontSendNotification);
    styleTileValue(rootNoteSlider_);
    rootNoteSlider_.setFont(FontManager::getInstance().getMonoFont(11.0f));
    rootNoteSlider_.setValueFormatter([](double v) {
        static const char* noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                          "F#", "G",  "G#", "A",  "A#", "B"};
        int note = juce::roundToInt(v);
        int octave = (note / 12) - 2;  // C3 = 60
        return juce::String(noteNames[note % 12]) + juce::String(octave);
    });
    rootNoteSlider_.setValueParser([](const juce::String& text) {
        // Parse note names like "C3", "F#4", "Bb2"
        juce::String t = text.trim().toUpperCase();
        if (t.isEmpty())
            return 60.0;
        static const char* noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                          "F#", "G",  "G#", "A",  "A#", "B"};
        // Try sharp notation first
        int semitone = -1;
        int nameLen = 0;
        for (int i = 0; i < 12; ++i) {
            juce::String nn(noteNames[i]);
            if (t.startsWith(nn) && nn.length() > nameLen) {
                semitone = i;
                nameLen = nn.length();
            }
        }
        // Handle flat (b) as alias for sharp of note below
        if (t.length() >= 2 && t[1] == 'B' && t[0] >= 'A' && t[0] <= 'G') {
            // e.g., "Bb" = A#
            for (int i = 0; i < 12; ++i) {
                juce::String nn(noteNames[i]);
                if (nn.length() == 1 && nn[0] == t[0]) {
                    semitone = (i + 11) % 12;  // one semitone below
                    nameLen = 2;
                    break;
                }
            }
        }
        if (semitone < 0)
            return 60.0;
        juce::String octStr = t.substring(nameLen).trim();
        int octave = octStr.isEmpty() ? 3 : octStr.getIntValue();
        return juce::jlimit(0.0, 127.0, static_cast<double>((octave + 2) * 12 + semitone));
    });
    rootNoteSlider_.onValueChanged = [this](double value) {
        if (zoneList_)
            editSelectedZone([&value](auto& zone) { zone.rootNote = juce::roundToInt(value); });
        else if (onRootNoteChanged)
            onRootNoteChanged(juce::roundToInt(value));
    };
    addAndMakeVisible(rootNoteSlider_);

    setupLabel(rootNoteLabel_, "ROOT");
    rootNoteLabel_.setFont(
        FontManager::getInstance().getMonoFont(10.0f).withExtraKerningFactor(0.12f));
    rootNoteLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(rootNoteLabel_);

    // --- Time slider setup helper ---
    auto setupTimeSlider = [this](LinkableTextSlider& slider, int paramIndex, double min,
                                  double max, double defaultVal) {
        slider.setRange(min, max, 0.001);
        slider.setValue(defaultVal, juce::dontSendNotification);
        slider.setValueFormatter([](double v) {
            if (v < 0.01)
                return juce::String(v * 1000.0, 1) + " ms";
            if (v < 1.0)
                return juce::String(v * 1000.0, 0) + " ms";
            return juce::String(v, 2) + " s";
        });
        slider.setValueParser([](const juce::String& text) {
            juce::String t = text.trim();
            if (t.endsWithIgnoreCase("ms"))
                return static_cast<double>(t.dropLastCharacters(2).trim().getFloatValue()) / 1000.0;
            if (t.endsWithIgnoreCase("s"))
                return static_cast<double>(t.dropLastCharacters(1).trim().getFloatValue());
            double v = t.getDoubleValue();
            return v > 10.0 ? v / 1000.0 : v;  // assume ms if > 10
        });
        slider.onValueChanged = [this, paramIndex](double value) {
            // A zone of a list keeps its own markers (params 7-10), not the parameters.
            if (zoneList_ && paramIndex >= 7 && paramIndex <= 10)
                editSelectedZone([paramIndex, value](auto& zone) {
                    double* const markers[] = {&zone.region.start, &zone.region.end,
                                               &zone.region.loopStart, &zone.region.loopEnd};
                    *markers[paramIndex - 7] = value;
                });
            else if (onParameterChanged)
                onParameterChanged(paramIndex, static_cast<float>(value));
            repaint();
        };
        styleTileValue(slider);
        addAndMakeVisible(slider);
    };

    // --- Sample start slider (param index 7) ---
    setupTimeSlider(startSlider_, 7, 0.0, 300.0, 0.0);

    // --- Sample end slider (param index 8) ---
    setupTimeSlider(endSlider_, 8, 0.0, 300.0, 0.0);

    // --- Loop start slider (param index 9) ---
    setupTimeSlider(loopStartSlider_, 9, 0.0, 300.0, 0.0);

    // --- Loop end slider (param index 10) ---
    setupTimeSlider(loopEndSlider_, 10, 0.0, 300.0, 0.0);

    // --- Loop toggle button (SVG icon) ---
    loopButton_ =
        std::make_unique<magda::SvgButton>("Loop", BinaryData::loop_svg, BinaryData::loop_svgSize);
    node_header::applyDeviceIconStyle(*loopButton_, node_header::DeviceIcon::Toggle,
                                      juce::Colour(0xFFBCBCBC), ActiveTheme::ACCENT_ATTENTION);
    // An 18 px glyph, at the view choice's height rather than a header icon's 12.
    loopButton_->setIconPadding(3.0f);
    loopButton_->setClickingTogglesState(false);
    loopButton_->setTooltip("Loop between the loop markers");
    loopButton_->onClick = [this]() {
        bool newState = !loopButton_->isActive();
        loopButton_->setActive(newState);
        if (zoneList_)
            editSelectedZone([newState](auto& zone) { zone.region.loop = newState; });
        else if (onLoopEnabledChanged)
            onLoopEnabledChanged(newState);
        lockLoopBoxes();
        repaint();
    };
    addAndMakeVisible(*loopButton_);

    // --- 0-1 shown as a percentage ---
    auto setupPercentSlider = [this](LinkableTextSlider& slider, int paramIndex,
                                     double defaultVal) {
        slider.setRange(0.0, 1.0, 0.01);
        slider.setValue(defaultVal, juce::dontSendNotification);
        slider.setValueFormatter(
            [](double v) { return juce::String(static_cast<int>(v * 100)) + "%"; });
        slider.setValueParser([](const juce::String& text) {
            juce::String t = text.trim();
            if (t.endsWithIgnoreCase("%"))
                t = t.dropLastCharacters(1).trim();
            double v = t.getDoubleValue();
            return v > 1.0 ? v / 100.0 : v;
        });
        slider.onValueChanged = [this, paramIndex](double value) {
            if (onParameterChanged)
                onParameterChanged(paramIndex, static_cast<float>(value));
            repaint();
        };
        styleTileValue(slider);
        addAndMakeVisible(slider);
    };

    // --- Whole semitones ---
    auto setupSemitoneSlider = [this](LinkableTextSlider& slider, int paramIndex, double range) {
        slider.setRange(-range, range, 1.0);
        slider.setValue(0.0, juce::dontSendNotification);
        slider.setValueFormatter(
            [](double v) { return juce::String(static_cast<int>(v)) + " st"; });
        slider.setValueParser([](const juce::String& text) {
            return static_cast<double>(
                text.trim().upToFirstOccurrenceOf(" ", false, false).getIntValue());
        });
        slider.onValueChanged = [this, paramIndex](double value) {
            if (onParameterChanged)
                onParameterChanged(paramIndex, static_cast<float>(value));
            repaint();
        };
        styleTileValue(slider);
        addAndMakeVisible(slider);
    };

    // --- ADSR sliders ---
    setupTimeSlider(attackSlider_, 0, 0.001, 5.0, 0.001);
    setupTimeSlider(decaySlider_, 1, 0.001, 5.0, 0.1);
    setupPercentSlider(sustainSlider_, 2, 1.0);
    setupTimeSlider(releaseSlider_, 3, 0.001, 10.0, 0.1);

    // --- Pitch envelope (params 14-18) ---
    setupTimeSlider(pitchAttackSlider_, 14, 0.001, 5.0, 0.001);
    setupTimeSlider(pitchDecaySlider_, 15, 0.001, 5.0, 0.2);
    setupPercentSlider(pitchSustainSlider_, 16, 0.0);
    setupTimeSlider(pitchReleaseSlider_, 17, 0.001, 10.0, 0.1);
    setupSemitoneSlider(pitchEnvAmountSlider_, 18, 48.0);

    setupSemitoneSlider(pitchSlider_, 4, 24.0);

    // --- Filter (params 19-30) ---
    auto setupChoiceSlider = [this](LinkableTextSlider& slider, int paramIndex, int count,
                                    std::function<juce::String(int)> label) {
        slider.setRange(0.0, static_cast<double>(count - 1), 1.0);
        slider.setValue(0.0, juce::dontSendNotification);
        slider.setValueFormatter([label](double v) { return label(juce::roundToInt(v)); });
        slider.setValueParser([label, count](const juce::String& text) {
            for (int i = 0; i < count; ++i)
                if (label(i).equalsIgnoreCase(text.trim()))
                    return static_cast<double>(i);
            return text.getDoubleValue();
        });
        slider.onValueChanged = [this, paramIndex](double value) {
            if (onParameterChanged)
                onParameterChanged(paramIndex, static_cast<float>(value));
            refreshFilterTiles();
            repaint();
        };
        styleTileValue(slider);
        addAndMakeVisible(slider);
    };
    setupChoiceSlider(filterTypeSlider_, 19, kFilterTypes.size(),
                      [](int i) { return kFilterTypes[i]; });
    setupChoiceSlider(filterModeSlider_, 20, 5, [this](int i) {
        const auto labels = filterModeLabels(juce::roundToInt(filterTypeSlider_.getValue()));
        return labels[i < labels.size() ? i : 0];
    });
    setupChoiceSlider(filterPolesSlider_, 21, 3, [](int i) { return juce::String(2 + 2 * i); });
    for (auto* slider : {&filterTypeSlider_, &filterModeSlider_, &filterPolesSlider_})
        slider->setVisible(false);

    filterTypeBox_.addItemList(kFilterTypes, 1);
    filterTypeBox_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    filterTypeBox_.setJustificationType(juce::Justification::centredRight);
    filterTypeBox_.setSelectedId(1, juce::dontSendNotification);
    filterTypeBox_.onChange = [this] {
        filterTypeSlider_.setValue(filterTypeBox_.getSelectedId() - 1, juce::sendNotificationSync);
    };
    addAndMakeVisible(filterTypeBox_);
    lookAndFeelChanged();
    filterModeChoice_.onChange = [this](int index) {
        filterModeSlider_.setValue(index, juce::sendNotificationSync);
    };
    addAndMakeVisible(filterModeChoice_);
    filterPolesChoice_.setOptions({"2", "4", "6"});
    filterPolesChoice_.onChange = [this](int index) {
        filterPolesSlider_.setValue(index, juce::sendNotificationSync);
    };
    addAndMakeVisible(filterPolesChoice_);

    filterCutoffSlider_.setRange(20.0, 20000.0, 1.0);
    filterCutoffSlider_.getSlider().setSkewForCentre(1000.0);
    filterCutoffSlider_.setValue(1000.0, juce::dontSendNotification);
    filterCutoffSlider_.setValueFormatter([](double v) {
        return v < 1000.0 ? juce::String(juce::roundToInt(v)) + " Hz"
                          : juce::String(v / 1000.0, v < 10000.0 ? 2 : 1) + " kHz";
    });
    filterCutoffSlider_.setValueParser([](const juce::String& text) {
        const auto t = text.trim();
        const double v = t.getDoubleValue();
        return t.containsIgnoreCase("k") ? v * 1000.0 : v;
    });
    filterCutoffSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(22, static_cast<float>(value));
    };
    styleTileValue(filterCutoffSlider_);
    addAndMakeVisible(filterCutoffSlider_);

    setupPercentSlider(filterResonanceSlider_, 23, 0.0);
    setupPercentSlider(filterKeyTrackSlider_, 24, 0.0);
    setupPercentSlider(filterVelocitySlider_, 25, 0.0);
    setupTimeSlider(filterAttackSlider_, 26, 0.001, 5.0, 0.001);
    setupTimeSlider(filterDecaySlider_, 27, 0.001, 5.0, 0.3);
    setupPercentSlider(filterSustainSlider_, 28, 0.0);
    setupTimeSlider(filterReleaseSlider_, 29, 0.001, 10.0, 0.1);
    setupSemitoneSlider(filterEnvAmountSlider_, 30, 72.0);

    // --- Fine slider (-100 to +100 cents) ---
    fineSlider_.setRange(-100.0, 100.0, 1.0);
    fineSlider_.setValue(0.0, juce::dontSendNotification);
    fineSlider_.setValueFormatter(
        [](double v) { return juce::String(static_cast<int>(v)) + " ct"; });
    fineSlider_.setValueParser([](const juce::String& text) {
        return static_cast<double>(
            text.trim().upToFirstOccurrenceOf(" ", false, false).getIntValue());
    });
    fineSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(5, static_cast<float>(value));
    };
    styleTileValue(fineSlider_);
    addAndMakeVisible(fineSlider_);

    // --- Level slider (-60 to +12 dB) ---
    levelSlider_.setRange(-60.0, 12.0, 0.1);
    levelSlider_.setValue(0.0, juce::dontSendNotification);
    levelSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(6, static_cast<float>(value));
        // Update waveform scaling to reflect level
        waveformGain_ = juce::Decibels::decibelsToGain(static_cast<float>(value));
        waveformView_.setGain(waveformGain_);
        if (hasWaveform_)
            repaint();
    };
    styleTileValue(levelSlider_);
    addAndMakeVisible(levelSlider_);

    setupPercentSlider(velAmountSlider_, 11, 1.0);

    // --- Glide (param 13): portamento time ---
    glideSlider_.setRange(0.0, 2000.0, 1.0);
    glideSlider_.setValue(0.0, juce::dontSendNotification);
    glideSlider_.setValueFormatter([](double v) {
        return v < 1000.0 ? juce::String(static_cast<int>(v)) + " ms"
                          : juce::String(v / 1000.0, 2) + " s";
    });
    glideSlider_.setValueParser([](const juce::String& text) {
        juce::String t = text.trim();
        if (t.endsWithIgnoreCase("ms"))
            return static_cast<double>(t.dropLastCharacters(2).trim().getFloatValue());
        if (t.endsWithIgnoreCase("s"))
            return static_cast<double>(t.dropLastCharacters(1).trim().getFloatValue()) * 1000.0;
        return t.getDoubleValue();
    });
    glideSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(13, static_cast<float>(value));
    };
    glideSlider_.setParamIndex(13);
    styleTileValue(glideSlider_);
    addAndMakeVisible(glideSlider_);

    // --- Voice Mode (param 12): the slot footer shows it; this carries its links ---
    voiceModeSlider_.setRange(0.0, 2.0, 1.0);
    voiceModeSlider_.setValue(0.0, juce::dontSendNotification);
    voiceModeSlider_.setParamIndex(12);
    voiceModeSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(12, static_cast<float>(value));
    };
    addChildComponent(voiceModeSlider_);

    // --- The slice in frames: boxes drive the marker sliders, which stay as the link carriers.
    static constexpr const char* kFrameCaptions[] = {"START", "END", "LOOP IN", "LOOP OUT"};
    LinkableTextSlider* const markerSliders[] = {&startSlider_, &endSlider_, &loopStartSlider_,
                                                 &loopEndSlider_};
    for (size_t i = 0; i < frameBoxes_.size(); ++i) {
        auto& caption = frameCaptions_[i];
        caption.setText(kFrameCaptions[i], juce::dontSendNotification);
        caption.setFont(
            FontManager::getInstance().getMonoFont(10.0f).withExtraKerningFactor(0.08f));
        caption.setColour(juce::Label::textColourId,
                          ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
        addAndMakeVisible(caption);
        frameBoxes_[i].onCommit = [this, slider = markerSliders[i]](juce::int64 frames) {
            // Unrounded: the slider's millisecond step would swallow a one-frame arrow step.
            slider->getSlider().setValueWithInterval(static_cast<double>(frames) / sampleRate_, 0.0,
                                                     juce::sendNotificationSync);
            syncFrameBoxes();
            repaint();
        };
        addAndMakeVisible(frameBoxes_[i]);
        markerSliders[i]->setVisible(false);
    }

    viewChoice_.setOptions({"Sample", "Pitch", "Filter", "Amp"});
    viewChoice_.setSelectedIndex(0);
    viewChoice_.onChange = [this](int index) { showView(index); };
    addAndMakeVisible(viewChoice_);

    // --- Labels ---
    setupLabel(startLabel_, "Start");
    setupLabel(endLabel_, "End");
    setupLabel(loopStartLabel_, "Loop in");
    setupLabel(loopEndLabel_, "Loop out");
    setupLabel(attackLabel_, "Attack");
    setupLabel(decayLabel_, "Decay");
    setupLabel(sustainLabel_, "Sustain");
    setupLabel(releaseLabel_, "Release");
    setupLabel(pitchLabel_, "Pitch");
    setupLabel(fineLabel_, "Fine");
    setupLabel(levelLabel_, "Level");
    setupLabel(velAmountLabel_, "Velocity");
    setupLabel(glideLabel_, "Glide");
    setupLabel(pitchEnvAmountLabel_, "Env");
    setupLabel(filterModeLabel_, "Mode");
    setupLabel(filterPolesLabel_, "Poles");
    setupLabel(filterCutoffLabel_, "Cutoff");
    setupLabel(filterResonanceLabel_, "Res");
    setupLabel(filterKeyTrackLabel_, "Key");
    setupLabel(filterVelocityLabel_, "Vel");
    setupLabel(filterEnvAmountLabel_, "Env");
    refreshFilterTiles();
    for (auto* label : {&startLabel_, &endLabel_, &loopStartLabel_, &loopEndLabel_})
        label->setVisible(false);

    waveformView_.setChrome({.ruler = true, .scrollBar = true});
    showView(0);
    lockLoopBoxes();

    zoneEditor_.onSelect = [this](int index) { selectZone(index); };
    zoneEditor_.onEdit = [this](int index, const daw::audio::SamplerZoneSettings& zone) {
        if (onZoneEdited)
            onZoneEdited(index, zone);
    };
    zoneEditor_.onRemove = [this](int index) {
        if (onZoneRemoved)
            onZoneRemoved(index);
    };
    addChildComponent(zoneEditor_);

    addZoneButton_.setTooltip("Add an empty zone; drop a sample on it to fill it");
    addZoneButton_.onClick = [this] {
        selectAddedZone_ = true;
        if (onEmptyZoneAdded)
            onEmptyZoneAdded();
    };
    zoneListContent_.addAndMakeVisible(addZoneButton_);
    zoneListView_.setViewedComponent(&zoneListContent_, false);
    zoneListView_.setScrollBarsShown(true, false);
    zoneListView_.setScrollBarThickness(6);
    addAndMakeVisible(zoneListView_);
}

void SamplerUI::selectZone(int index) {
    if (index < 0 || index >= static_cast<int>(zones_.size()))
        return;
    selectedZone_ = index;
    zoneEditor_.setSelected(index);
    showSelectedZone();
    refreshZoneRows();
}

void SamplerUI::setZonesShown(bool shown) {
    if (shown == zonesShown_)
        return;
    zonesShown_ = shown;
    resized();
    repaint();
}

int SamplerUI::preferredWidth() const {
    return kFaceplateW + (zonesShown_ ? kZonePanelW + kSpace : 0);
}

int SamplerUI::zoneRowAt(juce::Point<int> position) const {
    for (size_t i = 0; i < zoneRows_.size(); ++i)
        if (zoneRows_[i]->getScreenBounds().contains(localPointToGlobal(position)))
            return static_cast<int>(i);
    return -1;
}

void SamplerUI::refreshZoneRows() {
    // Rows are kept while the count holds, so a drag on one survives a refresh.
    if (zoneRows_.size() != zones_.size()) {
        zoneRows_.clear();
        for (size_t i = 0; i < zones_.size(); ++i) {
            auto row = std::make_unique<SamplerZoneRow>();
            row->onSelect = [this](int index) { selectZone(index); };
            row->onEdit = [this](int index, const daw::audio::SamplerZoneSettings& zone) {
                if (onZoneEdited)
                    onZoneEdited(index, zone);
            };
            row->onRemove = [this](int index) {
                if (onZoneRemoved)
                    onZoneRemoved(index);
            };
            zoneListContent_.addAndMakeVisible(*row);
            zoneRows_.push_back(std::move(row));
        }
    }
    for (size_t i = 0; i < zones_.size(); ++i)
        zoneRows_[i]->setZone(zones_[i].settings, static_cast<int>(i),
                              static_cast<int>(i) == selectedZone_);
    layoutZoneList();
}

void SamplerUI::layoutZoneList() {
    auto area = zonePanel_.withTrimmedTop(zoneTabs_.getHeight() + 4);
    if (area.isEmpty())
        return;
    constexpr int kGap = 4;
    constexpr int kAddHeight = 22;
    const int rows = static_cast<int>(zoneRows_.size());
    const int content = rows * (SamplerZoneRow::kHeight + kGap) + kAddHeight;
    zoneListView_.setBounds(area);
    const int width = area.getWidth() - (content > area.getHeight() ? 8 : 0);
    zoneListContent_.setSize(width, content);
    auto list = zoneListContent_.getLocalBounds();
    for (auto& row : zoneRows_) {
        row->setView(zoneView_);
        row->setBounds(list.removeFromTop(SamplerZoneRow::kHeight));
        list.removeFromTop(kGap);
    }
    addZoneButton_.setBounds(list.removeFromTop(kAddHeight));
}

juce::Rectangle<int> SamplerUI::zoneTabBounds(SamplerZoneRow::View view) const {
    const int width = zoneTabs_.getWidth() / 3;
    return zoneTabs_.withWidth(width).translated(width * static_cast<int>(view), 0);
}

void SamplerUI::setZones(std::vector<ZoneView> zones, bool list, std::uint64_t generation) {
    if (generation == zoneGeneration_)
        return;
    zoneGeneration_ = generation;
    zones_ = std::move(zones);
    zoneList_ = list;
    if (std::exchange(selectAddedZone_, false))
        selectedZone_ = static_cast<int>(zones_.size()) - 1;
    selectedZone_ =
        juce::jlimit(0, juce::jmax(0, static_cast<int>(zones_.size()) - 1), selectedZone_);
    std::vector<daw::audio::SamplerZoneSettings> settings;
    for (const auto& zone : zones_)
        settings.push_back(zone.settings);
    zoneEditor_.setZones(settings);
    zoneEditor_.setSelected(zones_.empty() ? -1 : selectedZone_);
    showSelectedZone();
    refreshZoneRows();
}

void SamplerUI::showSelectedZone() {
    if (zones_.empty()) {
        shownAudio_.reset();
        setWaveformData(nullptr, 0.0, 0.0);
        return;
    }
    const auto& zone = zones_[static_cast<size_t>(selectedZone_)];
    // A new map that shares this zone's audio keeps the view's zoom and scroll.
    if (zone.audio != shownAudio_) {
        shownAudio_ = zone.audio;
        if (zone.audio != nullptr)
            setWaveformData(&zone.audio->buffer, zone.audio->sourceRate,
                            zone.audio->lengthSeconds());
        else
            setWaveformData(nullptr, 0.0, 0.0);
    }
    rootNoteSlider_.setValue(zone.settings.rootNote, juce::dontSendNotification);
    if (!zoneList_)
        return;

    hasSampleName_ = true;
    sampleNameLabel_.setText(juce::File(zone.settings.source).getFileNameWithoutExtension() +
                                 juce::String::fromUTF8("  ·  zone ") +
                                 juce::String(selectedZone_ + 1) + " of " +
                                 juce::String(zones_.size()),
                             juce::dontSendNotification);
    sampleNameLabel_.setColour(juce::Label::textColourId,
                               ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
    const auto& region = zone.settings.region;
    for (auto [slider, value] :
         {std::pair{&startSlider_, region.start}, std::pair{&endSlider_, region.end},
          std::pair{&loopStartSlider_, region.loopStart},
          std::pair{&loopEndSlider_, region.loopEnd}})
        slider->getSlider().setValueWithInterval(value, 0.0, juce::dontSendNotification);
    loopButton_->setActive(region.loop);
    lockLoopBoxes();
    syncFrameBoxes();
    repaint();
}

void SamplerUI::editSelectedZone(
    const std::function<void(daw::audio::SamplerZoneSettings&)>& change) {
    if (zones_.empty())
        return;
    auto zone = zones_[static_cast<size_t>(selectedZone_)].settings;
    change(zone);
    zones_[static_cast<size_t>(selectedZone_)].settings = zone;
    if (onZoneEdited)
        onZoneEdited(selectedZone_, zone);
}

void SamplerUI::showView(int index) {
    view_ = static_cast<View>(juce::jlimit(0, 3, index));
    pitchView_ = view_ == View::Pitch;
    filterView_ = view_ == View::Filter;
    ampView_ = view_ == View::Amp;
    envDrag_ = EnvHandle::None;
    for (auto view : {View::Sample, View::Pitch, View::Filter, View::Amp})
        for (auto* control : controlsFor(view))
            control->setVisible(view == view_);
    // One set of stage names, over whichever envelope's tiles are showing.
    for (auto* label : {&attackLabel_, &decayLabel_, &sustainLabel_, &releaseLabel_})
        label->setVisible(view_ != View::Sample);
    loopButton_->setVisible(view_ == View::Sample);
    syncWaveformMarkers();
    resized();
    repaint();
}

std::vector<juce::Component*> SamplerUI::controlsFor(View view) {
    switch (view) {
        case View::Sample: {
            std::vector<juce::Component*> controls;
            for (auto& box : frameBoxes_)
                controls.push_back(&box);
            for (auto& caption : frameCaptions_)
                controls.push_back(&caption);
            return controls;
        }
        case View::Pitch:
            return {&pitchSlider_,          &fineSlider_,          &glideSlider_,
                    &pitchEnvAmountSlider_, &pitchLabel_,          &fineLabel_,
                    &glideLabel_,           &pitchEnvAmountLabel_, &pitchAttackSlider_,
                    &pitchDecaySlider_,     &pitchSustainSlider_,  &pitchReleaseSlider_};
        case View::Filter:
            return {&filterTypeBox_,        &filterModeChoice_,      &filterPolesChoice_,
                    &filterCutoffSlider_,   &filterResonanceSlider_, &filterKeyTrackSlider_,
                    &filterVelocitySlider_, &filterEnvAmountSlider_, &filterModeLabel_,
                    &filterPolesLabel_,     &filterCutoffLabel_,     &filterResonanceLabel_,
                    &filterKeyTrackLabel_,  &filterVelocityLabel_,   &filterEnvAmountLabel_,
                    &filterAttackSlider_,   &filterDecaySlider_,     &filterSustainSlider_,
                    &filterReleaseSlider_};
        case View::Amp:
            return {&attackSlider_, &decaySlider_,     &sustainSlider_, &releaseSlider_,
                    &levelSlider_,  &velAmountSlider_, &levelLabel_,    &velAmountLabel_};
    }
    return {};
}

void SamplerUI::lockLoopBoxes() {
    // frameBoxes_ is START, END, LOOP IN, LOOP OUT.
    const bool loopOn = loopButton_->isActive();
    for (size_t i : {size_t{2}, size_t{3}}) {
        frameBoxes_[i].setEnabled(loopOn);
        frameBoxes_[i].setAlpha(loopOn ? 1.0f : 0.4f);
        frameCaptions_[i].setAlpha(loopOn ? 1.0f : 0.4f);
    }
}

void SamplerUI::syncFrameBoxes() {
    const LinkableTextSlider* const markerSliders[] = {&startSlider_, &endSlider_,
                                                       &loopStartSlider_, &loopEndSlider_};
    for (size_t i = 0; i < frameBoxes_.size(); ++i)
        frameBoxes_[i].setFrames(
            static_cast<juce::int64>(std::llround(markerSliders[i]->getValue() * sampleRate_)));
}

SamplerUI::~SamplerUI() {
    stopTimer();
    filterTypeBox_.setLookAndFeel(nullptr);
}

void SamplerUI::setupLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(12.5f));
    label.setColour(juce::Label::textColourId, ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    label.setJustificationType(juce::Justification::centredLeft);
    label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label);
}

void SamplerUI::lookAndFeelChanged() {
    // Re-apply cached theme colours after a live theme switch.
    sampleNameLabel_.setColour(juce::Label::textColourId,
                               ActiveTheme::getColour(hasSampleName_ ? ActiveTheme::DEVICE_TEXT
                                                                     : ActiveTheme::DEVICE_DIM2));
    filterTypeBox_.setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    filterTypeBox_.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    filterTypeBox_.setColour(juce::ComboBox::textColourId,
                             ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
    filterTypeBox_.setColour(juce::ComboBox::arrowColourId,
                             ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    for (auto* label : {&rootNoteLabel_,        &startLabel_,          &endLabel_,
                        &loopStartLabel_,       &loopEndLabel_,        &attackLabel_,
                        &decayLabel_,           &sustainLabel_,        &releaseLabel_,
                        &pitchLabel_,           &fineLabel_,           &levelLabel_,
                        &velAmountLabel_,       &glideLabel_,          &pitchEnvAmountLabel_,
                        &filterModeLabel_,      &filterPolesLabel_,    &filterCutoffLabel_,
                        &filterResonanceLabel_, &filterKeyTrackLabel_, &filterVelocityLabel_,
                        &filterEnvAmountLabel_})
        label->setColour(juce::Label::textColourId,
                         ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    repaint();
}

void SamplerUI::updateParameters(float attack, float decay, float sustain, float release,
                                 float pitch, float fine, float level, float sampleStart,
                                 float sampleEnd, bool loopEnabled, float loopStart, float loopEnd,
                                 float velAmount, const juce::String& sampleName, int rootNote,
                                 float voiceMode, float glide) {
    attackSlider_.setValue(attack, juce::dontSendNotification);
    decaySlider_.setValue(decay, juce::dontSendNotification);
    sustainSlider_.setValue(sustain, juce::dontSendNotification);
    releaseSlider_.setValue(release, juce::dontSendNotification);
    pitchSlider_.setValue(pitch, juce::dontSendNotification);
    fineSlider_.setValue(fine, juce::dontSendNotification);
    levelSlider_.setValue(level, juce::dontSendNotification);
    waveformGain_ = juce::Decibels::decibelsToGain(level);
    waveformView_.setGain(waveformGain_);
    velAmountSlider_.setValue(velAmount, juce::dontSendNotification);

    glideSlider_.setValue(glide, juce::dontSendNotification);
    voiceModeSlider_.setValue(std::round(voiceMode), juce::dontSendNotification);

    // A zone of a list has its own name, root, markers and loop; showSelectedZone() sets them.
    if (zoneList_) {
        repaint();
        return;
    }

    rootNoteSlider_.setValue(rootNote, juce::dontSendNotification);

    // The markers keep frame precision, as the frame boxes set them.
    for (auto [slider, value] :
         {std::pair{&startSlider_, sampleStart}, std::pair{&endSlider_, sampleEnd},
          std::pair{&loopStartSlider_, loopStart}, std::pair{&loopEndSlider_, loopEnd}})
        slider->getSlider().setValueWithInterval(value, 0.0, juce::dontSendNotification);
    loopButton_->setActive(loopEnabled);
    lockLoopBoxes();

    hasSampleName_ = sampleName.isNotEmpty();
    if (hasSampleName_) {
        sampleNameLabel_.setText(sampleName, juce::dontSendNotification);
        sampleNameLabel_.setColour(juce::Label::textColourId,
                                   ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
    } else {
        sampleNameLabel_.setText("No sample loaded", juce::dontSendNotification);
        sampleNameLabel_.setColour(juce::Label::textColourId,
                                   ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
    }
    syncFrameBoxes();
    repaint();
}

std::array<LinkableTextSlider*, SamplerUI::kFilterSlotCount> SamplerUI::filterSliders() {
    return {&filterTypeSlider_,     &filterModeSlider_,      &filterPolesSlider_,
            &filterCutoffSlider_,   &filterResonanceSlider_, &filterKeyTrackSlider_,
            &filterVelocitySlider_, &filterAttackSlider_,    &filterDecaySlider_,
            &filterSustainSlider_,  &filterReleaseSlider_,   &filterEnvAmountSlider_};
}

void SamplerUI::updateFilter(const std::array<float, kFilterSlotCount>& values) {
    const auto sliders = filterSliders();
    for (size_t i = 0; i < sliders.size(); ++i)
        sliders[i]->setValue(values[i], juce::dontSendNotification);
    refreshFilterTiles();
    repaint();
}

void SamplerUI::refreshFilterTiles() {
    using magda::devices::faust::FilterFamily;
    const int type = juce::roundToInt(filterTypeSlider_.getValue());
    const bool on = type > 0;
    const auto family = static_cast<FilterFamily>(juce::jmax(0, type - 1));
    const bool modes = on && magda::devices::faust::filterModeCount(family) > 1;
    const bool poles =
        on && family == FilterFamily::SVF &&
        juce::roundToInt(filterModeSlider_.getValue()) < magda::devices::faust::kNotchPlusMode;
    constexpr float kDim = 0.4f;
    for (auto* slider : filterSliders())
        if (slider != &filterTypeSlider_)
            slider->setAlpha(on ? 1.0f : kDim);
    filterTypeBox_.setSelectedId(type + 1, juce::dontSendNotification);
    const auto modeLabels = filterModeLabels(type);
    const int mode = juce::roundToInt(filterModeSlider_.getValue());
    filterModeChoice_.setOptions(modeLabels);
    filterModeChoice_.setSelectedIndex(mode < modeLabels.size() ? mode : 0);
    filterModeChoice_.setAlpha(modes ? 1.0f : kDim);
    filterPolesChoice_.setSelectedIndex(juce::roundToInt(filterPolesSlider_.getValue()));
    filterPolesChoice_.setAlpha(poles ? 1.0f : kDim);
}

void SamplerUI::updatePitchEnvelope(float attack, float decay, float sustain, float release,
                                    float amount) {
    pitchAttackSlider_.setValue(attack, juce::dontSendNotification);
    pitchDecaySlider_.setValue(decay, juce::dontSendNotification);
    pitchSustainSlider_.setValue(sustain, juce::dontSendNotification);
    pitchReleaseSlider_.setValue(release, juce::dontSendNotification);
    pitchEnvAmountSlider_.setValue(amount, juce::dontSendNotification);
    repaint();
}

void SamplerUI::setWaveformData(const juce::AudioBuffer<float>* buffer, double sampleRate,
                                double sampleLengthSeconds) {
    sampleLength_ = sampleLengthSeconds;
    if (sampleRate > 0.0)
        sampleRate_ = sampleRate;

    if (buffer == nullptr || buffer->getNumSamples() == 0) {
        hasWaveform_ = false;
        waveformView_.setSource(nullptr, 0.0);
        waveformSource_.reset();
        stopTimer();
        repaint();
        return;
    }

    // Update slider ranges to match sample length
    startSlider_.setRange(0.0, sampleLengthSeconds, 0.001);
    endSlider_.setRange(0.0, sampleLengthSeconds, 0.001);
    loopStartSlider_.setRange(0.0, sampleLengthSeconds, 0.001);
    loopEndSlider_.setRange(0.0, sampleLengthSeconds, 0.001);

    // Default end to sample length if not yet set
    if (endSlider_.getValue() < 0.001)
        endSlider_.setValue(sampleLengthSeconds, juce::dontSendNotification);

    hasWaveform_ = true;

    // The view draws channel 0 and fits the whole sample to the pane.
    waveformSource_ = std::make_unique<sdk::BufferWaveformSource>(buffer->getReadPointer(0),
                                                                  buffer->getNumSamples());
    const auto waveArea = getWaveformBounds();
    waveformView_.setSize(waveArea.getWidth(), waveArea.getHeight());
    waveformView_.setSource(waveformSource_.get(), sampleLength_);

    if (!isTimerRunning())
        startTimerHz(30);

    syncFrameBoxes();
    repaint();
}

bool SamplerUI::isInterestedInFileDrag(const juce::StringArray& files) {
    return std::ranges::any_of(files, [](const juce::String& path) {
        const juce::File file(path);
        return isAudioFile(path) || file.isDirectory() || sampler_import::isInstrumentFile(file);
    });
}

void SamplerUI::fileDragEnter(const juce::StringArray& /*files*/, int /*x*/, int /*y*/) {
    dropHover_ = true;
    repaint();
}

void SamplerUI::fileDragExit(const juce::StringArray& /*files*/) {
    dropHover_ = false;
    repaint();
}

void SamplerUI::filesDropped(const juce::StringArray& files, int x, int y) {
    dropHover_ = false;
    repaint();
    // An instrument replaces the zones wherever it lands.
    for (const auto& path : files)
        if (const juce::File file(path); sampler_import::isInstrumentFile(file)) {
            if (onInstrumentChosen)
                onInstrumentChosen(file);
            return;
        }
    juce::Array<juce::File> dropped;
    for (const auto& path : files)
        if (const juce::File file(path); file.isDirectory() || isAudioFile(path))
            dropped.add(file);
    if (dropped.isEmpty())
        return;

    // On a zone's row, one fills that zone; elsewhere on the list each joins the zones;
    // elsewhere several or a folder become a map by name (#3098).
    const int row = zoneRowAt({x, y});
    if (row >= 0 && dropped.size() == 1 && !dropped.getFirst().isDirectory()) {
        if (onZoneSampleChosen)
            onZoneSampleChosen(row, dropped.getFirst());
        return;
    }
    if (zonePanel_.contains(x, y)) {
        if (onZonesAdded)
            onZonesAdded(dropped);
        return;
    }
    if (dropped.size() > 1 || dropped.getFirst().isDirectory()) {
        if (onFilesDropped)
            onFilesDropped(dropped);
        return;
    }
    // One on the zone map adds a zone on the key under it; anywhere else it replaces.
    const auto onMap = zoneEditor_.getLocalPoint(this, juce::Point<int>(x, y));
    if (zoneEditor_.isVisible() && zoneEditor_.gridContains(onMap)) {
        if (onZoneAdded)
            onZoneAdded(dropped.getFirst(), zoneEditor_.keyAt(static_cast<float>(onMap.x)));
        return;
    }
    if (listZone() >= 0) {
        if (onZoneSampleChosen)
            onZoneSampleChosen(listZone(), dropped.getFirst());
        return;
    }
    if (onFileDropped)
        onFileDropped(dropped.getFirst());
}

// Samples dragged from MAGDA's own browser come through as an internal
// {type:"files"} drag rather than an OS file drag, so route them back into the
// FileDragAndDropTarget handlers above. See InternalFileDrag.hpp.
bool SamplerUI::isInterestedInDragSource(const SourceDetails& details) {
    return magda::dnd::acceptsFilesDrag(*this, details);
}

void SamplerUI::itemDragEnter(const SourceDetails& details) {
    magda::dnd::forwardFilesDragEnter(*this, details);
}

void SamplerUI::itemDragMove(const SourceDetails& details) {
    magda::dnd::forwardFilesDragMove(*this, details);
}

void SamplerUI::itemDragExit(const SourceDetails& details) {
    magda::dnd::forwardFilesDragExit(*this, details);
}

void SamplerUI::itemDropped(const SourceDetails& details) {
    magda::dnd::forwardFilesDrop(*this, details);
}

// =============================================================================
// Coordinate Mapping
// =============================================================================

juce::Rectangle<int> SamplerUI::getWaveformBounds() const {
    return waveArea_;
}

namespace {
/// Each stage owns a fixed share of the width; its handle travels that share on a log scale.
constexpr float kAttackShare = 0.22f, kDecayShare = 0.26f, kHoldShare = 0.2f, kReleaseShare = 0.32f;

float stageFraction(const TextSlider& slider) {
    const double lo = slider.getMinimum(), hi = slider.getMaximum();
    return static_cast<float>(std::log(juce::jlimit(lo, hi, slider.getValue()) / lo) /
                              std::log(hi / lo));
}

double stageValue(const TextSlider& slider, float fraction) {
    const double lo = slider.getMinimum(), hi = slider.getMaximum();
    return lo * std::pow(hi / lo, static_cast<double>(juce::jlimit(0.0f, 1.0f, fraction)));
}
}  // namespace

juce::Rectangle<float> SamplerUI::envelopeArea() const {
    auto area = waveArea_.toFloat().reduced(12.0f, 0.0f);
    return area.withTrimmedTop(12.0f).withTrimmedBottom(18.0f);
}

SamplerUI::EnvSliders SamplerUI::envSliders() {
    if (filterView_)
        return {&filterAttackSlider_, &filterDecaySlider_, &filterSustainSlider_,
                &filterReleaseSlider_};
    if (pitchView_)
        return {&pitchAttackSlider_, &pitchDecaySlider_, &pitchSustainSlider_,
                &pitchReleaseSlider_};
    return {&attackSlider_, &decaySlider_, &sustainSlider_, &releaseSlider_};
}

bool SamplerUI::envelopeInverted() const {
    return (pitchView_ && pitchEnvAmountSlider_.getValue() < 0.0) ||
           (filterView_ && filterEnvAmountSlider_.getValue() < 0.0);
}

float SamplerUI::envelopeLevelAt(float y) const {
    const auto area = envelopeArea();
    const float fromBottom = (area.getBottom() - y) / area.getHeight();
    return juce::jlimit(0.0f, 1.0f, envelopeInverted() ? 1.0f - fromBottom : fromBottom);
}

std::optional<SamplerUI::EnvShape> SamplerUI::envelopeShape() {
    if (!showsEnvelope() || waveArea_.isEmpty())
        return std::nullopt;
    const auto area = envelopeArea();
    const auto env = envSliders();
    const float w = area.getWidth();
    const bool inverted = envelopeInverted();
    const auto y = [&](float level) {
        return inverted ? area.getY() + level * area.getHeight()
                        : area.getBottom() - level * area.getHeight();
    };
    const float x0 = area.getX();
    const float peakX = x0 + w * kAttackShare * stageFraction(env.attack->getSlider());
    const float decayX =
        x0 + w * kAttackShare + w * kDecayShare * stageFraction(env.decay->getSlider());
    const float holdX = x0 + w * (kAttackShare + kDecayShare + kHoldShare);
    const float endX = holdX + w * kReleaseShare * stageFraction(env.release->getSlider());
    const auto sustain = static_cast<float>(env.sustain->getValue());
    return EnvShape{{x0, y(0.0f)},
                    {peakX, y(1.0f)},
                    {decayX, y(sustain)},
                    {holdX, y(sustain)},
                    {endX, y(0.0f)}};
}

SamplerUI::EnvHandle SamplerUI::envHandleAt(juce::Point<float> position) {
    const auto shape = envelopeShape();
    if (!shape.has_value())
        return EnvHandle::None;
    for (const auto& [handle, point] :
         {std::pair{EnvHandle::Release, shape->end}, std::pair{EnvHandle::Decay, shape->decayEnd},
          std::pair{EnvHandle::Attack, shape->peak}})
        if (position.getDistanceFrom(point) <= kEnvHandleGrab)
            return handle;
    return EnvHandle::None;
}

void SamplerUI::dragEnvelope(EnvHandle handle, juce::Point<float> position) {
    const auto area = envelopeArea();
    const auto env = envSliders();
    const float w = area.getWidth();
    const auto within = [&](float start, float share) {
        return (position.x - start) / (w * share);
    };
    switch (handle) {
        case EnvHandle::Attack:
            env.attack->setValue(
                stageValue(env.attack->getSlider(), within(area.getX(), kAttackShare)),
                juce::sendNotificationSync);
            break;
        case EnvHandle::Decay:
            env.decay->setValue(stageValue(env.decay->getSlider(),
                                           within(area.getX() + w * kAttackShare, kDecayShare)),
                                juce::sendNotificationSync);
            env.sustain->setValue(envelopeLevelAt(position.y), juce::sendNotificationSync);
            break;
        case EnvHandle::Release:
            env.release->setValue(
                stageValue(env.release->getSlider(),
                           within(area.getX() + w * (kAttackShare + kDecayShare + kHoldShare),
                                  kReleaseShare)),
                juce::sendNotificationSync);
            break;
        case EnvHandle::None:
            break;
    }
    repaint();
}

void SamplerUI::syncWaveformMarkers() {
    // The loop's markers while it is on, the start and end's otherwise.
    const bool loopOn = loopButton_->isActive();
    sdk::WaveformMarkers markers;
    markers.start = startSlider_.getValue();
    markers.end = endSlider_.getValue();
    markers.startEnd = !loopOn;
    markers.loop = loopOn;
    markers.loopStart = loopStartSlider_.getValue();
    markers.loopEnd = loopEndSlider_.getValue();
    waveformView_.setMarkers(markers);
}

void SamplerUI::applyWaveformMarkers(const sdk::WaveformResponse& response) {
    if (response.markersChanged) {
        const auto& m = waveformView_.markers();
        const auto push = [](LinkableTextSlider& slider, double value) {
            if (slider.getValue() != value)
                slider.setValue(value, juce::sendNotificationSync);
        };
        push(startSlider_, m.start);
        push(endSlider_, m.end);
        push(loopStartSlider_, m.loopStart);
        push(loopEndSlider_, m.loopEnd);
        // The sliders round to their interval; the view follows what they kept.
        syncWaveformMarkers();
        syncFrameBoxes();
    }
    if (response.repaint || response.markersChanged)
        repaint();
}

sdk::WaveformPointer SamplerUI::waveformPointer(const juce::MouseEvent& e) const {
    const auto local = e.getPosition() - getWaveformBounds().getPosition();
    return {static_cast<float>(local.x), static_cast<float>(local.y), e.mods.isShiftDown(),
            e.mods.isCommandDown(),      e.mods.isAltDown(),          e.mods.isMiddleButtonDown()};
}

void SamplerUI::updateWaveformCursor(const juce::MouseEvent& e) {
    if (!getWaveformBounds().contains(e.getPosition()) || !hasWaveform_) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    syncWaveformMarkers();
    switch (waveformView_.cursor(waveformPointer(e))) {
        case sdk::WaveformCursor::ResizeLeftRight:
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            break;
        case sdk::WaveformCursor::DraggingHand:
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            break;
        case sdk::WaveformCursor::Zoom:
            setMouseCursor(CursorManager::getInstance().getZoomCursor());
            break;
        case sdk::WaveformCursor::ZoomIn:
            setMouseCursor(CursorManager::getInstance().getZoomInCursor());
            break;
        case sdk::WaveformCursor::ZoomOut:
            setMouseCursor(CursorManager::getInstance().getZoomOutCursor());
            break;
        case sdk::WaveformCursor::Normal:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
    }
}

// =============================================================================
// Mouse Interaction on Waveform
// =============================================================================

void SamplerUI::showRulerMenu() {
    const bool samples = waveformView_.chrome().units == sdk::WaveformUnits::Samples;
    juce::PopupMenu menu;
    menu.addItem(1, "Seconds", true, !samples);
    menu.addItem(2, "Samples", true, samples);
    menu.showMenuAsync(juce::PopupMenu::Options(),
                       [safe = juce::Component::SafePointer<SamplerUI>(this)](int result) {
                           if (safe == nullptr || result == 0)
                               return;
                           auto chrome = safe->waveformView_.chrome();
                           chrome.units = result == 2 ? sdk::WaveformUnits::Samples
                                                      : sdk::WaveformUnits::Seconds;
                           safe->waveformView_.setChrome(chrome);
                           safe->repaint(safe->getWaveformBounds());
                       });
}

void SamplerUI::mouseDown(const juce::MouseEvent& e) {
    for (const auto view :
         {SamplerZoneRow::View::Mix, SamplerZoneRow::View::Key, SamplerZoneRow::View::Velocity})
        if (zoneTabBounds(view).contains(e.getPosition())) {
            zoneView_ = view;
            layoutZoneList();
            repaint(zoneTabs_);
            return;
        }
    if (!showsEnvelope() && hasWaveform_ && e.mods.isPopupMenu() &&
        getWaveformBounds().contains(e.getPosition()) &&
        waveformView_.overRuler(waveformPointer(e))) {
        showRulerMenu();
        return;
    }
    envDrag_ = envHandleAt(e.position);
    if (showsEnvelope()) {
        waveformGesture_ = false;
        repaint();
        return;
    }
    waveformGesture_ = false;
    // An empty well is the load button's bigger twin, as its hint says.
    if (!hasWaveform_ && getWaveformBounds().contains(e.getPosition())) {
        if (onLoadSampleRequested)
            onLoadSampleRequested();
        return;
    }
    if (!getWaveformBounds().contains(e.getPosition()) || !hasWaveform_) {
        Component::mouseDown(e);
        return;
    }
    waveformGesture_ = true;
    syncWaveformMarkers();
    applyWaveformMarkers(waveformView_.pointerDown(waveformPointer(e)));
    if (e.mods.isCommandDown())
        setMouseCursor(CursorManager::getInstance().getZoomCursor());
}

void SamplerUI::mouseDrag(const juce::MouseEvent& e) {
    if (envDrag_ != EnvHandle::None) {
        dragEnvelope(envDrag_, e.position);
        return;
    }
    if (!waveformGesture_ || !hasWaveform_) {
        Component::mouseDrag(e);
        return;
    }
    applyWaveformMarkers(waveformView_.pointerDrag(waveformPointer(e)));
    if (e.mods.isCommandDown())
        updateWaveformCursor(e);
}

void SamplerUI::mouseUp(const juce::MouseEvent& e) {
    if (envDrag_ != EnvHandle::None) {
        envDrag_ = EnvHandle::None;
        repaint();
        return;
    }
    if (waveformGesture_)
        waveformView_.pointerUp(waveformPointer(e));
    waveformGesture_ = false;
    updateWaveformCursor(e);
}

void SamplerUI::mouseMove(const juce::MouseEvent& e) {
    if (showsEnvelope()) {
        setMouseCursor(envHandleAt(e.position) != EnvHandle::None
                           ? juce::MouseCursor::DraggingHandCursor
                           : juce::MouseCursor::NormalCursor);
        return;
    }
    if (!hasWaveform_ && getWaveformBounds().contains(e.getPosition())) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        return;
    }
    if (envHandleAt(e.position) != EnvHandle::None) {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    updateWaveformCursor(e);
}

void SamplerUI::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (showsEnvelope() || !getWaveformBounds().contains(e.getPosition()) || !hasWaveform_ ||
        sampleLength_ <= 0.0) {
        Component::mouseWheelMove(e, wheel);
        return;
    }

    const auto gesture = magda::GestureRouter::getInstance().resolve(
        magda::GestureContext::Sampler, wheel, e.mods, e.getPosition());
    if (gesture.type != magda::GestureActionType::ZoomHorizontal) {
        Component::mouseWheelMove(e, wheel);
        return;
    }

    applyWaveformMarkers(
        waveformView_.zoomBy(1.0 + static_cast<double>(gesture.magnitude), waveformPointer(e).x));
}

// =============================================================================
// Timer (Playhead Animation)
// =============================================================================

std::vector<LinkableTextSlider*> SamplerUI::getLinkableSliders() {
    // In MagdaSamplerPlugin::ParamIndex order.
    std::vector<LinkableTextSlider*> sliders{
        &attackSlider_,       &decaySlider_,        &sustainSlider_,       &releaseSlider_,
        &pitchSlider_,        &fineSlider_,         &levelSlider_,         &startSlider_,
        &endSlider_,          &loopStartSlider_,    &loopEndSlider_,       &velAmountSlider_,
        &voiceModeSlider_,    &glideSlider_,        &pitchAttackSlider_,   &pitchDecaySlider_,
        &pitchSustainSlider_, &pitchReleaseSlider_, &pitchEnvAmountSlider_};
    for (auto* slider : filterSliders())
        sliders.push_back(slider);
    return sliders;
}

void SamplerUI::timerCallback() {
    if (getPlaybackPosition) {
        // The playhead shows on the zone it plays.
        const bool shown = !getPlaybackZone || getPlaybackZone() == selectedZone_;
        double newPos = shown ? getPlaybackPosition() : 0.0;
        if (std::abs(newPos - playheadPosition_) > 0.0001) {
            playheadPosition_ = newPos;
            repaint(getWaveformBounds());
        }
    }
}

// =============================================================================
// Paint
// =============================================================================

void SamplerUI::paint(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();

    // Tiles behind the value boxes: the grid cell's fill and border.
    for (const auto& tile : tiles_) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
        g.fillRoundedRectangle(tile.toFloat(), 5.0f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
        g.drawRoundedRectangle(tile.toFloat().reduced(0.5f), 5.0f, 1.0f);
    }
    if (!rootChip_.isEmpty()) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
        g.fillRoundedRectangle(rootChip_.toFloat(), 4.0f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
        g.drawRoundedRectangle(rootChip_.toFloat().reduced(0.5f), 4.0f, 1.0f);
    }

    // The zone list's views, tabbed as a rack's chain list is.
    if (!zoneTabs_.isEmpty()) {
        g.setFont(fonts.getMonoFont(10.0f).withExtraKerningFactor(0.08f));
        for (const auto& [view, name] : {std::pair{SamplerZoneRow::View::Mix, "MIX"},
                                         std::pair{SamplerZoneRow::View::Key, "KEY"},
                                         std::pair{SamplerZoneRow::View::Velocity, "VEL"}}) {
            const auto tab = zoneTabBounds(view);
            const bool active = view == zoneView_;
            g.setColour(ActiveTheme::getColour(active ? ActiveTheme::DEVICE_VALUE_TEXT
                                                      : ActiveTheme::DEVICE_DIM2));
            g.drawText(name, tab, juce::Justification::centred, false);
            if (active) {
                g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
                g.fillRect(tab.withTop(tab.getBottom() - 2));
            }
        }
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
        g.fillRect(zoneTabs_.withTop(zoneTabs_.getBottom() - 1));
    }

    g.setFont(fonts.getMonoFont(10.0f).withExtraKerningFactor(0.12f));
    // The envelope column is lit, as the plot beside it draws that envelope.
    const std::array<const char*, 2> titles = [this]() -> std::array<const char*, 2> {
        switch (view_) {
            case View::Pitch:
                return {"PITCH", "PITCH ENV"};
            case View::Filter:
                return {"FILTER", "FILTER ENV"};
            case View::Amp:
                return {"", "AMP"};
            case View::Sample:
                break;
        }
        return {"", "SAMPLE"};
    }();
    for (size_t i = 0; i < sectionTitles_.size(); ++i) {
        g.setColour(ActiveTheme::getColour(
            i == 1 && view_ != View::Sample ? ActiveTheme::DEVICE_BLUE : ActiveTheme::DEVICE_DIM));
        g.drawText(titles[i], sectionTitles_[i], juce::Justification::centredLeft, false);
    }

    const auto waveformArea = getWaveformBounds();
    if (waveformArea.isEmpty())
        return;
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillRoundedRectangle(waveformArea.toFloat(), 4.0f);

    if (showsEnvelope()) {
        paintEnvelope(g);
        return;
    }
    if (!hasWaveform_) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
        g.setFont(fonts.getMonoFont(11.0f));
        g.drawText("Drop a sample or click to load", waveformArea, juce::Justification::centred);
        return;
    }

    syncWaveformMarkers();
    waveformView_.setPlayhead(sampleLength_ > 0.0 ? playheadPosition_ : 0.0);
    waveformView_.render(waveformList_);
    g.saveState();
    g.reduceClipRegion(waveformArea);
    g.setOrigin(waveformArea.getPosition());
    sdk::juce_host::drawDisplayList(g, waveformList_, sdkRoleColour);
    g.restoreState();
}

// The well lights up under a sample being dragged in, whichever view is showing.
void SamplerUI::paintOverChildren(juce::Graphics& g) {
    if (!dropHover_ || waveArea_.isEmpty())
        return;
    const auto well = waveArea_.toFloat();
    const auto accent = ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG).withAlpha(0.7f));
    g.fillRoundedRectangle(well, 4.0f);
    g.setColour(accent.withAlpha(0.12f));
    g.fillRoundedRectangle(well, 4.0f);
    g.setColour(accent);
    g.drawRoundedRectangle(well.reduced(0.75f), 4.0f, 1.5f);
    g.setFont(FontManager::getInstance().getMonoFont(11.0f));
    g.drawText(hasWaveform_ ? "Drop to replace the sample" : "Drop to load", waveArea_,
               juce::Justification::centred);
}

void SamplerUI::paintEnvelope(juce::Graphics& g) {
    const auto shape = envelopeShape();
    if (!shape.has_value())
        return;
    const auto area = envelopeArea();
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(waveArea_);
    // The stage boundaries: the handles never cross them.
    const float shares[] = {kAttackShare, kDecayShare, kHoldShare, kReleaseShare};
    float edges[5] = {area.getX()};
    for (int i = 0; i < 4; ++i)
        edges[i + 1] = edges[i] + shares[i] * area.getWidth();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_GRID));
    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine(juce::roundToInt(edges[i]), area.getY() - 4.0f, shape->start.y);
    g.drawHorizontalLine(juce::roundToInt(shape->start.y), area.getX(), area.getRight());

    const auto colour = ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE);
    if (pitchView_ || filterView_) {
        const auto amount =
            (filterView_ ? filterEnvAmountSlider_ : pitchEnvAmountSlider_).getValue();
        g.setFont(FontManager::getInstance().getMonoFont(10.0f));
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
        g.drawText((amount > 0.0 ? "+" : "") + juce::String(static_cast<int>(amount)) + " st",
                   area.withHeight(14.0f).translated(0.0f, -10.0f),
                   juce::Justification::centredRight, false);
    }
    juce::Path path;
    path.startNewSubPath(shape->start);
    for (const auto point : {shape->peak, shape->decayEnd, shape->releaseStart, shape->end})
        path.lineTo(point);
    juce::Path fill = path;
    fill.closeSubPath();
    g.setColour(colour.withAlpha(0.12f));
    g.fillPath(fill);
    g.setColour(colour.withAlpha(0.9f));
    g.strokePath(path, juce::PathStrokeType(1.5f));

    for (const auto& [handle, point] :
         {std::pair{EnvHandle::Attack, shape->peak}, std::pair{EnvHandle::Decay, shape->decayEnd},
          std::pair{EnvHandle::Release, shape->end}}) {
        const float radius = envDrag_ == handle ? kEnvHandleRadius + 1.5f : kEnvHandleRadius;
        const auto dot = juce::Rectangle<float>(radius * 2, radius * 2).withCentre(point);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
        g.fillEllipse(dot);
        g.setColour(colour);
        g.drawEllipse(dot, 1.5f);
    }

    // Each stage named under its zone.
    g.setFont(FontManager::getInstance().getMonoFont(9.0f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
    const char* names[] = {"A", "D", "S", "R"};
    const float labelY = envelopeInverted() ? area.getBottom() + 2.0f : shape->start.y + 2.0f;
    for (int i = 0; i < 4; ++i)
        g.drawText(names[i],
                   juce::Rectangle<float>(edges[i], labelY, edges[i + 1] - edges[i], 14.0f),
                   juce::Justification::centred, false);
}

void SamplerUI::setExpanded(bool expanded) {
    if (expanded == expanded_)
        return;
    expanded_ = expanded;
    if (expanded) {
        for (auto* child : getChildren())
            if (child->isVisible()) {
                hiddenWhileExpanded_.push_back(child);
                child->setVisible(false);
            }
        loopButton_->setVisible(true);
        zoneEditor_.setVisible(true);
        tiles_.clear();
        sectionTitles_ = {};
        rootChip_ = {};
    } else {
        for (auto* child : hiddenWhileExpanded_)
            child->setVisible(true);
        hiddenWhileExpanded_.clear();
        zoneEditor_.setVisible(false);
    }
}

void SamplerUI::resized() {
    // Expanded into the arrangement, the faceplate is the zone map, the waveform and its loop
    // switch.
    setExpanded(magda::ExpandedFaceplate::getInstance().holds(*this));
    if (expanded_) {
        zonePanel_ = {};
        zoneTabs_ = {};
        auto area = getLocalBounds().reduced(kSpace);
        // The zone map over the selected zone's waveform.
        zoneEditor_.setBounds(area.removeFromTop(juce::jmax(140, area.getHeight() * 2 / 5)));
        area.removeFromTop(kSpace);
        loopButton_->setBounds(
            area.removeFromTop(kTitleH).removeFromLeft(24).withSizeKeepingCentre(24, 24));
        area.removeFromTop(4);
        waveArea_ = area;
        waveformView_.setSize(waveArea_.getWidth(), waveArea_.getHeight());
        return;
    }

    auto area = getLocalBounds().reduced(kSpace);

    // The zone list down the left, as a rack's chains.
    zoneListView_.setVisible(zonesShown_);
    zonePanel_ = zonesShown_ ? area.removeFromLeft(kZonePanelW) : juce::Rectangle<int>();
    zoneTabs_ = zonePanel_.withHeight(kNameRowH);
    layoutZoneList();
    if (zonesShown_)
        area.removeFromLeft(kSpace);

    // Name row: the sample, its root and the load button.
    auto nameRow = area.removeFromTop(kNameRowH);
    loadButton_->setBounds(nameRow.removeFromRight(26).withSizeKeepingCentre(26, 22));
    nameRow.removeFromRight(kSpace);
    rootChip_ = nameRow.removeFromRight(54).withSizeKeepingCentre(54, 22);
    rootNoteSlider_.setBounds(rootChip_.reduced(6, 1));
    nameRow.removeFromRight(6);
    rootNoteLabel_.setBounds(nameRow.removeFromRight(40));
    sampleNameLabel_.setBounds(nameRow);
    area.removeFromTop(6);

    // The tab's columns on the right, the plot takes the rest. A module tab is its own column
    // and its envelope's; Sample and Amp are one column each.
    constexpr int kSectionW = 2 * kTileW + kTileGap;
    constexpr int kFilterW = 3 * kTileW + 2 * kTileGap;
    const int firstW = view_ == View::Filter ? kFilterW : view_ == View::Pitch ? kSectionW : 0;
    auto columns = area.removeFromRight(firstW + (firstW > 0 ? kSpace : 0) + kSectionW);
    area.removeFromRight(kSpace);
    auto viewRow = area.removeFromTop(kTitleH);
    viewChoice_.setBounds(viewRow.removeFromLeft(240).withSizeKeepingCentre(240, 20));
    viewRow.removeFromLeft(6);
    loopButton_->setBounds(viewRow.removeFromLeft(24).withSizeKeepingCentre(24, 24));
    area.removeFromTop(4);
    waveArea_ = area;

    const int tileH = std::min(kMaxTileH, (columns.getHeight() - kTitleH - 2 * kTileGap) / 3);
    tiles_.clear();
    const auto tile = [&](juce::Rectangle<int> column, int col, int row) {
        return juce::Rectangle<int>(column.getX() + col * (kTileW + kTileGap),
                                    column.getY() + row * (tileH + kTileGap), kTileW, tileH);
    };
    const auto place = [&](juce::Rectangle<int> bounds, juce::Label& label,
                           juce::Component& slider) {
        tiles_.push_back(bounds);
        auto inner = bounds.reduced(10, 6);
        label.setBounds(inner.removeFromTop(14));
        slider.setBounds(inner);
    };

    // [0] the module's own column, empty on Sample and Amp; [1] the envelope's, or the slice.
    std::array<juce::Rectangle<int>, 2> column;
    column[0] = columns.removeFromLeft(firstW);
    if (firstW > 0)
        columns.removeFromLeft(kSpace);
    column[1] = columns;
    for (size_t i = 0; i < column.size(); ++i)
        sectionTitles_[i] = column[i].removeFromTop(kTitleH);

    const auto placeStages = [&](LinkableTextSlider& attack, LinkableTextSlider& decay,
                                 LinkableTextSlider& sustain, LinkableTextSlider& release) {
        place(tile(column[1], 0, 0), attackLabel_, attack);
        place(tile(column[1], 1, 0), decayLabel_, decay);
        place(tile(column[1], 0, 1), sustainLabel_, sustain);
        place(tile(column[1], 1, 1), releaseLabel_, release);
    };

    switch (view_) {
        case View::Sample: {
            // A caption, a frame box and its arrows per marker.
            auto frames = column[1];
            for (size_t i = 0; i < frameBoxes_.size(); ++i) {
                auto row = frames.removeFromTop(22);
                frames.removeFromTop(kTileGap);
                frameCaptions_[i].setBounds(row.removeFromLeft(56));
                frameBoxes_[i].setBounds(row);
            }
            break;
        }
        case View::Pitch:
            place(tile(column[0], 0, 0), pitchLabel_, pitchSlider_);
            place(tile(column[0], 1, 0), fineLabel_, fineSlider_);
            place(tile(column[0], 0, 1), glideLabel_, glideSlider_);
            place(tile(column[0], 1, 1), pitchEnvAmountLabel_, pitchEnvAmountSlider_);
            placeStages(pitchAttackSlider_, pitchDecaySlider_, pitchSustainSlider_,
                        pitchReleaseSlider_);
            break;
        case View::Filter:
            // The type picks the model, so it sits in the title row beside the section's name.
            filterTypeBox_.setBounds(sectionTitles_[0].withTrimmedLeft(70).withSizeKeepingCentre(
                sectionTitles_[0].getWidth() - 70, 20));
            place(tile(column[0], 0, 0).getUnion(tile(column[0], 2, 0)), filterModeLabel_,
                  filterModeChoice_);
            place(tile(column[0], 0, 1), filterCutoffLabel_, filterCutoffSlider_);
            place(tile(column[0], 1, 1), filterResonanceLabel_, filterResonanceSlider_);
            place(tile(column[0], 2, 1), filterPolesLabel_, filterPolesChoice_);
            place(tile(column[0], 0, 2), filterKeyTrackLabel_, filterKeyTrackSlider_);
            place(tile(column[0], 1, 2), filterVelocityLabel_, filterVelocitySlider_);
            place(tile(column[0], 2, 2), filterEnvAmountLabel_, filterEnvAmountSlider_);
            placeStages(filterAttackSlider_, filterDecaySlider_, filterSustainSlider_,
                        filterReleaseSlider_);
            break;
        case View::Amp:
            placeStages(attackSlider_, decaySlider_, sustainSlider_, releaseSlider_);
            place(tile(column[1], 0, 2), levelLabel_, levelSlider_);
            place(tile(column[1], 1, 2), velAmountLabel_, velAmountSlider_);
            break;
    }

    // The view keeps the whole sample in reach at the new width.
    waveformView_.setSize(waveArea_.getWidth(), waveArea_.getHeight());
}

}  // namespace magda::daw::ui
