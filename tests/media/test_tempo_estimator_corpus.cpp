// Measures the BPM tier against a real sample library (#2674). Hidden: it
// reads whatever library this machine has and is a measurement, not a pass/fail
// assertion. Run it with
//
//   ./magda_tests "[.tempo-corpus]" --success
//
// Ground truth is the tempo in the filename, which most sample packs put there
// -- `MNT_RDS_80_synth_pulse_...` -- and not what the media DB holds. On this
// library the DB disagrees with the filename on 1542 of 1798 files that carry
// both, because the ACID chunks it read them from are wrong (80 against 160,
// 100 against 50). Only files with exactly one plausible token are counted, so
// an index in the name cannot be mistaken for a tempo.

#include <juce_audio_formats/juce_audio_formats.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "../../magda/daw/media_db/AudioFeatures.hpp"
#include "../../magda/daw/media_db/BeatTracker.hpp"
#include "../../magda/daw/media_db/MediaDbContext.hpp"
#include "../../magda/daw/media_db/TempoEstimator.hpp"

namespace {

struct Labelled {
    std::string path;
    double bpm = 0.0;
};

/// The tempo a sample pack put in the filename: a two or three digit token,
/// not part of a longer run of digits, in a range a tempo can be. Returns
/// nothing when the name has none or has more than one, because then it is a
/// guess about which number means what.
std::optional<double> tempoInName(const std::string& path) {
    const auto stem = std::filesystem::path(path).stem().string();
    std::optional<double> found;
    for (std::size_t i = 0; i < stem.size();) {
        if (std::isdigit(static_cast<unsigned char>(stem[i])) == 0) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < stem.size() && std::isdigit(static_cast<unsigned char>(stem[end])) != 0) {
            ++end;
        }
        const auto digits = end - i;
        if (digits == 2 || digits == 3) {
            const double value = std::stod(stem.substr(i, digits));
            if (value >= 60.0 && value <= 200.0) {
                if (found) {
                    return std::nullopt;  // two candidates, so neither is evidence
                }
                found = value;
            }
        }
        i = end;
    }
    return found;
}

std::vector<Labelled> query(const std::string& sql) {
    std::vector<Labelled> rows;
    sqlite3* db = nullptr;
    const auto path = magda::media::MediaDbContext::dbPath().string();
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        return rows;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Labelled row;
            row.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (sqlite3_column_count(stmt) > 1) {
                row.bpm = sqlite3_column_double(stmt, 1);
            }
            rows.push_back(std::move(row));
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return rows;
}

std::vector<Labelled> sample(std::vector<Labelled> rows, std::size_t count) {
    std::mt19937 rng(20260915);
    std::shuffle(rows.begin(), rows.end(), rng);
    if (rows.size() > count) {
        rows.resize(count);
    }
    return rows;
}

bool within(double a, double b, double tolerance) {
    return b > 0.0 && std::abs(a - b) / b <= tolerance;
}

}  // namespace

