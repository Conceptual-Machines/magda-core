// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CpuLoadWindow.hpp"

#include <algorithm>
#include <deque>
#include <set>
#include <vector>

#include "../../audio/io/AudioIOControl.hpp"
#include "../../core/ClipManager.hpp"
#include "../../core/SelectionManager.hpp"
#include "../../core/StringTable.hpp"
#include "../../core/TrackManager.hpp"
#include "../../engine/AudioEngine.hpp"
#include "../../engine/ProcessCpuMeter.hpp"
#include "../../engine/host/RenderLoadShares.hpp"
#include "../../engine/host/TracksBeingPlayed.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kHistoryWindows = 60;  // 30 s of CallbackLoad's half-second windows
constexpr int kRowHeight = 22;
constexpr int kGraphHeight = 140;
constexpr int kSummaryHeight = 44;

juce::String percent(double share) {
    return juce::String(juce::roundToInt(share * 100.0)) + "%";
}

/// Where most of a share was spent, as the list labels it.
juce::String whereOf(const LoadShare& share) {
    const auto total = share.total();
    if (total <= 0.0)
        return "-";
    if (share.callback > 0.05 * total && share.ahead > 0.05 * total)
        return tr("cpu_load.where_both");
    return share.callback >= share.ahead ? tr("cpu_load.where_callback")
                                         : tr("cpu_load.where_ahead");
}

/// @p share of one core as a share of the whole machine, the unit the transport reads in.
LoadShare ofMachine(const LoadShare& share) {
    const auto cores = static_cast<double>(std::max(juce::SystemStats::getNumCpus(), 1));
    return {.callback = share.callback / cores, .ahead = share.ahead / cores};
}

juce::Colour otherColour() {
    return ActiveTheme::getColour(ActiveTheme::TEXT_DIM);
}

juce::Colour callbackColour() {
    return ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION);
}

juce::Colour aheadColour() {
    return ActiveTheme::getColour(ActiveTheme::ACCENT_INFO);
}

/** @brief The tracks and, under each expanded one, its devices. */
class TrackLoadList : public juce::Component {
  public:
    struct Row {
        bool isDevice = false;
        TrackId track = INVALID_TRACK_ID;
        juce::String name;
        LoadShare share;
        juce::String detail;
    };

    void refresh(const RenderLoadShares& shares, const AudioEngine& engine) {
        const auto& manager = TrackManager::getInstance();
        const auto& tracks = manager.getTracks();
        const auto played = engine_host::tracksBeingPlayed(tracks, SelectionManager::getInstance(),
                                                           ClipManager::getInstance());
        rows_.clear();
        const auto add = [&](const TrackInfo& track) {
            const auto devices = loadDevicesOf(track);
            juce::StringArray detail;
            detail.add(juce::String(static_cast<int>(devices.size())) + " " +
                       tr("cpu_load.devices"));
            if (track.recordArmed)
                detail.add(tr("cpu_load.armed"));
            else if (track.monitorsInput())
                detail.add(tr("cpu_load.monitoring"));
            else if (played.contains(track.id))
                detail.add(tr("cpu_load.in_editor"));
            rows_.push_back({.track = track.id,
                             .name = track.name,
                             .share = ofMachine(shares.track(track.id)),
                             .detail = detail.joinIntoString(" \xc2\xb7 ")});
            if (!expanded_.contains(track.id))
                return;
            for (const auto& device : devices) {
                const auto latencyMs = engine.deviceLatencySeconds(device.path) * 1000.0;
                rows_.push_back(
                    {.isDevice = true,
                     .track = track.id,
                     .name = device.name,
                     .share = ofMachine(shares.device(track.id, device.segment, device.id)),
                     .detail = tr("cpu_load.latency") + " " + juce::String(latencyMs, 1) + " ms"});
            }
        };
        for (const auto& track : tracks)
            if (track.id != MASTER_TRACK_ID)
                add(track);
        if (const auto* master = manager.getTrack(MASTER_TRACK_ID))
            add(*master);

        setSize(getWidth(), static_cast<int>(rows_.size()) * kRowHeight);
        repaint();
    }

    std::function<void()> onExpandedChanged;

