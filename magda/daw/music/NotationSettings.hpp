#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace magda::music {

enum class NoteNaming { English, Solfege, Both };

/**
 * @brief App-wide note-name notation for chord display (C vs solfège Do).
 *
 * Shared by the chord-track lane blocks and the chord engine panel so a single
 * toggle flips both. Names are produced in English by the chord engine; this
 * converts the leading root (and any slash-bass) to the active notation for
 * display only.
 */
class NotationSettings : public juce::ChangeBroadcaster {
  public:
    static NotationSettings& getInstance() {
        static NotationSettings instance;
        return instance;
    }

    NoteNaming mode() const {
        return mode_;
    }
    void setMode(NoteNaming m) {
        if (mode_ != m) {
            mode_ = m;
            sendChangeMessage();
        }
    }
    void cycle() {
        setMode(static_cast<NoteNaming>((static_cast<int>(mode_) + 1) % 3));
    }

    juce::String label() const {
        switch (mode_) {
            case NoteNaming::English:
                return "C";
            case NoteNaming::Solfege:
                return "Do";
            case NoteNaming::Both:
                return "C/Do";
        }
        return "C";
    }

    // Format an English chord name (e.g. "C maj", "Am7", "F#m/A") for display.
    juce::String format(const juce::String& englishName) const {
        switch (mode_) {
            case NoteNaming::English:
                return englishName;
            case NoteNaming::Solfege:
                return toSolfege(englishName);
            case NoteNaming::Both:
                return englishName + "  " + toSolfege(englishName);
        }
        return englishName;
    }

    // For scale / key names, where doubling the whole label (Both) reads badly:
    // English mode keeps letters, Solfège and Both both show the solfège root.
    juce::String formatRoot(const juce::String& englishName) const {
        return mode_ == NoteNaming::English ? englishName : toSolfege(englishName);
    }

    // Replace note-letter roots (start of string and after '/') with solfège.
    static juce::String toSolfege(const juce::String& name) {
        auto rootFor = [](juce::juce_wchar c) -> const char* {
            switch (c) {
                case 'A':
                    return "La";
                case 'B':
                    return "Si";
                case 'C':
                    return "Do";
                case 'D':
                    return "Re";
                case 'E':
                    return "Mi";
                case 'F':
                    return "Fa";
                case 'G':
                    return "Sol";
                default:
                    return nullptr;
            }
        };

        juce::String out;
        bool atRoot = true;  // start of string is a root position
        for (int i = 0; i < name.length(); ++i) {
            const juce::juce_wchar c = name[i];
            if (atRoot) {
                atRoot = false;
                if (const char* sol = rootFor(c)) {
                    out += sol;
                    continue;
                }
            }
            out += c;
            if (c == '/')
                atRoot = true;  // a bass note follows the slash
        }
        return out;
    }

  private:
    NoteNaming mode_ = NoteNaming::English;
};

}  // namespace magda::music
