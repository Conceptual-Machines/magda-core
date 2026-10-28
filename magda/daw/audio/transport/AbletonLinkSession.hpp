// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_events/juce_events.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>

#include "../../core/Config.hpp"
#include "../../project/ProjectManager.hpp"

/**
 * @file AbletonLinkSession.hpp
 * @brief MAGDA's place in an Ableton Link session (#929).
 */

namespace magda {

/**
 * @brief Joins or leaves the Link session as Config says, and keeps the project tempo and the
 *        session tempo the same.
 *
 * Message thread. While on, tempo automation is ignored: every peer plays one tempo. The rules
 * for who wins a tempo follow Ableton's TEST-PLAN.md: a session being joined, or a project
 * being loaded into one, keeps the session's tempo.
 */
class AbletonLinkSession : private ConfigListener,
                           private ProjectManagerListener,
                           private juce::AsyncUpdater {
  public:
    struct Hooks {
        /// Set the project tempo as an edit would.
        std::function<void(double)> setProjectTempo;
        /// Whether tempo automation drives the engine's tempo.
        std::function<void(bool)> followTempoLane;
    };

    explicit AbletonLinkSession(Hooks hooks);
    ~AbletonLinkSession() override;

    AbletonLinkSession(const AbletonLinkSession&) = delete;
    AbletonLinkSession& operator=(const AbletonLinkSession&) = delete;

    bool isEnabled() const {
        return enabled_;
    }
    /** @brief Other apps in the session, as last reported on the message thread. */
    std::size_t numPeers() const {
        return shownPeers_;
    }

    /// Called when Link is switched on or off, or the peer count changes.
    std::function<void()> onStateChanged;

  private:
    void configChanged() override;
    void projectTeardown() override;
    void projectOpened(const ProjectInfo& info) override;
    void projectPropertiesChanged() override;
    void handleAsyncUpdate() override;

    void applyEnabled(bool enabled);
    void sendProjectTempo();
    void takeSessionTempo();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    Hooks hooks_;

    bool enabled_ = false;
    /// Between a project's teardown and its replacement opening, whose tempo is not an edit.
    bool replacingProject_ = false;
    /// Written on Link's thread.
    std::atomic<std::size_t> peers_{0};
    std::size_t shownPeers_ = 0;
};

}  // namespace magda
