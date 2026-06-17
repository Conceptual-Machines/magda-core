#include "MarkerLaneComponent.hpp"

#include <algorithm>
#include <cmath>

#include "../../../core/Config.hpp"
#include "../../layout/LayoutConfig.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda {

namespace {

constexpr int kMarkerHitRadius = 8;
constexpr int kFlagWidth = 12;
constexpr int kLaneTopInset = 5;
constexpr int kColourMenuBase = 200;  // First menu id for palette colour entries

}  // namespace

MarkerLaneComponent::MarkerLaneComponent() {
    setOpaque(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

MarkerLaneComponent::~MarkerLaneComponent() = default;

void MarkerLaneComponent::setController(TimelineController* controller) {
    timelineListener_.reset(controller);
    if (!controller)
        return;

    const auto& state = controller->getState();
    markers_ = state.markers;
    pixelsPerBeat_ = state.zoom.horizontalZoom;
    timelineLengthBeats_ = state.timelineLengthBeats;
    selectedMarkerId_ = state.selectedMarkerId;
    repaint();
}

void MarkerLaneComponent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::TIMELINE_BACKGROUND));

    auto bounds = getLocalBounds();
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.65f));
    g.fillRect(bounds.removeFromTop(1));
    g.fillRect(bounds.removeFromBottom(1));

    const auto font = FontManager::getInstance().getUIFont(10.0f);
    g.setFont(font);

    for (const auto& marker : markers_) {
        const int x = markerToX(marker);
        if (x < -80 || x > getWidth() + 80)
            continue;

        const bool selected = marker.id == selectedMarkerId_;
        const bool hovered = marker.id == hoveredMarkerId_;
        auto colour = marker.colour;

        juce::Path flag;
        flag.addTriangle(static_cast<float>(x - kFlagWidth / 2), static_cast<float>(kLaneTopInset),
                         static_cast<float>(x + kFlagWidth / 2), static_cast<float>(kLaneTopInset),
                         static_cast<float>(x), static_cast<float>(kLaneTopInset + 12));
        g.setColour(colour.withAlpha(selected || hovered ? 1.0f : 0.82f));
        g.fillPath(flag);

        g.setColour(selected ? juce::Colours::white.withAlpha(0.85f)
                             : DarkTheme::getColour(DarkTheme::BORDER).brighter(0.3f));
        g.strokePath(flag, juce::PathStrokeType(selected ? 1.4f : 1.0f));

        const int labelX = x + 8;
        const int labelW = juce::jmax(0, getWidth() - labelX - 4);
        if (labelW > 20) {
            auto labelColour = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);
            if (selected || hovered)
                labelColour = DarkTheme::getColour(DarkTheme::TEXT_PRIMARY);
            g.setColour(labelColour);
            g.drawFittedText(marker.name, labelX, kLaneTopInset + 1, juce::jmin(96, labelW), 16,
                             juce::Justification::centredLeft, 1);
        }
    }

    // When the active section's marker has scrolled off the left edge, watermark
    // its name pinned at the left so the current section stays identifiable.
    int visibleLeft = 0;
    int visibleRight = getWidth();
    if (auto* vp = findParentComponentOfClass<juce::Viewport>()) {
        visibleLeft = vp->getViewPositionX();
        visibleRight = visibleLeft + vp->getViewWidth();
    }

    const TimelineMarker* active = nullptr;
    for (const auto& marker : markers_) {
        if (markerToX(marker) <= visibleLeft &&
            (active == nullptr || marker.positionBeats > active->positionBeats)) {
            active = &marker;
        }
    }

    if (active != nullptr && markerToX(*active) < visibleLeft) {
        // Stop the watermark short of the next marker's flag if one is near.
        int rightLimit = visibleRight;
        for (const auto& marker : markers_) {
            const int mx = markerToX(marker);
            if (mx > visibleLeft && mx < rightLimit)
                rightLimit = mx;
        }
        const int labelX = visibleLeft + 8;
        const int labelW = juce::jmin(180, rightLimit - labelX - 6);
        if (labelW > 24) {
            g.setFont(FontManager::getInstance().getUIFont(11.0f).italicised());
            g.setColour(active->colour.withAlpha(0.5f));
            g.drawFittedText(active->name, labelX, kLaneTopInset + 1, labelW, 16,
                             juce::Justification::centredLeft, 1);
        }
    }
}

void MarkerLaneComponent::mouseUp(const juce::MouseEvent& event) {
    auto* controller = timelineListener_.get();
    if (!controller)
        return;

    const int markerId = markerAt(event.getPosition());
    if (event.mods.isPopupMenu()) {
        const auto screenPosition = localPointToGlobal(event.getPosition());
        if (markerId != 0) {
            showMarkerMenu(markerId, screenPosition);
        } else {
            showLaneMenu(screenPosition);
        }
        return;
    }

    if (markerId != 0)
        controller->dispatch(GoToMarkerEvent{markerId});
}

