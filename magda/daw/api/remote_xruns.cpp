// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "remote_xruns.hpp"

#include <algorithm>

namespace magda::remote {
namespace {

juce::var sinceValue(int since) {
    return since >= 0 ? juce::var(since) : juce::var();
}

juce::var callbackValue(const XrunLog::Callback& callback) {
    auto* entry = new juce::DynamicObject();
    entry->setProperty("atMs", callback.atMs);
    entry->setProperty("renderMs", callback.renderMs);
    entry->setProperty("lengthMs", callback.lengthMs);
    entry->setProperty("sincePreviousMs", callback.sincePreviousMs);
    entry->setProperty("writerWaitMs", callback.writerWaitMs);
    entry->setProperty("blocks", static_cast<int>(callback.blocks));
    entry->setProperty("wholeBlocks", static_cast<int>(callback.wholeBlocks));
    entry->setProperty("missedBlocks", static_cast<int>(callback.missedBlocks));
    entry->setProperty("started", callback.started);
    entry->setProperty("jumped", callback.jumped);
    entry->setProperty("published", callback.published);
    return entry;
}

}  // namespace

std::vector<juce::var> XrunProblems::read(const std::vector<XrunLog::Event>& events, int risen,
                                          juce::int64 nowMs) {
    std::vector<juce::var> problems;
    for (const auto& event : events) {
        problems.push_back(xrunProblem(event));
        logged_ += event.count;
    }

    // What the previous read owed and the log has still not explained.
    const auto settled = std::min(logged_, owed_);
    logged_ -= settled;
    owed_ -= settled;
    if (owed_ > 0)
        problems.push_back(plainProblem("audio_xrun", owedAtMs_, static_cast<int>(owed_)));

    const auto explained = std::min<juce::int64>(logged_, std::max(risen, 0));
    logged_ -= explained;
    owed_ = std::max(risen, 0) - explained;
    owedAtMs_ = nowMs;
    return problems;
}

void XrunProblems::reset() {
    logged_ = owed_ = owedAtMs_ = 0;
}

const char* likelyCause(const XrunLog::Event& event) {
    for (int i = event.numCallbacks - 1; i >= 0; --i) {
        const auto& callback = event.callbacks[static_cast<std::size_t>(i)];
        const auto over = callback.renderMs - callback.lengthMs;
        if (callback.lengthMs <= 0.0f || over <= 0.0f)
            continue;
        return callback.writerWaitMs >= over ? "writerWait" : "overran";
    }
    return "outsideRender";
}

juce::var xrunProblem(const XrunLog::Event& event) {
    auto problem = plainProblem("audio_xrun", event.atMs, event.count);
    auto* obj = problem.getDynamicObject();
    juce::Array<juce::var> callbacks;
    for (int i = 0; i < event.numCallbacks; ++i)
        callbacks.add(callbackValue(event.callbacks[static_cast<std::size_t>(i)]));
    obj->setProperty("callbacks", callbacks);
    obj->setProperty("likelyCause", likelyCause(event));
    obj->setProperty("sinceStart", sinceValue(event.sinceStart));
    obj->setProperty("sinceJump", sinceValue(event.sinceJump));
    obj->setProperty("sincePublish", sinceValue(event.sincePublish));
    return problem;
}

juce::var plainProblem(const char* code, juce::int64 atMs, int count) {
    auto* entry = new juce::DynamicObject();
    entry->setProperty("code", code);
    entry->setProperty("atMs", atMs);
    entry->setProperty("count", count);
    return entry;
}

}  // namespace magda::remote