    void paint(juce::Graphics& g) override {
        auto& fonts = FontManager::getInstance();
        const auto width = getWidth();
        const auto nameWidth = width * 36 / 100;
        const auto barWidth = width * 24 / 100;
        const auto whereWidth = width * 14 / 100;

        for (std::size_t index = 0; index < rows_.size(); ++index) {
            const auto& row = rows_[index];
            auto area =
                juce::Rectangle<int>(0, static_cast<int>(index) * kRowHeight, width, kRowHeight);
            if (!row.isDevice && index > 0) {
                g.setColour(ActiveTheme::getColour(ActiveTheme::SEPARATOR));
                g.drawHorizontalLine(area.getY(), 0.0f, static_cast<float>(width));
            }
            area = area.reduced(6, 0);

            auto name = area.removeFromLeft(nameWidth);
            g.setFont(fonts.getUIFont(12.0f));
            g.setColour(ActiveTheme::getColour(row.isDevice ? ActiveTheme::TEXT_SECONDARY
                                                            : ActiveTheme::TEXT_PRIMARY));
            if (row.isDevice) {
                name.removeFromLeft(18);
            } else {
                const auto arrow = name.removeFromLeft(14).toFloat();
                juce::Path triangle;
                triangle.addTriangle(0.0f, 0.0f, 6.0f, 3.5f, 0.0f, 7.0f);
                if (expanded_.contains(row.track))
                    triangle.applyTransform(juce::AffineTransform::rotation(
                        juce::MathConstants<float>::halfPi, 3.0f, 3.5f));
                triangle.applyTransform(juce::AffineTransform::translation(
                    arrow.getX() + 2.0f, arrow.getCentreY() - 3.5f));
                g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
                g.fillPath(triangle);
                g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
            }
            g.drawText(row.name, name, juce::Justification::centredLeft, true);

            auto bar = area.removeFromLeft(barWidth).reduced(0, 6);
            const auto label = bar.removeFromRight(40);
            g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE));
            g.fillRect(bar);
            const auto scale = static_cast<double>(bar.getWidth());
            const auto callbackWidth = juce::roundToInt(std::min(row.share.callback, 1.0) * scale);
            const auto aheadWidth = juce::roundToInt(
                std::min(row.share.ahead, 1.0 - std::min(row.share.callback, 1.0)) * scale);
            g.setColour(callbackColour());
            g.fillRect(bar.withWidth(callbackWidth));
            g.setColour(aheadColour());
            g.fillRect(bar.withX(bar.getX() + callbackWidth).withWidth(aheadWidth));
            g.setFont(fonts.getMonoFont(11.0f));
            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
            g.drawText(percent(row.share.total()), label, juce::Justification::centredRight);

            g.setFont(fonts.getUIFont(11.0f));
            g.drawText(whereOf(row.share), area.removeFromLeft(whereWidth).withTrimmedLeft(10),
                       juce::Justification::centredLeft);
            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
            g.drawText(row.detail, area, juce::Justification::centredLeft, true);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override {
        const auto index = static_cast<std::size_t>(event.y / kRowHeight);
        if (index >= rows_.size() || rows_[index].isDevice)
            return;
        const auto track = rows_[index].track;
        if (!expanded_.erase(track))
            expanded_.insert(track);
        if (onExpandedChanged)
            onExpandedChanged();
    }

  private:
    std::vector<Row> rows_;
    std::set<TrackId> expanded_;
};