void MarkerLaneComponent::mouseDoubleClick(const juce::MouseEvent& event) {
    const int markerId = markerAt(event.getPosition());
    if (markerId == 0)
        return;
    if (const auto* marker = findMarker(markerId))
        showRenameMarkerDialog(markerId, *marker);
}

void MarkerLaneComponent::mouseMove(const juce::MouseEvent& event) {
    const int markerId = markerAt(event.getPosition());
    if (markerId != hoveredMarkerId_) {
        hoveredMarkerId_ = markerId;
        repaint();
    }
    setMouseCursor(markerId != 0 ? juce::MouseCursor::PointingHandCursor
                                 : juce::MouseCursor::NormalCursor);
}

void MarkerLaneComponent::mouseExit(const juce::MouseEvent& /*event*/) {
    if (hoveredMarkerId_ != 0) {
        hoveredMarkerId_ = 0;
        repaint();
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void MarkerLaneComponent::timelineStateChanged(const TimelineState& state, ChangeFlags changes) {
    if (!hasFlag(changes, ChangeFlags::Markers) && !hasFlag(changes, ChangeFlags::Zoom) &&
        !hasFlag(changes, ChangeFlags::Timeline) && !hasFlag(changes, ChangeFlags::Tempo)) {
        return;
    }

    markers_ = state.markers;
    pixelsPerBeat_ = state.zoom.horizontalZoom;
    timelineLengthBeats_ = state.timelineLengthBeats;
    selectedMarkerId_ = state.selectedMarkerId;
    repaint();
}

int MarkerLaneComponent::markerToX(const TimelineMarker& marker) const {
    return static_cast<int>(std::round(marker.positionBeats * pixelsPerBeat_)) +
           LayoutConfig::TIMELINE_LEFT_PADDING;
}

int MarkerLaneComponent::markerAt(juce::Point<int> point) const {
    int bestMarkerId = 0;
    int bestDistance = kMarkerHitRadius + 1;
    for (const auto& marker : markers_) {
        const int distance = std::abs(point.x - markerToX(marker));
        if (distance < bestDistance && point.y >= 0 && point.y <= getHeight()) {
            bestDistance = distance;
            bestMarkerId = marker.id;
        }
    }
    return bestMarkerId;
}

const TimelineMarker* MarkerLaneComponent::findMarker(int markerId) const {
    auto it = std::find_if(markers_.begin(), markers_.end(),
                           [&](const TimelineMarker& marker) { return marker.id == markerId; });
    return it != markers_.end() ? &*it : nullptr;
}

void MarkerLaneComponent::showMarkerMenu(int markerId, juce::Point<int> screenPosition) {
    const auto* marker = findMarker(markerId);
    auto* controller = timelineListener_.get();
    if (!marker || !controller)
        return;

    juce::PopupMenu menu;
    menu.addItem(3, "Rename...");
    menu.addItem(4, "Edit Position...");
    menu.addSeparator();

    // Colour submenu built from the shared track/clip palette (default +
    // user-defined custom colours) rather than a hardcoded marker-only set.
    auto makeChip = [](juce::Colour colour) {
        juce::Image chip(juce::Image::ARGB, 14, 14, true);
        juce::Graphics cg(chip);
        cg.setColour(colour);
        cg.fillRoundedRectangle(0.0f, 0.0f, 14.0f, 14.0f, 2.0f);
        auto drawable = std::make_unique<juce::DrawableImage>();
        drawable->setImage(chip);
        return drawable;
    };

    juce::PopupMenu colourMenu;
    for (size_t i = 0; i < Config::defaultColourPalette.size(); ++i) {
        auto colour = juce::Colour(Config::defaultColourPalette[i].colour);
        colourMenu.addItem(kColourMenuBase + static_cast<int>(i),
                           Config::defaultColourPalette[i].name, true, false, makeChip(colour));
    }

    const auto customPalette = Config::getInstance().getTrackColourPalette();
    const int customColourBase =
        kColourMenuBase + static_cast<int>(Config::defaultColourPalette.size());
    if (!customPalette.empty()) {
        colourMenu.addSeparator();
        for (size_t i = 0; i < customPalette.size(); ++i) {
            auto colour = juce::Colour(customPalette[i].colour);
            colourMenu.addItem(customColourBase + static_cast<int>(i),
                               juce::String(customPalette[i].name), true, false, makeChip(colour));
        }
    }
    menu.addSubMenu("Colour", colourMenu);
    menu.addSeparator();
    menu.addItem(2, "Delete Marker");

    const auto snapshot = *marker;
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea({screenPosition.x, screenPosition.y, 1, 1}),
        [safeThis = juce::Component::SafePointer<MarkerLaneComponent>(this), markerId, snapshot,
         customPalette](int result) {
            if (safeThis == nullptr || result == 0)
                return;
            auto* tc = safeThis->timelineListener_.get();
            if (!tc)
                return;

            if (result == 2) {
                tc->dispatch(RemoveMarkerEvent{markerId});
            } else if (result == 3) {
                safeThis->showRenameMarkerDialog(markerId, snapshot);
            } else if (result == 4) {
                safeThis->showEditPositionDialog(markerId, snapshot);
            } else if (result >= kColourMenuBase) {
                const int defaultCount = static_cast<int>(Config::defaultColourPalette.size());
                const int idx = result - kColourMenuBase;
                juce::Colour colour;
                if (idx < defaultCount) {
                    colour = juce::Colour(Config::getDefaultColour(idx));
                } else {
                    const size_t customIdx = static_cast<size_t>(idx - defaultCount);
                    if (customIdx >= customPalette.size())
                        return;
                    colour = juce::Colour(customPalette[customIdx].colour);
                }
                tc->dispatch(
                    UpdateMarkerEvent{markerId, snapshot.positionBeats, snapshot.name, colour});
            }
        });
}

void MarkerLaneComponent::showLaneMenu(juce::Point<int> screenPosition) {
    auto* controller = timelineListener_.get();
    if (!controller)
        return;

    // The lane "Add Marker" adds at the playhead. Hide it when a marker already
    // sits there, since the add would be a no-op (positions must be unique).
    const auto& state = controller->getState();
    const double playheadBeats = state.playhead.getCurrentPositionBeats();
    constexpr double kSamePositionEpsilon = 1e-6;
    const bool markerAtPlayhead =
        std::any_of(state.markers.begin(), state.markers.end(), [&](const TimelineMarker& m) {
            return std::abs(m.positionBeats - playheadBeats) <= kSamePositionEpsilon;
        });
    if (markerAtPlayhead)
        return;

    juce::PopupMenu menu;
    menu.addItem(1, "Add Marker");

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea({screenPosition.x, screenPosition.y, 1, 1}),
        [safeThis = juce::Component::SafePointer<MarkerLaneComponent>(this)](int result) {
            if (safeThis == nullptr || result != 1)
                return;

            if (auto* tc = safeThis->timelineListener_.get()) {
                const auto& state = tc->getState();
                tc->dispatch(AddMarkerBeatsEvent{state.playhead.getCurrentPositionBeats()});
            }
        });
}

