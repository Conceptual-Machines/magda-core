#include "modulation/ModsPanelComponent.hpp"

#include <array>

#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

// AddModButton implementation
AddModButton::AddModButton() = default;

namespace {
constexpr std::array<const char*, 4> kShortcutNames{"LFO", "ENV", "RND", "FOL"};
constexpr std::array<magda::ModType, 4> kShortcutTypes{
    magda::ModType::LFO, magda::ModType::Envelope, magda::ModType::Random,
    magda::ModType::Follower};
}  // namespace

void AddModButton::setPrimary(bool primary) {
    if (primary_ != primary) {
        primary_ = primary;
        repaint();
    }
}

juce::Rectangle<int> AddModButton::shortcutBounds(int index) const {
    auto row = getLocalBounds().reduced(6, 8).removeFromBottom(14);
    const int width = row.getWidth() / static_cast<int>(kShortcutNames.size());
    return row.withWidth(width).translated(width * index, 0);
}

void AddModButton::paint(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    const bool hovered = isMouseOver(true);
    if (!primary_ && !hovered)
        return;

    auto bounds = getLocalBounds();
    const auto dim = ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2);
    const auto text = ActiveTheme::getColour(ActiveTheme::DEVICE_DIM);
    auto label = bounds.withSizeKeepingCentre(bounds.getWidth(), 36).translated(0, -8);
    g.setColour(hovered ? text : dim);
    g.setFont(fonts.getUIFont(16.0f));
    g.drawText("+", label.removeFromTop(18), juce::Justification::centred);
    g.setColour(text);
    g.setFont(fonts.getUIFont(12.0f));
    g.drawText("Add mod", label, juce::Justification::centred);

    if (!primary_)
        return;
    g.setFont(fonts.getMonoFont(9.0f).withExtraKerningFactor(0.06f));
    for (int i = 0; i < static_cast<int>(kShortcutNames.size()); ++i) {
        const auto area = shortcutBounds(i);
        const bool over = hovered && area.contains(getMouseXYRelative());
        g.setColour(over ? ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION) : dim);
        g.drawText(kShortcutNames[static_cast<std::size_t>(i)], area, juce::Justification::centred);
    }
}

void AddModButton::mouseDown(const juce::MouseEvent& e) {
    if (primary_) {
        for (int i = 0; i < static_cast<int>(kShortcutNames.size()); ++i) {
            if (shortcutBounds(i).contains(e.getPosition())) {
                if (onAddMod)
                    onAddMod(kShortcutTypes[static_cast<std::size_t>(i)], magda::LFOWaveform::Sine);
                return;
            }
        }
    }
    showAddMenu();
}

void AddModButton::mouseMove(const juce::MouseEvent&) {
    repaint();
}

void AddModButton::showAddMenu() {
    juce::PopupMenu menu;

    menu.addItem(1, "LFO");
    menu.addItem(2, "Curve");
    menu.addItem(3, "Envelope");
    menu.addItem(4, "Random");
    menu.addItem(5, "Follower");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int result) {
        if (!onAddMod)
            return;
        if (result == 1) {
            // Standard LFO with sine wave
            onAddMod(magda::ModType::LFO, magda::LFOWaveform::Sine);
        } else if (result == 2) {
            // Curve LFO with custom waveform
            onAddMod(magda::ModType::LFO, magda::LFOWaveform::Custom);
        } else if (result == 3) {
            // ADSR envelope generator (waveform unused)
            onAddMod(magda::ModType::Envelope, magda::LFOWaveform::Sine);
        } else if (result == 4) {
            // Random modulator (waveform unused)
            onAddMod(magda::ModType::Random, magda::LFOWaveform::Sine);
        } else if (result == 5) {
            // Envelope follower (waveform unused)
            onAddMod(magda::ModType::Follower, magda::LFOWaveform::Sine);
        }
    });
}

void AddModButton::mouseEnter(const juce::MouseEvent& /*e*/) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    repaint();  // Show the button
}

void AddModButton::mouseExit(const juce::MouseEvent& /*e*/) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();  // Hide the button
}

// ModsPanelComponent implementation

ModsPanelComponent::ModsPanelComponent() : PagedControlPanel(magda::MODS_PER_PAGE) {
    // Enable page management - users can add more pages of empty slots
    setCanAddPage(true);
    setCanRemovePage(true);
    setMinPages(1);  // Always keep at least 1 page

    // Start with 1 page (4 slots) with + buttons in empty slots
    ensureSlotCount(allocatedPages_ * magda::MODS_PER_PAGE);
    markPrimarySlot();
}

