// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/SamplerZoneEditor.hpp"

#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kFieldsH = 44;
constexpr int kKeyboardH = 14;
constexpr int kTileGap = 4;
constexpr float kEdgeGrab = 4.0f;

/// C3 is 60, as the faceplate's root chip names it.
juce::String noteName(int note) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    note = juce::jlimit(0, 127, note);
    return juce::String(names[note % 12]) + juce::String(note / 12 - 2);
}

double parseNote(const juce::String& text) {
    if (const auto note = daw::audio::noteFromFileName(text.trim()))
        return *note;
    return text.getDoubleValue();
}

bool isBlackKey(int note) {
    const int pitch = note % 12;
    return pitch == 1 || pitch == 3 || pitch == 6 || pitch == 8 || pitch == 10;
}
}  // namespace

SamplerZoneEditor::SamplerZoneEditor() {
    setWantsKeyboardFocus(true);

    static constexpr const char* kCaptions[] = {"Root",     "Low key", "High key", "Low vel",
                                                "High vel", "Tune",    "Gain"};
    for (int i = 0; i < kFieldCount; ++i) {
        auto& label = labels_[static_cast<size_t>(i)];
        label.setText(kCaptions[i], juce::dontSendNotification);
        label.setFont(FontManager::getInstance().getUIFont(12.5f));
        label.setColour(juce::Label::textColourId, ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);

        auto& field = fields_[static_cast<size_t>(i)];
        field.setDrawFrame(false);
        field.setShowFillIndicator(false);
        field.setBackgroundColour(juce::Colours::transparentBlack);
        field.setFont(FontManager::getInstance().getUIFontMedium(15.0f));
        field.setTextColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
        field.setJustification(juce::Justification::centredLeft);
        field.onValueChanged = [this, i](double value) {
            if (selected_ < 0)
                return;
            auto zone = zones_[static_cast<size_t>(selected_)];
            const int whole = juce::roundToInt(value);
            switch (i) {
                case kRoot:
                    zone.rootNote = whole;
                    break;
                case kLowKey:
                    zone.zones.keyLow = whole;
                    zone.zones.keyHigh = juce::jmax(zone.zones.keyHigh, whole);
                    break;
                case kHighKey:
                    zone.zones.keyHigh = whole;
                    zone.zones.keyLow = juce::jmin(zone.zones.keyLow, whole);
                    break;
                case kLowVelocity:
                    zone.zones.velocityLow = whole;
                    zone.zones.velocityHigh = juce::jmax(zone.zones.velocityHigh, whole);
                    break;
                case kHighVelocity:
                    zone.zones.velocityHigh = whole;
                    zone.zones.velocityLow = juce::jmin(zone.zones.velocityLow, whole);
                    break;
                case kTune:
                    zone.tuneCents = static_cast<float>(value);
                    break;
                case kGain:
                    zone.gainDb = static_cast<float>(value);
                    break;
                default:
                    break;
            }
            edit(zone);
        };
        addAndMakeVisible(field);
    }

    for (auto field : {kRoot, kLowKey, kHighKey}) {
        auto& slider = fields_[static_cast<size_t>(field)];
        slider.setRange(0.0, 127.0, 1.0);
        slider.setValueFormatter([](double v) { return noteName(juce::roundToInt(v)); });
        slider.setValueParser(parseNote);
    }
    for (auto field : {kLowVelocity, kHighVelocity}) {
        auto& slider = fields_[static_cast<size_t>(field)];
        slider.setRange(1.0, 127.0, 1.0);
        slider.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    }
    fields_[kTune].setRange(-1200.0, 1200.0, 1.0);
    fields_[kTune].setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v)) + " ct"; });
    fields_[kGain].setRange(-48.0, 12.0, 0.1);
    fields_[kGain].setValueFormatter([](double v) { return juce::String(v, 1) + " dB"; });

    fileLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    fileLabel_.setColour(juce::Label::textColourId,
                         ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
    fileLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(fileLabel_);
    refreshFields();
}

