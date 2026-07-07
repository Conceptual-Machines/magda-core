#include "TrackControlsLayout.hpp"

#include <algorithm>

namespace magda::track_controls {

namespace {

void placeCell(juce::Component* comp, juce::Rectangle<int> cell, int cellH) {
    if (!comp)
        return;
    const int h = cellH > 0 ? std::min(cellH, cell.getHeight()) : cell.getHeight();
    comp->setBounds(cell.withSizeKeepingCentre(cell.getWidth(), h));
    comp->setVisible(true);
}

}  // namespace

int fixedClusterWidth(const MixControls& c, const Metrics& m) {
    int w = 0;
    if (c.pan)
        w += m.gap + m.cellW;
    w += static_cast<int>(c.buttons.size()) * (m.gap + m.buttonW);
    if (c.trailing)
        w += m.gap + m.cellW;
    return w;
}

bool fitsOneRow(int rowWidth, const MixControls& c, const Metrics& m) {
    return rowWidth - fixedClusterWidth(c, m) >= m.minGainW;
}

void layoutGainRow(juce::Rectangle<int> row, const MixControls& c, const Metrics& m) {
    if (c.pan) {
        placeCell(c.pan, row.removeFromRight(m.cellW), c.panCellH);
        row.removeFromRight(m.gap);
    }
    if (c.gain) {
        c.gain->setBounds(row);
        c.gain->setVisible(true);
    }
}

void layoutButtonRow(juce::Rectangle<int> row, const MixControls& c, const Metrics& m) {
    if (c.trailing) {
        placeCell(c.trailing, row.removeFromRight(m.cellW), m.buttonH);
        row.removeFromRight(m.gap);
    }
    const int n = static_cast<int>(c.buttons.size());
    if (n == 0)
        return;
    // Shrink the buttons rather than clip the last one on a narrow row.
    int btnW = m.buttonW;
    if (n * btnW + (n - 1) * m.gap > row.getWidth())
        btnW = std::max(12, (row.getWidth() - (n - 1) * m.gap) / n);
    for (auto* b : c.buttons) {
        placeCell(b, row.removeFromLeft(btnW), m.buttonH);
        row.removeFromLeft(m.gap);
    }
}

void layoutSingleRow(juce::Rectangle<int> row, const MixControls& c, const Metrics& m) {
    if (c.gain) {
        const int gainW = std::max(m.minGainW, row.getWidth() - fixedClusterWidth(c, m));
        c.gain->setBounds(row.removeFromLeft(gainW));
        c.gain->setVisible(true);
    }
    if (c.pan) {
        row.removeFromLeft(m.gap);
        placeCell(c.pan, row.removeFromLeft(m.cellW), c.panCellH);
    }
    for (auto* b : c.buttons) {
        row.removeFromLeft(m.gap);
        placeCell(b, row.removeFromLeft(m.buttonW), m.buttonH);
    }
    if (c.trailing) {
        row.removeFromLeft(m.gap);
        placeCell(c.trailing, row.removeFromLeft(m.cellW), m.buttonH);
    }
}

int layoutMixControls(juce::Rectangle<int>& area, const MixControls& c, const Metrics& m) {
    const bool clusterOnly = c.buttons.empty() && !c.trailing;
    auto row = area.removeFromTop(m.rowH);
    if (clusterOnly || fitsOneRow(row.getWidth(), c, m)) {
        layoutSingleRow(row, c, m);
        return m.rowH;
    }
    layoutGainRow(row, c, m);
    area.removeFromTop(m.rowGap);
    layoutButtonRow(area.removeFromTop(m.rowH), c, m);
    return 2 * m.rowH + m.rowGap;
}

int routingDropdownWidth(int rowWidth, int numDropdowns, const Metrics& m) {
    const int avail = rowWidth - m.iconW - m.ddGap;
    if (numDropdowns <= 1)
        return std::max(0, avail);
    return std::max(0, (avail - m.ddGap) / 2);
}

void layoutRoutingRow(juce::Rectangle<int> row, juce::Component* dd1, juce::Component* dd2,
                      juce::Component* icon, const Metrics& m) {
    if (icon) {
        icon->setBounds(row.removeFromRight(m.iconW));
        icon->setVisible(true);
        row.removeFromRight(m.ddGap);
    }
    if (dd1 && dd2) {
        dd1->setBounds(row.removeFromLeft((row.getWidth() - m.ddGap) / 2));
        dd1->setVisible(true);
        row.removeFromLeft(m.ddGap);
        dd2->setBounds(row);
        dd2->setVisible(true);
    } else if (auto* dd = dd1 ? dd1 : dd2) {
        dd->setBounds(row);
        dd->setVisible(true);
    }
}

}  // namespace magda::track_controls
