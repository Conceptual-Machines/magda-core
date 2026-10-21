#include "plugins/SamplerZones.hpp"

#include <algorithm>
#include <map>

namespace magda::daw::audio {

namespace {

bool isWordChar(juce::juce_wchar c) {
    return juce::CharacterFunctions::isLetterOrDigit(c);
}

/// The note a name like `C3`, `f#2` or `Db-1` spells at @p at, or nothing.
std::optional<int> noteNameAt(const juce::String& text, int at) {
    static constexpr int kSemitones[] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
    const auto charAt = [&text](int i) {
        return i < text.length() ? text[i] : juce::juce_wchar{0};
    };
    const auto letter = juce::CharacterFunctions::toUpperCase(charAt(at));
    if (letter < 'A' || letter > 'G')
        return std::nullopt;

    int semitone = kSemitones[letter - 'A'];
    int i = at + 1;
    if (charAt(i) == '#') {
        ++semitone;
        ++i;
    } else if (charAt(i) == 'b' &&
               (juce::CharacterFunctions::isDigit(charAt(i + 1)) || charAt(i + 1) == '-')) {
        --semitone;
        ++i;
    }
    const bool negative = charAt(i) == '-';
    if (negative)
        ++i;
    const int digitsStart = i;
    while (juce::CharacterFunctions::isDigit(charAt(i)))
        ++i;
    if (i == digitsStart || i - digitsStart > 2 || isWordChar(charAt(i)))
        return std::nullopt;

    const int octave = text.substring(digitsStart, i).getIntValue() * (negative ? -1 : 1);
    const int note = (octave + 2) * 12 + semitone;
    if (note < 0 || note > 127)
        return std::nullopt;
    return note;
}

}  // namespace

std::optional<int> noteFromFileName(const juce::String& fileName) {
    std::optional<int> named;
    std::optional<int> numbered;
    for (int at = 0; at < fileName.length(); ++at) {
        if (at > 0 && isWordChar(fileName[at - 1]))
            continue;
        if (const auto note = noteNameAt(fileName, at)) {
            named = note;
            continue;
        }
        int end = at;
        while (end < fileName.length() && juce::CharacterFunctions::isDigit(fileName[end]))
            ++end;
        if (end > at && end - at <= 3 && (end == fileName.length() || !isWordChar(fileName[end]))) {
            const int value = fileName.substring(at, end).getIntValue();
            if (value <= 127)
                numbered = value;
        }
    }
    // A number beside a note name is more often a take or a layer than a key.
    return named ? named : numbered;
}

std::vector<MappedSample> mapSamplesByName(
    const juce::Array<juce::File>& files,
    const std::function<std::optional<int>(const juce::File&)>& metadataRoot) {
    std::map<int, std::vector<juce::File>> byNote;
    std::vector<juce::File> unplaced;
    for (const auto& file : files) {
        auto note = noteFromFileName(file.getFileNameWithoutExtension());
        if (!note && metadataRoot)
            note = metadataRoot(file);
        if (note)
            byNote[*note].push_back(file);
        else
            unplaced.push_back(file);
    }

    std::ranges::sort(unplaced, [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural(b.getFileName()) < 0;
    });
    int nextKey = byNote.empty() ? 60 : byNote.rbegin()->first + 1;
    for (const auto& file : unplaced)
        if (nextKey <= 127)
            byNote[nextKey++].push_back(file);

    std::vector<int> notes;
    for (const auto& [note, _] : byNote)
        notes.push_back(note);

    std::vector<MappedSample> mapped;
    for (size_t n = 0; n < notes.size(); ++n) {
        const int note = notes[n];
        const int lowKey = n == 0 ? 0 : notes[n - 1] + (note - notes[n - 1] - 1) / 2 + 1;
        const int highKey = n + 1 == notes.size() ? 127 : note + (notes[n + 1] - note - 1) / 2;

        auto layers = byNote[note];
        std::ranges::sort(layers, [](const juce::File& a, const juce::File& b) {
            return a.getFileName().compareNatural(b.getFileName()) < 0;
        });
        const int count = static_cast<int>(layers.size());
        for (int layer = 0; layer < count; ++layer) {
            MappedSample sample;
            sample.file = layers[static_cast<size_t>(layer)];
            sample.zone.source = sample.file.getFullPathName();
            sample.zone.rootNote = note;
            sample.zone.zones.keyLow = lowKey;
            sample.zone.zones.keyHigh = highKey;
            sample.zone.zones.velocityLow = 1 + layer * 127 / count;
            sample.zone.zones.velocityHigh = (layer + 1) * 127 / count;
            mapped.push_back(std::move(sample));
        }
    }
    return mapped;
}

}  // namespace magda::daw::audio