void SamplerZoneEditor::setZones(const std::vector<Zone>& zones) {
    if (grab_ != Grab::None)
        return;
    zones_ = zones;
    if (selected_ >= static_cast<int>(zones_.size()))
        selected_ = static_cast<int>(zones_.size()) - 1;
    if (selected_ < 0 && !zones_.empty())
        selected_ = 0;
    refreshFields();
    repaint();
}

void SamplerZoneEditor::setSelected(int index) {
    index = juce::jlimit(-1, static_cast<int>(zones_.size()) - 1, index);
    if (index == selected_)
        return;
    selected_ = index;
    refreshFields();
    repaint();
}

void SamplerZoneEditor::refreshFields() {
    const bool any = selected_ >= 0;
    for (auto& field : fields_)
        field.setEnabled(any);
    if (!any) {
        fileLabel_.setText("Drop samples here to map them by note", juce::dontSendNotification);
        return;
    }
    const auto& zone = zones_[static_cast<size_t>(selected_)];
    const double values[] = {static_cast<double>(zone.rootNote), zone.zones.keyLow * 1.0,
                             zone.zones.keyHigh * 1.0,           zone.zones.velocityLow * 1.0,
                             zone.zones.velocityHigh * 1.0,      zone.tuneCents * 1.0,
                             static_cast<double>(zone.gainDb)};
    for (size_t i = 0; i < fields_.size(); ++i)
        fields_[i].setValue(values[i], juce::dontSendNotification);
    fileLabel_.setText(juce::File(zone.source).getFileName() +
                           juce::String::fromUTF8("  ·  zone ") + juce::String(selected_ + 1) +
                           " of " + juce::String(zones_.size()),
                       juce::dontSendNotification);
}

void SamplerZoneEditor::edit(const Zone& zone) {
    zones_[static_cast<size_t>(selected_)] = zone;
    refreshFields();
    repaint();
    if (onEdit)
        onEdit(selected_, zone);
}

void SamplerZoneEditor::resized() {
    auto area = getLocalBounds();
    auto fields = area.removeFromTop(kFieldsH);
    tiles_.clear();
    const int tileW =
        juce::jmin(84, (fields.getWidth() - (kFieldCount - 1) * kTileGap) / (kFieldCount + 2));
    for (size_t i = 0; i < fields_.size(); ++i) {
        auto tile = fields.removeFromLeft(tileW);
        fields.removeFromLeft(kTileGap);
        tiles_.push_back(tile);
        auto inner = tile.reduced(8, 4);
        labels_[i].setBounds(inner.removeFromTop(14));
        fields_[i].setBounds(inner);
    }
    fileLabel_.setBounds(fields.withTrimmedLeft(4));
    area.removeFromTop(6);
    keyboard_ = area.removeFromBottom(kKeyboardH);
    grid_ = area;
}

float SamplerZoneEditor::xForKey(int key) const {
    return static_cast<float>(grid_.getX()) +
           static_cast<float>(grid_.getWidth()) * static_cast<float>(key) / 128.0f;
}

float SamplerZoneEditor::yForVelocity(int velocity) const {
    // Velocity 127 at the top, 1 at the bottom.
    return static_cast<float>(grid_.getBottom()) -
           static_cast<float>(grid_.getHeight()) * static_cast<float>(velocity) / 127.0f;
}

int SamplerZoneEditor::keyAt(float x) const {
    if (grid_.getWidth() <= 0)
        return 60;
    return juce::jlimit(0, 127,
                        static_cast<int>((x - static_cast<float>(grid_.getX())) * 128.0f /
                                         static_cast<float>(grid_.getWidth())));
}

int SamplerZoneEditor::velocityAt(float y) const {
    if (grid_.getHeight() <= 0)
        return 64;
    return juce::jlimit(1, 127,
                        juce::roundToInt((static_cast<float>(grid_.getBottom()) - y) * 127.0f /
                                         static_cast<float>(grid_.getHeight())));
}

