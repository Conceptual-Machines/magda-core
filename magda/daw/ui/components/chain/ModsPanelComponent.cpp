#include "ModsPanelComponent.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

// AddModButton implementation
AddModButton::AddModButton() = default;

void AddModButton::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Only show content on hover (grid is drawn by parent)
    if (!isMouseOver()) {
        return;
    }

    // Hover state - highlight background
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE).brighter(0.08f));
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

    // + icon
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    auto centerX = bounds.getCentreX();
    auto centerY = bounds.getCentreY();
    float size = 20.0f;
    g.fillRect(centerX - size * 0.5f, centerY - 1.5f, size, 3.0f);
    g.fillRect(centerX - 1.5f, centerY - size * 0.5f, 3.0f, size);

    // "Add Mod" text
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.drawText("Add Mod", bounds.removeFromBottom(16), juce::Justification::centred);
}

void AddModButton::mouseDown(const juce::MouseEvent& /*e*/) {
    if (onClick) {
        onClick();
    }
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

    // Wire up page management callbacks
    onAddPageRequested = [this](int /*itemsPerPage*/) {
        allocatedPages_++;
        ensureSlotCount(allocatedPages_ * magda::MODS_PER_PAGE);
        resized();
        repaint();
    };

    onRemovePageRequested = [this](int /*itemsPerPage*/) {
        if (allocatedPages_ > 1) {
            // Only allow removing page if last page is completely empty
            int lastPageStartIndex = (allocatedPages_ - 1) * magda::MODS_PER_PAGE;
            if (currentModCount_ <= lastPageStartIndex) {
                allocatedPages_--;
                resized();
                repaint();
            }
        }
    };

    // Start with 1 page (8 slots) with + buttons in empty slots
    ensureSlotCount(allocatedPages_ * magda::MODS_PER_PAGE);
}

void ModsPanelComponent::ensureKnobCount(int count) {
    // Add new knobs if needed
    while (static_cast<int>(knobs_.size()) < count) {
        int i = static_cast<int>(knobs_.size());
        auto knob = std::make_unique<ModKnobComponent>(i);

        // Wire up callbacks with mod index
        knob->onAmountChanged = [this, i](float amount) {
            if (onModAmountChanged) {
                onModAmountChanged(i, amount);
            }
        };

        knob->onTargetChanged = [this, i](magda::ModTarget target) {
            if (onModTargetChanged) {
                onModTargetChanged(i, target);
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
        knob->setParentPath(parentPath_);
        addAndMakeVisible(*knob);
        knobs_.push_back(std::move(knob));
    }
}

void ModsPanelComponent::ensureSlotCount(int count) {
    // NOTE: Do NOT create knobs here - knobs are created on demand when mods are added
    // via ensureKnobCount(currentModCount_) in setMods()

    // Ensure we have enough add buttons for empty slots
    while (static_cast<int>(addButtons_.size()) < count) {
        int slotIndex = static_cast<int>(addButtons_.size());
        auto addButton = std::make_unique<AddModButton>();

        // Wire up callback with slot index
        addButton->onClick = [this, slotIndex]() {
            if (onAddModRequested) {
                // Only add LFO for now (as per user request)
                onAddModRequested(slotIndex, magda::ModType::LFO);
            }
        };

        addChildComponent(*addButton);  // Hidden by default
        addButtons_.push_back(std::move(addButton));
    }
}

void ModsPanelComponent::setMods(const magda::ModArray& mods) {
    currentModCount_ = static_cast<int>(mods.size());
    ensureKnobCount(currentModCount_);

    // Calculate required pages based on highest mod index
    if (currentModCount_ > 0) {
        int requiredPages = (currentModCount_ + magda::MODS_PER_PAGE - 1) / magda::MODS_PER_PAGE;
        if (requiredPages > allocatedPages_) {
            allocatedPages_ = requiredPages;
            ensureSlotCount(allocatedPages_ * magda::MODS_PER_PAGE);
        }
    }

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

void ModsPanelComponent::paint(juce::Graphics& g) {
    // Call base class paint for background
    PagedControlPanel::paint(g);

    // Calculate grid area (same as resized() logic in PagedControlPanel)
    auto bounds = getLocalBounds().reduced(2);
    int totalPages = getTotalPages();
    bool showNav = totalPages > 1 || canAddPage() || canRemovePage();
    if (showNav) {
        bounds.removeFromTop(NAV_HEIGHT);
    }

    // Draw grid cells
    int visibleCount = getVisibleItemCount();
    if (visibleCount <= 0)
        return;

    int rows = (visibleCount + GRID_COLUMNS - 1) / GRID_COLUMNS;
    int itemWidth = (bounds.getWidth() - GRID_SPACING) / GRID_COLUMNS;
    int itemHeight = (bounds.getHeight() - (rows - 1) * GRID_SPACING) / rows;

    // Draw grid cell outlines for all slots
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));

    for (int i = 0; i < visibleCount; ++i) {
        int col = i % GRID_COLUMNS;
        int row = i / GRID_COLUMNS;
        int x = bounds.getX() + col * (itemWidth + GRID_SPACING);
        int y = bounds.getY() + row * (itemHeight + GRID_SPACING);

        auto cellBounds = juce::Rectangle<int>(x, y, itemWidth, itemHeight).toFloat();
        g.drawRoundedRectangle(cellBounds.reduced(0.5f), 3.0f, 1.0f);
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

}  // namespace magda::daw::ui