TEST_CASE("Tempo estimator against the library", "[.tempo-corpus]") {
    constexpr std::size_t kSampleSize = 300;
    constexpr double kTolerance = 0.02;

    auto named = query("SELECT path FROM media_file WHERE kind='audio' AND shape!='one-shot'");
    std::vector<Labelled> labelled;
    std::vector<Labelled> unnamed;
    for (auto& row : named) {
        if (const auto tempo = tempoInName(row.path)) {
            row.bpm = *tempo;
            labelled.push_back(row);
        } else {
            unnamed.push_back(row);
        }
    }
    labelled = sample(std::move(labelled), kSampleSize);
    std::printf("\n--- %zu files whose tempo is in the filename ---\n", labelled.size());

    int agree = 0;
    int octave = 0;
    int other = 0;
    int abstained = 0;
    std::vector<std::string> disagreements;

    for (const auto& row : labelled) {
        const auto measured = magda::media::measureTempo(row.path);
        if (!measured || measured->confidence < 0.35) {
            ++abstained;
            continue;
        }
        if (within(measured->bpm, row.bpm, kTolerance)) {
            ++agree;
        } else if (within(measured->bpm, row.bpm * 2.0, kTolerance) ||
                   within(measured->bpm, row.bpm / 2.0, kTolerance)) {
            ++octave;
            if (disagreements.size() < 12) {
                disagreements.push_back("x2/x0.5  db=" + std::to_string(row.bpm) +
                                        " measured=" + std::to_string(measured->bpm) + "  " +
                                        std::filesystem::path(row.path).filename().string());
            }
        } else {
            ++other;
            if (disagreements.size() < 12) {
                disagreements.push_back("other    db=" + std::to_string(row.bpm) +
                                        " measured=" + std::to_string(measured->bpm) + "  " +
                                        std::filesystem::path(row.path).filename().string());
            }
        }
    }

    const auto answered = agree + octave + other;
    std::printf("  agree (+/-2%%)   : %d\n", agree);
    std::printf("  half or double  : %d\n", octave);
    std::printf("  other mismatch  : %d\n", other);
    std::printf("  abstained       : %d\n", abstained);
    if (answered > 0) {
        std::printf("  agreement where it answered: %.1f%%\n", 100.0 * agree / answered);
    }
    for (const auto& line : disagreements) {
        std::printf("    %s\n", line.c_str());
    }

    auto unlabelled = sample(std::move(unnamed), kSampleSize);
    std::printf("\n--- %zu files with no tempo in the name ---\n", unlabelled.size());

    int measuredCount = 0;
    int wholeBars = 0;
    int silent = 0;
    for (const auto& row : unlabelled) {
        const auto measured = magda::media::measureTempo(row.path);
        if (measured && measured->confidence >= 0.35) {
            ++measuredCount;
            if (measured->wholeBars) {
                ++wholeBars;
            }
        } else {
            ++silent;
        }
    }
    std::printf("  measured        : %d\n", measuredCount);
    std::printf("  of those, the file is a whole number of bars at it: %d\n", wholeBars);
    std::printf("  abstained       : %d\n", silent);

    SUCCEED();
}

// ---------------------------------------------------------------------------