juce::Rectangle<float> SamplerZoneEditor::zoneBounds(const Zone& zone) const {
    return juce::Rectangle<float>::leftTopRightBottom(
        xForKey(zone.zones.keyLow), yForVelocity(zone.zones.velocityHigh),
        xForKey(zone.zones.keyHigh + 1), yForVelocity(zone.zones.velocityLow - 1));
}

std::pair<int, SamplerZoneEditor::Grab> SamplerZoneEditor::hit(juce::Point<float> p) const {
    const auto partOf = [this, p](int index) {
        const auto box = zoneBounds(zones_[static_cast<size_t>(index)]);
        if (!box.expanded(kEdgeGrab).contains(p))
            return Grab::None;
        if (std::abs(p.x - box.getX()) <= kEdgeGrab)
            return Grab::LowKey;
        if (std::abs(p.x - box.getRight()) <= kEdgeGrab)
            return Grab::HighKey;
        if (std::abs(p.y - box.getY()) <= kEdgeGrab)
            return Grab::HighVelocity;
        if (std::abs(p.y - box.getBottom()) <= kEdgeGrab)
            return Grab::LowVelocity;
        return box.contains(p) ? Grab::Body : Grab::None;
    };
    if (selected_ >= 0)
        if (const auto part = partOf(selected_); part != Grab::None)
            return {selected_, part};
    // The last drawn is on top.
    for (int i = static_cast<int>(zones_.size()) - 1; i >= 0; --i)
        if (const auto part = partOf(i); part != Grab::None)
            return {i, part};
    return {-1, Grab::None};
}

void SamplerZoneEditor::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    const auto [index, part] = hit(e.position);
    if (index < 0)
        return;
    if (index != selected_) {
        setSelected(index);
        if (onSelect)
            onSelect(index);
    }
    if (e.mods.isPopupMenu()) {
        showZoneMenu(index);
        return;
    }
    grab_ = part;
    dragStart_ = zones_[static_cast<size_t>(index)];
    dragOrigin_ = e.position;
}

void SamplerZoneEditor::mouseDrag(const juce::MouseEvent& e) {
    if (grab_ == Grab::None || selected_ < 0)
        return;
    auto zone = dragStart_;
    const int keyShift = keyAt(e.position.x) - keyAt(dragOrigin_.x);
    const int velocityShift = velocityAt(e.position.y) - velocityAt(dragOrigin_.y);
    switch (grab_) {
        case Grab::Body: {
            const int keys = juce::jlimit(-zone.zones.keyLow, 127 - zone.zones.keyHigh, keyShift);
            const int velocities = juce::jlimit(1 - zone.zones.velocityLow,
                                                127 - zone.zones.velocityHigh, velocityShift);
            zone.zones.keyLow += keys;
            zone.zones.keyHigh += keys;
            zone.rootNote = juce::jlimit(0, 127, zone.rootNote + keys);
            zone.zones.velocityLow += velocities;
            zone.zones.velocityHigh += velocities;
            break;
        }
        case Grab::LowKey:
            zone.zones.keyLow = juce::jlimit(0, zone.zones.keyHigh, zone.zones.keyLow + keyShift);
            break;
        case Grab::HighKey:
            zone.zones.keyHigh =
                juce::jlimit(zone.zones.keyLow, 127, zone.zones.keyHigh + keyShift);
            break;
        case Grab::LowVelocity:
            zone.zones.velocityLow =
                juce::jlimit(1, zone.zones.velocityHigh, zone.zones.velocityLow + velocityShift);
            break;
        case Grab::HighVelocity:
            zone.zones.velocityHigh =
                juce::jlimit(zone.zones.velocityLow, 127, zone.zones.velocityHigh + velocityShift);
            break;
        case Grab::None:
            break;
    }
    zones_[static_cast<size_t>(selected_)] = zone;
    refreshFields();
    repaint();
}

