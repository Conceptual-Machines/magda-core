// Path-derived metadata: instrument family hint, keyword tags, filename-
// encoded key (issue #768).
//
// Producers encode rich semantics in folder structure and filenames:
// "/Vocals/Adlibs/MTVR_dry_C.wav" tells you the instrument family (vocal),
// some content tags (vocals, adlibs), and the key (C). These signals are
// often more reliable than CLAP audio inference on short percussive samples.
//
// Mirrors prototypes/media_db/src/media_db/derive.py — same vocabulary,
// same leaf-folder-first traversal, same flat-to-sharp key normalization.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace magda::media {

// Family inferred from path keywords ("vocal", "drum", "kick", "/Snares/"...).
// Walks path components LEAF-FIRST so the immediate parent folder dominates
// pack-name ancestors (e.g. ".../LAUT 'Drum & Bass Toolkit'/Snares/x.wav"
// resolves to 'drum' from the leaf "Snares", not 'bass' from "Drum & Bass").
//
// Returns std::nullopt when no keyword matches. Resolves symlinks before
// inspection so test corpora that flatten the original folder hierarchy
// still see the real /Vocals/ /Snares/ leaves on the canonical path.
std::optional<std::string> pathFamilyHint(const std::filesystem::path& path);

// All path keyword matches as (tag, confidence=1.0) pairs. Deduped, leaf-
// first occurrence order. Stored in media_tag with source_model='path' so
// the UI can show them alongside CLAP-derived tags and FTS picks them up.
std::vector<std::pair<std::string, float>> pathTags(const std::filesystem::path& path);

// Producer-encoded key parsed from the filename stem. Returns the root
// normalized to PITCH_CLASSES (sharps; "Bb" -> "A#", "Db" -> "C#") and
// the scale ("major" / "minor" / std::nullopt when only the root is given).
//
// Examples:
//   kick_C.wav         -> ("C",  nullopt)
//   synth_Cm.wav       -> ("C",  "minor")
//   synth_F#m.wav      -> ("F#", "minor")
//   synth_Bbmin.wav    -> ("A#", "minor")
//   pad_C_minor.wav    -> ("C",  "minor")    // split-token form
//   C_to_F.wav         -> ("F",  nullopt)    // last match wins
//
// Returns std::nullopt when no key marker is found. Robust against false
// matches like "guitar", "Modern_Trap_Vocals", "Animal_Sounds".
struct ParsedKey {
    std::string root;                  // "C", "C#", "D", ...
    std::optional<std::string> scale;  // "major", "minor", or nullopt
};
std::optional<ParsedKey> parseKeyFromPath(const std::filesystem::path& path);

// Producer-encoded BPM parsed from the filename: "kick_120bpm.wav",
// "120_BPM_loop.wav", "house_128.5BPM.wav". The "bpm" suffix is required —
// a bare number is too ambiguous (could be a file index, count, MIDI note).
//
// Examples:
//   kick_120bpm.wav          -> 120.0
//   house_125_BPM_loop.wav   -> 125.0
//   trap_85.5bpm.wav         -> 85.5
//   loop_92bpm_140bpm.wav    -> 140.0   // last match wins
//   kick_120.wav             -> nullopt // no "bpm" marker
//
// Sanity-clamped to [30, 300]; out-of-range matches are rejected.
std::optional<double> parseBpmFromPath(const std::filesystem::path& path);

}  // namespace magda::media