void ModsPanelComponent::markPrimarySlot() {
    for (int i = 0; i < static_cast<int>(addButtons_.size()); ++i)
        addButtons_[static_cast<std::size_t>(i)]->setPrimary(i == currentModCount_);
}

void ModsPanelComponent::ensureKnobCount(int count) {
    // Remove excess knobs if count shrunk
    while (static_cast<int>(knobs_.size()) > count) {
        removeChildComponent(knobs_.back().get());
        knobs_.pop_back();
    }

    // Add new knobs if needed
    while (static_cast<int>(knobs_.size()) < count) {
        int i = static_cast<int>(knobs_.size());
        auto knob = std::make_unique<ModKnobComponent>(i);

        // Wire up callbacks with mod index
        knob->onTargetChanged = [this, i](const magda::ControlTarget& target) {
            if (onModTargetChanged) {
                onModTargetChanged(i, target);
            }
        };

        knob->onLinkRemoved = [this, i](const magda::ControlTarget& target) {
            if (onModLinkRemoved) {
                onModLinkRemoved(i, target);
            }
        };

        knob->onAllLinksCleared = [this, i]() {
            if (onModAllLinksCleared) {
                onModAllLinksCleared(i);
            }
        };

        knob->onNameChanged = [this, i](juce::String name) {
            if (onModNameChanged) {
                onModNameChanged(i, name);
            }
        };

        knob->onClicked = [this, i]() {
            // Deselect all other knobs and select this one
            for (auto& k : knobs_) {
                k->setSelected(false);
            }
            knobs_[i]->setSelected(true);

            if (onModClicked) {
                onModClicked(i);
            }
        };

        knob->onRemoveRequested = [this, i]() {
            if (onModRemoveRequested) {
                onModRemoveRequested(i);
            }
        };

        knob->onEnableToggled = [this, i](bool enabled) {
            if (onModEnableToggled) {
                onModEnableToggled(i, enabled);
            }
        };

        knob->setAvailableTargets(availableDevices_);
        knob->setDeviceParamNames(deviceParamNames_);
        // Modifier list is shared verbatim; the knob skips its own ModId
        // when building the menu (it has currentMod_.id; the parent panel
        // doesn't, since the knob owns its ModInfo).
        knob->setAvailableModifiers(availableModifiers_);
        knob->setParentPath(parentPath_);
        addAndMakeVisible(*knob);
        knobs_.push_back(std::move(knob));
    }
}

void ModsPanelComponent::setAvailableModifiers(
    const std::vector<std::pair<magda::ModId, juce::String>>& modifiers) {
    availableModifiers_ = modifiers;
    for (auto& knob : knobs_) {
        knob->setAvailableModifiers(modifiers);
    }
}

void ModsPanelComponent::ensureSlotCount(int count) {
    // NOTE: Do NOT create knobs here - knobs are created on demand when mods are added
    // via ensureKnobCount(currentModCount_) in setMods()

    // Ensure we have enough add buttons for empty slots
    while (static_cast<int>(addButtons_.size()) < count) {
        int slotIndex = static_cast<int>(addButtons_.size());
        auto addButton = std::make_unique<AddModButton>();

        // Wire up callback with slot index - receives type and waveform from popup menu
        addButton->onAddMod = [this, slotIndex](magda::ModType type, magda::LFOWaveform waveform) {
            if (onAddModRequested) {
                onAddModRequested(slotIndex, type, waveform);
            }
        };

        addChildComponent(*addButton);  // Hidden by default
        addButtons_.push_back(std::move(addButton));
    }
}

// Pages of empty slots are the panel's own: nothing in the model holds them.
void ModsPanelComponent::onAddPage() {
    allocatedPages_++;
    ensureSlotCount(allocatedPages_ * magda::MODS_PER_PAGE);
    markPrimarySlot();
    setCurrentPage(allocatedPages_ - 1);
    resized();
    repaint();
}

// Only a page with no mods on it goes.
void ModsPanelComponent::onRemovePage() {
    const int lastPageStart = (allocatedPages_ - 1) * magda::MODS_PER_PAGE;
    if (allocatedPages_ <= 1 || currentModCount_ > lastPageStart)
        return;
    allocatedPages_--;
    setCurrentPage(juce::jmin(getCurrentPage(), allocatedPages_ - 1));
    resized();
    repaint();
}