void SamplerZoneEditor::mouseUp(const juce::MouseEvent&) {
    if (grab_ == Grab::None || selected_ < 0)
        return;
    grab_ = Grab::None;
    const auto zone = zones_[static_cast<size_t>(selected_)];
    if (!(zone == dragStart_) && onEdit)
        onEdit(selected_, zone);
}

void SamplerZoneEditor::mouseMove(const juce::MouseEvent& e) {
    switch (hit(e.position).second) {
        case Grab::LowKey:
        case Grab::HighKey:
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            break;
        case Grab::LowVelocity:
        case Grab::HighVelocity:
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            break;
        case Grab::Body:
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            break;
        case Grab::None:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
    }
}

bool SamplerZoneEditor::keyPressed(const juce::KeyPress& key) {
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) &&
        selected_ >= 0) {
        if (onRemove)
            onRemove(selected_);
        return true;
    }
    return false;
}

void SamplerZoneEditor::showZoneMenu(int index) {
    juce::PopupMenu menu;
    menu.addItem(1, "Remove zone");
    menu.showMenuAsync(
        juce::PopupMenu::Options(),
        [safe = juce::Component::SafePointer<SamplerZoneEditor>(this), index](int result) {
            if (safe != nullptr && result == 1 && safe->onRemove)
                safe->onRemove(index);
        });
}

void SamplerZoneEditor::paint(juce::Graphics& g) {
    for (const auto& tile : tiles_) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
        g.fillRoundedRectangle(tile.toFloat(), 5.0f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
        g.drawRoundedRectangle(tile.toFloat().reduced(0.5f), 5.0f, 1.0f);
    }

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillRect(grid_);
    // An octave line at each C, and the keyboard under them.
    g.setFont(FontManager::getInstance().getMonoFont(9.0f));
    for (int key = 0; key < 128; ++key) {
        const float x0 = xForKey(key), x1 = xForKey(key + 1);
        g.setColour(isBlackKey(key) ? ActiveTheme::getColour(ActiveTheme::DEVICE_LINE)
                                    : ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
        g.fillRect(juce::Rectangle<float>(x0, static_cast<float>(keyboard_.getY()), x1 - x0,
                                          static_cast<float>(keyboard_.getHeight()))
                       .reduced(0.25f, 0.0f));
        if (key % 12 == 0) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_GRID));
            g.drawVerticalLine(juce::roundToInt(x0), static_cast<float>(grid_.getY()),
                               static_cast<float>(grid_.getBottom()));
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
            g.drawText(noteName(key),
                       juce::Rectangle<float>(x0 + 2.0f, grid_.getBottom() - 12.0f, 40.0f, 12.0f),
                       juce::Justification::centredLeft, false);
        }
    }

    const auto accent = ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE);
    for (size_t i = 0; i < zones_.size(); ++i) {
        const bool chosen = static_cast<int>(i) == selected_;
        const auto box = zoneBounds(zones_[i]);
        g.setColour(accent.withAlpha(chosen ? 0.35f : 0.14f));
        g.fillRect(box);
        g.setColour(accent.withAlpha(chosen ? 1.0f : 0.5f));
        g.drawRect(box, chosen ? 1.5f : 1.0f);
        // The root as a tick on the zone's floor.
        const float rootX = (xForKey(zones_[i].rootNote) + xForKey(zones_[i].rootNote + 1)) * 0.5f;
        g.drawVerticalLine(juce::roundToInt(rootX), box.getBottom() - 6.0f, box.getBottom());
    }
    if (zones_.empty()) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
        g.setFont(FontManager::getInstance().getMonoFont(11.0f));
        g.drawText("Drop samples or a folder to map them by note", grid_,
                   juce::Justification::centred);
    }
}

}  // namespace magda::daw::ui
