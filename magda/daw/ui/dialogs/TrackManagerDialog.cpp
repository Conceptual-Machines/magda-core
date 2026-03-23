#include "TrackManagerDialog.hpp"

#include "../themes/DarkTheme.hpp"

namespace magda {

// Column IDs
enum ColumnIds { TrackName = 1, LiveCol, ArrangeCol, MixCol };

// ============================================================================
// Content Component with TableListBox
// ============================================================================

class TrackManagerDialog::ContentComponent : public juce::Component,
                                             public juce::TableListBoxModel,
                                             public TrackManagerListener {
  public:
    ContentComponent() {
        // Table setup
        table_.setModel(this);
        table_.setColour(juce::ListBox::backgroundColourId,
                         DarkTheme::getColour(DarkTheme::SURFACE));
        table_.setRowHeight(26);
        table_.setHeaderHeight(28);

        auto& header = table_.getHeader();
        header.addColumn("Track", TrackName, 180, 100, 300,
                         juce::TableHeaderComponent::defaultFlags);
        header.addColumn("Live", LiveCol, 60, 50, 80, juce::TableHeaderComponent::defaultFlags);
        header.addColumn("Arrange", ArrangeCol, 60, 50, 80,
                         juce::TableHeaderComponent::defaultFlags);
        header.addColumn("Mix", MixCol, 60, 50, 80, juce::TableHeaderComponent::defaultFlags);

        // Style the header
        header.setColour(juce::TableHeaderComponent::backgroundColourId,
                         DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        header.setColour(juce::TableHeaderComponent::textColourId,
                         DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

        addAndMakeVisible(table_);

        // Info label
        infoLabel_.setText("Click checkboxes to toggle track visibility per view mode",
                           juce::dontSendNotification);
        infoLabel_.setColour(juce::Label::textColourId,
                             DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        infoLabel_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(infoLabel_);

        // Register listener
        TrackManager::getInstance().addListener(this);

        rebuildTrackList();
        setSize(500, 400);
    }

    ~ContentComponent() override {
        TrackManager::getInstance().removeListener(this);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(10);
        infoLabel_.setBounds(bounds.removeFromBottom(25));
        bounds.removeFromBottom(5);
        table_.setBounds(bounds);
    }

    // TrackManagerListener
    void tracksChanged() override {
        rebuildTrackList();
    }

    // TableListBoxModel implementation
    int getNumRows() override {
        return static_cast<int>(trackRows_.size());
    }

    void paintRowBackground(juce::Graphics& g, int rowNumber, int /*width*/, int /*height*/,
                            bool rowIsSelected) override {
        if (rowIsSelected) {
            g.fillAll(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
        } else if (rowNumber % 2 == 0) {
            g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE).darker(0.05f));
        }
    }

    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height,
                   bool /*rowIsSelected*/) override {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(trackRows_.size()))
            return;

        const auto& row = trackRows_[rowNumber];

        // Handle master track specially
        if (row.isMaster) {
            if (columnId == TrackName) {
                // Draw master track name with special styling
                g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
                g.drawText("Master", 5, 0, width - 10, height, juce::Justification::centredLeft);
            } else {
                // Draw checkbox for view mode columns
                ViewMode mode = columnIdToViewMode(columnId);
                const auto& master = TrackManager::getInstance().getMasterChannel();
                bool isVisible = master.isVisibleIn(mode);

                drawCheckbox(g, width, height, isVisible);
            }
            return;
        }

        // Regular track handling
        const auto* track = TrackManager::getInstance().getTrack(row.trackId);
        if (!track)
            return;

        if (columnId == TrackName) {
            // Draw track name with indentation for hierarchy
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            int indent = row.depth * 20;
            juce::String displayName = track->name;

            // Add group indicator
            if (track->isGroup()) {
                displayName = juce::String(juce::CharPointer_UTF8("\xe2\x96\xbc ")) + displayName;
            }

            g.drawText(displayName, indent + 5, 0, width - indent - 10, height,
                       juce::Justification::centredLeft);
        } else {
            // Draw checkbox for view mode columns
            ViewMode mode = columnIdToViewMode(columnId);
            bool isVisible = track->isVisibleIn(mode);

            drawCheckbox(g, width, height, isVisible);
        }
    }

    void drawCheckbox(juce::Graphics& g, int width, int height, bool isChecked) {
        auto checkBounds = juce::Rectangle<int>((width - 16) / 2, (height - 16) / 2, 16, 16);

        // Checkbox border
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(checkBounds, 1);

        // Checked state
        if (isChecked) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
            g.fillRect(checkBounds.reduced(3));

            // Draw checkmark
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            juce::Path checkPath;
            float cx = checkBounds.getCentreX();
            float cy = checkBounds.getCentreY();
            checkPath.startNewSubPath(cx - 4, cy);
            checkPath.lineTo(cx - 1, cy + 3);
            checkPath.lineTo(cx + 4, cy - 3);
            g.strokePath(checkPath, juce::PathStrokeType(2.0f));
        }
    }

    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent& /*e*/) override {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(trackRows_.size()))
            return;
        if (columnId == TrackName)
            return;  // Don't toggle on name column

        const auto& row = trackRows_[rowNumber];
        ViewMode mode = columnIdToViewMode(columnId);

        // Handle master track
        if (row.isMaster) {
            const auto& master = TrackManager::getInstance().getMasterChannel();
            bool currentlyVisible = master.isVisibleIn(mode);
            TrackManager::getInstance().setMasterVisible(mode, !currentlyVisible);
            table_.repaint();
            return;
        }

        // Regular track
        const auto* track = TrackManager::getInstance().getTrack(row.trackId);
        if (!track)
            return;

        bool currentlyVisible = track->isVisibleIn(mode);
        TrackManager::getInstance().setTrackVisible(row.trackId, mode, !currentlyVisible);

        table_.repaint();
    }

  private:
    struct TrackRow {
        TrackId trackId;
        int depth;
        bool isMaster = false;  // Special flag for master track
    };

    void rebuildTrackList() {
        trackRows_.clear();
        const auto& tracks = TrackManager::getInstance().getTracks();

        // Build hierarchical list - top level first, then children
        std::function<void(TrackId, int)> addTrackRecursive = [&](TrackId trackId, int depth) {
            const auto* track = TrackManager::getInstance().getTrack(trackId);
            if (!track)
                return;

            trackRows_.push_back({trackId, depth, false});

            // Add children if it's a group
            if (track->isGroup()) {
                for (auto childId : track->childIds) {
                    addTrackRecursive(childId, depth + 1);
                }
            }
        };

        // Start with top-level tracks
        for (const auto& track : tracks) {
            if (track.isTopLevel()) {
                addTrackRecursive(track.id, 0);
            }
        }

        // Add master track at the end
        trackRows_.push_back({INVALID_TRACK_ID, 0, true});

        table_.updateContent();
        table_.repaint();

        infoLabel_.setText("Click checkboxes to toggle track visibility per view mode",
                           juce::dontSendNotification);
    }

    static ViewMode columnIdToViewMode(int columnId) {
        switch (columnId) {
            case LiveCol:
                return ViewMode::Live;
            case ArrangeCol:
                return ViewMode::Arrange;
            case MixCol:
                return ViewMode::Mix;
            default:
                return ViewMode::Arrange;
        }
    }

    juce::TableListBox table_{"TrackTable", this};
    juce::Label infoLabel_;
    std::vector<TrackRow> trackRows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContentComponent)
};

// ============================================================================
// TrackManagerDialog
// ============================================================================

TrackManagerDialog::TrackManagerDialog()
    : DialogWindow("Track Manager", DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND), true) {
    content_ = std::make_unique<ContentComponent>();
    setContentOwned(content_.release(), true);
    centreWithSize(500, 400);
    setResizable(true, true);
    setUsingNativeTitleBar(true);

    TrackManager::getInstance().addListener(this);
}

TrackManagerDialog::~TrackManagerDialog() {
    TrackManager::getInstance().removeListener(this);
}

void TrackManagerDialog::closeButtonPressed() {
    setVisible(false);
}

void TrackManagerDialog::tracksChanged() {
    // Content component handles this via its own listener
}

void TrackManagerDialog::show() {
    auto* dialog = new TrackManagerDialog();
    dialog->setVisible(true);
    dialog->toFront(true);
}

}  // namespace magda
