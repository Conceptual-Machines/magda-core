#pragma once

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "ChordTypes.hpp"
#include "LRUCache.hpp"

namespace magda::music {

class ChordCache {
  public:
    struct CacheNote {
        int midiNote;
        int velocity;

        CacheNote(int note, int vel) : midiNote(note), velocity(vel) {}

        bool operator<(const CacheNote& other) const {
            if (midiNote != other.midiNote)
                return midiNote < other.midiNote;
            return velocity < other.velocity;
        }

        bool operator==(const CacheNote& other) const {
            return midiNote == other.midiNote && velocity == other.velocity;
        }
    };

    explicit ChordCache(size_t capacity = 1000) : cache(capacity) {}

    std::optional<Chord> get(const std::vector<CacheNote>& notes) {
        auto key = generateKey(notes);
        return cache.get(key);
    }

    void put(const std::vector<CacheNote>& notes, const Chord& chord) {
        auto key = generateKey(notes);
        cache.put(key, chord);
    }

    bool contains(const std::vector<CacheNote>& notes) const {
        auto key = generateKey(notes);
        return cache.contains(key);
    }

    void clear() {
        cache.clear();
    }
    size_t size() const {
        return cache.size();
    }

  private:
    LRUCache<std::string, Chord> cache;

    std::string generateKey(const std::vector<CacheNote>& notes) const {
        if (notes.empty())
            return "";
        std::vector<CacheNote> sortedNotes = notes;
        std::sort(sortedNotes.begin(), sortedNotes.end());
        std::ostringstream oss;
        for (size_t i = 0; i < sortedNotes.size(); ++i) {
            if (i > 0)
                oss << ",";
            oss << sortedNotes[i].midiNote << ":" << sortedNotes[i].velocity;
        }
        return oss.str();
    }
};

}  // namespace magda::music
