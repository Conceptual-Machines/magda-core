// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ModelDownloadText.hpp
 * @brief How the Local LLM download reads in Settings and the footer (#1830).
 */

#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

#include "core/StringTable.hpp"
#include "magda/agents/model_downloader.hpp"

namespace magda {

/** @brief @p bytes in GB with one decimal, or whole MB below a gigabyte. */
inline juce::String downloadSizeText(juce::int64 bytes) {
    const auto mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0)
        return tr("model_download.size.gb").replace("{0}", juce::String(mb / 1024.0, 1));
    return tr("model_download.size.mb").replace("{0}", juce::String(juce::roundToInt(mb)));
}

inline juce::String downloadTimeLeftText(double seconds) {
    const auto minutes = static_cast<int>(std::ceil(seconds / 60.0));
    if (minutes <= 1)
        return tr("model_download.time.under_minute");
    if (minutes < 60)
        return tr("model_download.time.minutes").replace("{0}", juce::String(minutes));
    return tr("model_download.time.hours")
        .replace("{0}", juce::String(minutes / 60))
        .replace("{1}", juce::String(minutes % 60));
}

/**
 * @brief "1.9 GB of 4.7 GB, 6.1 MB/s, 9 min left", or "42%, …" when @p asPercent.
 *
 * The rate and time left are left out until the first second of the transfer has a rate.
 */
inline juce::String downloadProgressText(const ModelDownloadManager::Status& status,
                                         bool asPercent = false) {
    juce::String text;
    if (asPercent)
        text << (status.bytesTotal > 0 ? status.bytesDone * 100 / status.bytesTotal : 0) << "%";
    else
        text = tr("model_download.status.progress")
                   .replace("{0}", downloadSizeText(status.bytesDone))
                   .replace("{1}", downloadSizeText(status.bytesTotal));

    if (status.bytesPerSecond > 0.0) {
        const auto mbPerSecond = status.bytesPerSecond / (1024.0 * 1024.0);
        text << ", "
             << tr("model_download.status.rate")
                    .replace(
                        "{0}",
                        tr("model_download.size.mb").replace("{0}", juce::String(mbPerSecond, 1)));
    }

    if (const auto left = status.secondsLeft(); left >= 0.0)
        text << ", " << downloadTimeLeftText(left);

    return text;
}

inline int downloadCheckedPercent(const ModelDownloadManager::Status& status) {
    if (status.bytesTotal <= 0)
        return 0;
    return static_cast<int>(status.bytesChecked * 100 / status.bytesTotal);
}

inline juce::String downloadFailureText(ModelDownloadManager::Failure failure) {
    switch (failure) {
        case ModelDownloadManager::Failure::Unreachable:
            return tr("model_download.error.unreachable");
        case ModelDownloadManager::Failure::Disk:
            return tr("model_download.error.disk");
        case ModelDownloadManager::Failure::Damaged:
            return tr("model_download.error.damaged");
        case ModelDownloadManager::Failure::Move:
            return tr("model_download.error.move");
        case ModelDownloadManager::Failure::Network:
        case ModelDownloadManager::Failure::None:
            break;
    }
    return tr("model_download.error.network");
}

/** @brief The line Settings shows under the download button; empty while idle. */
inline juce::String downloadStatusText(const ModelDownloadManager::Status& status) {
    using Phase = ModelDownloadManager::Phase;
    switch (status.phase) {
        case Phase::Connecting:
            return trEllipsis("model_download.status.connecting");
        case Phase::Downloading:
            return downloadProgressText(status);
        case Phase::Stopping:
            return trEllipsis("model_download.status.stopping");
        case Phase::Paused:
            return tr("model_download.status.paused")
                .replace("{0}", downloadSizeText(status.bytesDone))
                .replace("{1}", downloadSizeText(status.bytesTotal));
        case Phase::Verifying:
            return tr("model_download.status.checking")
                .replace("{0}", juce::String(downloadCheckedPercent(status)));
        case Phase::Done:
            return tr("model_download.status.done");
        case Phase::Failed:
            return downloadFailureText(status.failure);
        case Phase::Idle:
            break;
    }
    return {};
}

}  // namespace magda
