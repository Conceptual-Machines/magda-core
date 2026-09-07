#pragma once

#include <juce_core/juce_core.h>

#include "../../core/AppPaths.hpp"

namespace magda {

inline void logAdsrAudio(const juce::String& message) {
    const auto line =
        juce::Time::getCurrentTime().toString(true, true, true, true) + " [ADSR-AUDIO] " + message;
    DBG(line);
    juce::Logger::writeToLog(line);

    static juce::CriticalSection fileLock;
    const juce::ScopedLock guard(fileLock);

    auto appendLine = [](const juce::File& logFile, const juce::String& text) {
        logFile.getParentDirectory().createDirectory();
        if (!logFile.appendText(text + "\n", false, false, "\n")) {
            const auto failureLine = "[ADSR-AUDIO] failed to append " + logFile.getFullPathName();
            DBG(failureLine);
            juce::Logger::writeToLog(failureLine);
        }
    };

    appendLine(paths::logsDir().getChildFile("adsr-audio-trigger.log"), line);
    appendLine(juce::File("/tmp/magda-adsr-audio-trigger.log"), line);
    appendLine(juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("magda-adsr-audio-trigger.log"),
               line);
}

}  // namespace magda

#define MAGDA_ADSR_AUDIO_LOG(expr)                                                                 \
    do {                                                                                           \
        juce::String magdaAdsrAudioLogMessage;                                                     \
        magdaAdsrAudioLogMessage << expr;                                                          \
        ::magda::logAdsrAudio(magdaAdsrAudioLogMessage);                                           \
    } while (false)