TEST_CASE("Beat tracker against the library", "[.beat-corpus]") {
    if (!magda::media::BeatTracker::isAvailable()) {
        WARN("no beat model at " + magda::media::BeatTracker::defaultModelPath().string());
        return;
    }
    magda::media::BeatTracker tracker(magda::media::BeatTracker::defaultModelPath());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    auto named = query("SELECT path FROM media_file WHERE kind='audio' AND shape!='one-shot'");
    std::vector<Labelled> labelled;
    for (auto& row : named) {
        if (const auto tempo = tempoInName(row.path)) {
            row.bpm = *tempo;
            labelled.push_back(row);
        }
    }
    labelled = sample(std::move(labelled), 200);
    const auto reportPath = std::filesystem::temp_directory_path() / "magda_beat_tracker.csv";
    std::FILE* csv = std::fopen(reportPath.c_str(), "w");
    if (csv != nullptr) {
        std::fprintf(csv, "verdict,name_bpm,tracked_bpm,ratio,steadiness,beats,downbeats,"
                          "beats_per_bar,duration_s,file\n");
    }
    std::printf("\n--- beat tracker on %zu files whose tempo is in the filename ---\n",
                labelled.size());
    std::printf("    per-file report: %s\n", reportPath.c_str());

    int agree = 0;
    int octave = 0;
    int other = 0;
    int silent = 0;

    // How well it does when it is asked to be sure: a wrong tempo seeds a clip
    // and stretches it, a missing one leaves a field blank.
    static constexpr std::array<double, 4> kSteadinessBands = {0.0, 0.5, 0.8, 0.95};
    std::array<int, 4> answered{};
    std::array<int, 4> correct{};
    std::array<int, 4> correctOctave{};

    for (const auto& row : labelled) {
        std::unique_ptr<juce::AudioFormatReader> reader(
            formats.createReaderFor(juce::File(juce::String(row.path))));
        if (reader == nullptr || reader->lengthInSamples <= 0) {
            ++silent;
            continue;
        }
        juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                        static_cast<int>(reader->lengthInSamples));
        reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
        if (buffer.getNumChannels() > 1) {
            buffer.addFrom(0, 0, buffer, 1, 0, buffer.getNumSamples());
            buffer.applyGain(0, 0, buffer.getNumSamples(), 0.5F);
        }

        const auto tracked =
            tracker.track(buffer.getReadPointer(0), buffer.getNumSamples(), reader->sampleRate);
        const double durationS = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
        if (!tracked || tracked->bpm <= 0.0) {
            ++silent;
            if (csv != nullptr) {
                std::fprintf(csv, "no-beats,%g,,,,,,,%.3f,\"%s\"\n", row.bpm, durationS,
                             row.path.c_str());
            }
            continue;
        }
        const bool hit = within(tracked->bpm, row.bpm, 0.02);
        const bool octaveHit =
            within(tracked->bpm, row.bpm * 2.0, 0.02) || within(tracked->bpm, row.bpm / 2.0, 0.02);
        for (std::size_t band = 0; band < kSteadinessBands.size(); ++band) {
            if (tracked->steadiness >= kSteadinessBands[band]) {
                ++answered[band];
                if (hit) {
                    ++correct[band];
                }
                if (hit || octaveHit) {
                    ++correctOctave[band];
                }
            }
        }
        if (hit) {
            ++agree;
        } else if (octaveHit) {
            ++octave;
        } else {
            ++other;
        }

        if (csv != nullptr) {
            std::fprintf(csv, "%s,%g,%.2f,%.3f,%.3f,%zu,%zu,%d,%.3f,\"%s\"\n",
                         hit ? "exact" : (octaveHit ? "octave" : "wrong"), row.bpm, tracked->bpm,
                         tracked->bpm / row.bpm, tracked->steadiness, tracked->beats.size(),
                         tracked->downbeats.size(), tracked->beatsPerBar, durationS,
                         row.path.c_str());
        }
    }
    if (csv != nullptr) {
        std::fclose(csv);
    }

    std::printf("  agree (+/-2%%)   : %d\n", agree);
    std::printf("  half or double  : %d\n", octave);
    std::printf("  other mismatch  : %d\n", other);
    std::printf("  no beats found  : %d\n", silent);
    std::printf("  steadiness  answers  exact  octave-tolerant\n");
    for (std::size_t band = 0; band < kSteadinessBands.size(); ++band) {
        if (answered[band] == 0) {
            continue;
        }
        std::printf("    >= %.2f     %4d   %4.0f%%   %4.0f%%\n", kSteadinessBands[band],
                    answered[band], 100.0 * correct[band] / answered[band],
                    100.0 * correctOctave[band] / answered[band]);
    }
    SUCCEED();
}

TEST_CASE("Beat tracker on a click track", "[.beat-probe]") {
    if (!magda::media::BeatTracker::isAvailable()) {
        return;
    }
    magda::media::BeatTracker tracker(magda::media::BeatTracker::defaultModelPath());

    // 120 bpm: a click every half second, 16 seconds of it.
    constexpr double rate = 44100.0;
    constexpr double bpm = 120.0;
    juce::AudioBuffer<float> buffer(1, static_cast<int>(rate * 16.0));
    buffer.clear();
    const auto period = static_cast<int>(rate * 60.0 / bpm);
    for (int start = 0; start + 2000 < buffer.getNumSamples(); start += period) {
        for (int i = 0; i < 2000; ++i) {
            const float decay = std::exp(-static_cast<float>(i) / 300.0F);
            buffer.setSample(
                0, start + i,
                decay * std::sin(2.0F * 3.14159265F * 1000.0F * static_cast<float>(i) / 44100.0F));
        }
    }

    const auto tracked = tracker.track(buffer.getReadPointer(0), buffer.getNumSamples(), rate);
    if (!tracked) {
        std::printf("\nclick track: no track at all\n");
        SUCCEED();
        return;
    }
    std::printf("\nclick track at 120: bpm=%.2f beats=%zu downbeats=%zu steady=%.2f bar=%d\n",
                tracked->bpm, tracked->beats.size(), tracked->downbeats.size(), tracked->steadiness,
                tracked->beatsPerBar);
    for (std::size_t i = 0; i < std::min<std::size_t>(8, tracked->beats.size()); ++i) {
        std::printf("   beat %zu at %.3fs\n", i, tracked->beats[i]);
    }
    SUCCEED();
}