void ModsPanelComponent::setMods(const magda::ModArray& mods) {
    currentModCount_ = static_cast<int>(mods.size());
    ensureKnobCount(currentModCount_);

    // Calculate required pages based on mod count
    int requiredPages = currentModCount_ > 0
                            ? (currentModCount_ + magda::MODS_PER_PAGE - 1) / magda::MODS_PER_PAGE
                            : 1;
    if (requiredPages > allocatedPages_) {
        allocatedPages_ = requiredPages;
        ensureSlotCount(allocatedPages_ * magda::MODS_PER_PAGE);
        // Clamp current page if it's now out of range
        if (getCurrentPage() >= allocatedPages_)
            setCurrentPage(juce::jmax(0, allocatedPages_ - 1));
    }

    std::vector<magda::ControlTarget> targets;
    for (const auto& mod : mods)
        for (const auto& link : mod.links)
            if (std::ranges::find(targets, link.target) == targets.end())
                targets.push_back(link.target);
    targetCount_ = static_cast<int>(targets.size());
    markPrimarySlot();

    // Update existing mods
    for (size_t i = 0; i < mods.size() && i < knobs_.size(); ++i) {
        // Pass pointer to live mod for waveform animation
        knobs_[i]->setModInfo(mods[i], &mods[i]);
    }

    resized();
    repaint();
}

void ModsPanelComponent::setAvailableDevices(
    const std::vector<std::pair<magda::DeviceId, juce::String>>& devices) {
    availableDevices_ = devices;
    for (auto& knob : knobs_) {
        knob->setAvailableTargets(devices);
    }
}

void ModsPanelComponent::setDeviceParamNames(
    const std::map<magda::DeviceId, std::vector<juce::String>>& paramNames) {
    deviceParamNames_ = paramNames;
    for (auto& knob : knobs_) {
        knob->setDeviceParamNames(paramNames);
    }
}

void ModsPanelComponent::setParentPath(const magda::ChainNodePath& path) {
    parentPath_ = path;
    for (auto& knob : knobs_) {
        knob->setParentPath(path);
    }
}

void ModsPanelComponent::setSelectedModIndex(int modIndex) {
    for (size_t i = 0; i < knobs_.size(); ++i) {
        knobs_[i]->setSelected(static_cast<int>(i) == modIndex);
    }
}

void ModsPanelComponent::repaintWaveforms() {
    DBG("ModsPanelComponent::repaintWaveforms - repainting " + juce::String((int)knobs_.size()) +
        " knobs");
    for (auto& knob : knobs_) {
        knob->repaintWaveform();
    }
}

void ModsPanelComponent::paint(juce::Graphics& g) {
    PagedControlPanel::paint(g);

    // Empty slots are dashed outlines; a mod's card draws its own.
    const int first = getFirstVisibleIndex();
    const float dashes[] = {3.0f, 3.0f};
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE2));
    for (int i = 0; i < getVisibleItemCount(); ++i) {
        if (first + i < currentModCount_)
            continue;
        juce::Path outline;
        outline.addRoundedRectangle(getCellBounds(i).toFloat().reduced(0.5f), 5.0f);
        juce::Path dashed;
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.fillPath(dashed);
    }
}

int ModsPanelComponent::getTotalItemCount() const {
    // Return total allocated slots across all pages
    return allocatedPages_ * magda::MODS_PER_PAGE;
}

juce::Component* ModsPanelComponent::getItemComponent(int index) {
    if (index < 0)
        return nullptr;

    // If this slot has a mod, return the knob
    if (index < currentModCount_ && index < static_cast<int>(knobs_.size())) {
        return knobs_[index].get();
    }

    // Otherwise, return the add button for this empty slot
    if (index < static_cast<int>(addButtons_.size())) {
        return addButtons_[index].get();
    }

    return nullptr;
}

juce::Colour ModsPanelComponent::getTitleColour() const {
    return ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION);
}

juce::String ModsPanelComponent::getFooterText() const {
    return juce::String(currentModCount_) + (currentModCount_ == 1 ? " mod" : " mods") +
           juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(targetCount_) +
           (targetCount_ == 1 ? " target" : " targets");
}

}  // namespace magda::daw::ui