/** @brief The graph, the summary and the list, refreshed from the engine. */
class CpuLoadView : public juce::Component, private juce::Timer {
  public:
    explicit CpuLoadView(AudioEngine& engine) : engine_(engine) {
        list_.onExpandedChanged = [this] { refreshList(); };
        viewport_.setViewedComponent(&list_, false);
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);
        setSize(640, 520);
        startTimerHz(10);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(ActiveTheme::getColour(ActiveTheme::BACKGROUND));
        paintSummary(g);
        paintGraph(g);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(10);
        summary_ = area.removeFromTop(kSummaryHeight);
        graph_ = area.removeFromTop(kGraphHeight);
        area.removeFromTop(10);
        viewport_.setBounds(area);
        list_.setSize(viewport_.getMaximumVisibleWidth(), list_.getHeight());
    }

  private:
    void timerCallback() override {
        auto* io = engine_.getAudioIO();
        if (!isShowing())
            return;
        if (io == nullptr)
            return;
        const auto window = io->status().cpuWindow;
        if (window.index == lastWindow_)
            return;
        lastWindow_ = window.index;

        Sample sample{.cpu = cpu_.sample()};
        if (const auto* load = engine_.renderLoad(); load != nullptr && shares_.update(*load)) {
            sample.render = ofMachine(shares_.all());
            refreshList();
        }
        history_.push_back(sample);
        while (history_.size() > kHistoryWindows)
            history_.pop_front();
        repaint(summary_.getUnion(graph_));
    }

    void refreshList() {
        list_.setSize(viewport_.getMaximumVisibleWidth(), list_.getHeight());
        list_.refresh(shares_, engine_);
    }

    void paintSummary(juce::Graphics& g) const {
        auto& fonts = FontManager::getInstance();
        const auto latest = history_.empty() ? Sample{} : history_.back();

        auto area = summary_;
        auto top = area.removeFromTop(24);
        g.setFont(fonts.getUIFontBold(16.0f));
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        g.drawText(tr("transport.cpu.cpu") + " " + percent(latest.cpu), top,
                   juce::Justification::centredLeft);

        g.setFont(fonts.getUIFont(12.0f));
        const auto item = [&](juce::Colour colour, const juce::String& text) {
            auto slot = area.removeFromLeft(
                juce::GlyphArrangement::getStringWidthInt(fonts.getUIFont(12.0f), text) + 30);
            g.setColour(colour);
            g.fillRect(slot.removeFromLeft(12).withSizeKeepingCentre(10, 3));
            slot.removeFromLeft(6);
            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
            g.drawText(text, slot, juce::Justification::centredLeft);
        };
        item(callbackColour(), tr("cpu_load.callback") + " " + percent(latest.render.callback));
        item(aheadColour(), tr("cpu_load.ahead") + " " + percent(latest.render.ahead));
        item(otherColour(), tr("cpu_load.other") + " " + percent(latest.other()));
    }

    void paintGraph(juce::Graphics& g) const {
        const auto area = graph_.toFloat();
        g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE));
        g.fillRect(area);
        g.setColour(ActiveTheme::getColour(ActiveTheme::GRID_LINE));
        for (const auto level : {0.25f, 0.5f, 0.75f})
            g.drawHorizontalLine(juce::roundToInt(area.getBottom() - area.getHeight() * level),
                                 area.getX(), area.getRight());

        const auto y = [&](double share) {
            return area.getBottom() -
                   area.getHeight() * static_cast<float>(std::clamp(share, 0.0, 1.0));
        };
        const auto x = [&](std::size_t index) {
            const auto offset = static_cast<std::size_t>(kHistoryWindows) - history_.size();
            return area.getX() + area.getWidth() * static_cast<float>(index + offset) /
                                     static_cast<float>(kHistoryWindows - 1);
        };

        const auto line = [&](juce::Colour colour, auto share) {
            if (history_.size() < 2)
                return;
            juce::Path path;
            for (std::size_t index = 0; index < history_.size(); ++index) {
                const juce::Point<float> point{x(index), y(share(history_[index]))};
                if (index == 0)
                    path.startNewSubPath(point);
                else
                    path.lineTo(point);
            }
            g.setColour(colour);
            g.strokePath(path, juce::PathStrokeType(1.5f));
        };
        // The headline's figure, in its colour, over the two the render spent.
        line(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY),
             [](const Sample& sample) { return sample.cpu; });
        line(aheadColour(), [](const Sample& sample) { return sample.render.ahead; });
        line(callbackColour(), [](const Sample& sample) { return sample.render.callback; });

        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText("100%", graph_.reduced(4, 2), juce::Justification::topLeft);
        g.drawText(tr("cpu_load.last_30s"), graph_.reduced(4, 2), juce::Justification::bottomRight);
    }

    /** @brief One half-second window, every figure a share of the machine's CPU. */
    struct Sample {
        double cpu = 0.0;
        LoadShare render;

        /// What MAGDA spent outside the render: the UI, and waiting between blocks.
        double other() const {
            return std::max(cpu - render.total(), 0.0);
        }
    };

    AudioEngine& engine_;
    std::deque<Sample> history_;
    std::uint64_t lastWindow_ = 0;
    RenderLoadShares shares_;
    ProcessCpuMeter cpu_;
    juce::Rectangle<int> summary_, graph_;
    juce::Viewport viewport_;
    TrackLoadList list_;
};

}  // namespace

CpuLoadWindow::CpuLoadWindow(AudioEngine& engine) : FloatingHostWindow(tr("cpu_load.title")) {
    setContentOwned(new CpuLoadView(engine), true);
    setResizable(true, true);
    setResizeLimits(440, 320, 2000, 2000);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
    floatOverMainWindow(*this);
}

CpuLoadWindow::~CpuLoadWindow() = default;

void CpuLoadWindow::closeButtonPressed() {
    setVisible(false);
}

}  // namespace magda::daw::ui
