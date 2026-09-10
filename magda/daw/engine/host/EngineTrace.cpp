#include "EngineTrace.hpp"

#include <cstdlib>
#include <iostream>

namespace magda::daw::engine_host {

namespace {

juce::String describe(const EngineTrace::Entry& entry) {
    juce::String line;
    line << "block " << juce::String(entry.block) << " beat " << juce::String(entry.beat, 4)
         << "  ";

    switch (entry.kind) {
        case EngineTrace::Kind::NoteOn:
            line << "note-on  " << entry.note << " vel " << entry.velocity << " at sample "
                 << entry.sample;
            break;
        case EngineTrace::Kind::NoteOff:
            line << "note-off " << entry.note << " at sample " << entry.sample;
            break;
        case EngineTrace::Kind::Publish:
            line << "clips published";
            break;
        case EngineTrace::Kind::Swap:
            line << "plan published";
            break;
    }

    return line;
}

}  // namespace

bool EngineTrace::enabled() {
    static const bool asked = [] {
        const auto* value = std::getenv("MAGDA_ENGINE_TRACE_MIDI");
        if (value == nullptr)
            return false;

        const auto flag = juce::String(value).trim().toLowerCase();
        return flag.isNotEmpty() && flag != "0" && flag != "false" && flag != "off";
    }();

    return asked;
}

void EngineTrace::print(const juce::String& line) {
    // Both destinations, because which one somebody is reading is not this
    // file's business: the app installs a FileLogger, so writeToLog alone
    // reaches magda.log and never the terminal, and stderr alone is gone when
    // the run ends.
    //
    // Only when a logger is installed, though. With none, JUCE's own fallback
    // is this same stderr, and every line would arrive twice.
    std::cerr << "[trace] " << line << std::endl;

    if (juce::Logger::getCurrentLogger() != nullptr)
        juce::Logger::writeToLog("[trace] " + line);
}

void EngineTrace::write(Entry entry) {
    const auto at = written_.load(std::memory_order_relaxed);

    if (at - read_ >= kCapacity) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    entries_[at % kCapacity] = entry;
    written_.store(at + 1, std::memory_order_release);
}

juce::StringArray EngineTrace::drain() {
    juce::StringArray lines;

    const auto upTo = written_.load(std::memory_order_acquire);
    for (; read_ < upTo; ++read_)
        lines.add(describe(entries_[read_ % kCapacity]));

    if (const auto lost = dropped_.exchange(0, std::memory_order_relaxed); lost > 0)
        lines.add(juce::String(lost) + " entries dropped");

    return lines;
}

void TracingDevice::process(magda::engine::DeviceBlock& block) {
    if (block.midiIn != nullptr)
        for (const auto entry : *block.midiIn) {
            const auto message = entry.getMessage();
            if (!message.isNoteOnOrOff())
                continue;

            trace_.write({
                .kind = message.isNoteOn() ? EngineTrace::Kind::NoteOn : EngineTrace::Kind::NoteOff,
                .note = message.getNoteNumber(),
                .velocity = message.getVelocity(),
                .sample = entry.samplePosition,
                .beat = block.block.beats.start,
                .block = blocks_,
            });
        }

    ++blocks_;
    device_->process(block);
}

}  // namespace magda::daw::engine_host
