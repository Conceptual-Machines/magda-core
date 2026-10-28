// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compiled/DrumHitPreview.hpp"

#include "plugins/compiled/MagdaCompiledPolyInstrument.hpp"

namespace magda::daw::ui {

DrumHitPreview::DrumHitPreview() : juce::Thread("DrumHitPreview") {
    startThread(juce::Thread::Priority::low);
}

DrumHitPreview::~DrumHitPreview() {
    cancelPendingUpdate();
    signalThreadShouldExit();
    notify();
    stopThread(2000);
}

void DrumHitPreview::request(std::shared_ptr<Instrument> device, std::vector<float> slotValues,
                             float lengthMs) {
    if (device == nullptr)
        return;
    {
        const std::scoped_lock lock(mutex_);
        pending_ = Job{std::move(device), std::move(slotValues), lengthMs};
    }
    notify();
}

void DrumHitPreview::run() {
    while (!threadShouldExit()) {
        std::optional<Job> job;
        {
            const std::scoped_lock lock(mutex_);
            job.swap(pending_);
        }
        if (!job.has_value()) {
            wait(-1);
            continue;
        }

        const auto samples = static_cast<int>(job->device->previewSampleRate() *
                                              static_cast<double>(job->lengthMs) / 1000.0);
        Hit hit{job->device->renderPreviewHit(job->slotValues, samples), job->lengthMs};
        {
            const std::scoped_lock lock(mutex_);
            finished_ = std::move(hit);
        }
        triggerAsyncUpdate();
    }
}

void DrumHitPreview::handleAsyncUpdate() {
    std::optional<Hit> hit;
    {
        const std::scoped_lock lock(mutex_);
        hit.swap(finished_);
    }
    if (hit.has_value() && onRendered)
        onRendered(*hit);
}

}  // namespace magda::daw::ui