void MarkerLaneComponent::showRenameMarkerDialog(int markerId, const TimelineMarker& marker) {
    auto* alert = new juce::AlertWindow("Rename Marker", "", juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor("name", marker.name, "Name:");
    alert->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MarkerLaneComponent> safeThis(this);
    alert->enterModalState(
        true, juce::ModalCallbackFunction::create([alert, safeThis, markerId, marker](int result) {
            if (result != 1) {
                delete alert;
                return;
            }

            const auto name = alert->getTextEditorContents("name").trim();
            delete alert;
            if (name.isEmpty() || safeThis == nullptr)
                return;

            if (auto* tc = safeThis->timelineListener_.get()) {
                tc->dispatch(
                    UpdateMarkerEvent{markerId, marker.positionBeats, name, marker.colour});
            }
        }));
}

void MarkerLaneComponent::showEditPositionDialog(int markerId, const TimelineMarker& marker) {
    auto* controller = timelineListener_.get();
    if (!controller)
        return;

    const int beatsPerBar = juce::jmax(1, controller->getState().tempo.timeSignatureNumerator);

    // Present the position as 1-indexed bar.beat (beat 1.0 == the downbeat) to
    // match the ruler and transport. positionBeats is 0-indexed absolute beats.
    const int bar = static_cast<int>(marker.positionBeats / beatsPerBar) + 1;
    const double beatInBar =
        marker.positionBeats - static_cast<double>(bar - 1) * beatsPerBar + 1.0;

    auto* alert =
        new juce::AlertWindow("Edit Marker Position", "", juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor("bar", juce::String(bar), "Bar:");
    alert->addTextEditor("beat", juce::String(beatInBar, 3), "Beat:");
    alert->addButton("Set", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MarkerLaneComponent> safeThis(this);
    alert->enterModalState(
        true, juce::ModalCallbackFunction::create([alert, safeThis, markerId, marker,
                                                   beatsPerBar](int result) {
            if (result != 1) {
                delete alert;
                return;
            }

            const int bar = juce::jmax(1, alert->getTextEditorContents("bar").getIntValue());
            const double beat =
                juce::jmax(1.0, alert->getTextEditorContents("beat").getDoubleValue());
            delete alert;
            if (safeThis == nullptr)
                return;

            const double positionBeats =
                juce::jmax(0.0, static_cast<double>(bar - 1) * beatsPerBar + (beat - 1.0));
            if (auto* tc = safeThis->timelineListener_.get()) {
                tc->dispatch(
                    UpdateMarkerEvent{markerId, positionBeats, marker.name, marker.colour});
            }
        }));
}

}  // namespace magda
