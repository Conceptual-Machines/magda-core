#include "MixAnalysisInput.hpp"

#include <array>
#include <cmath>

#include "BandSpectrum.hpp"
#include "MaskingDetector.hpp"
#include "TrackMeasurer.hpp"

namespace magda::daw::audio {

namespace {

// 2048 so each processed block is one masking-FFT frame.
constexpr int kBlock = 2048;

using BandArray = std::array<float, kNumMaskingBands>;

// Measure a buffer; also produces the song-averaged 1/3-octave band spectrum
// when bandsOut is given. computeMaskingBandsDb rebuilds an FFT per call, so a
// song-average only samples ~128 frames rather than every block.
TrackMeasurementSnapshot measure(const juce::AudioBuffer<float>& buf, double sr,
                                 BandArray* bandsOut, bool enableTruePeak) {
    TrackMeasurer m;
    m.prepare(sr, kBlock, enableTruePeak);
    if (bandsOut != nullptr)
        m.setSpectrumCaptureEnabled(true);

    const int len = buf.getNumSamples();
    const int nch = juce::jmin(2, buf.getNumChannels());
    std::array<double, kNumMaskingBands> acc{};
    long frames = 0;
    BandArray frameBands{};

    const int totalBlocks = (len + kBlock - 1) / kBlock;
    const int hop = juce::jmax(1, totalBlocks / 128);
    int blockIdx = 0;

    for (int pos = 0; pos < len; pos += kBlock, ++blockIdx) {
        const int n = juce::jmin(kBlock, len - pos);
        const float* chans[2];
        chans[0] = buf.getReadPointer(0) + pos;
        chans[1] = nch > 1 ? buf.getReadPointer(1) + pos : chans[0];
        m.process(chans, nch, n);
        if (bandsOut != nullptr && n >= 2048 && (blockIdx % hop) == 0) {
            computeMaskingBandsDb(m.getSpectrumRing(), sr, frameBands);
            for (int b = 0; b < kNumMaskingBands; ++b)
                acc[static_cast<size_t>(b)] +=
                    std::pow(10.0, frameBands[static_cast<size_t>(b)] / 10.0);
            ++frames;
        }
    }
    if (bandsOut != nullptr)
        for (int b = 0; b < kNumMaskingBands; ++b)
            (*bandsOut)[static_cast<size_t>(b)] =
                frames > 0
                    ? static_cast<float>(10.0 * std::log10(acc[static_cast<size_t>(b)] / frames))
                    : -120.0f;
    return m.read();
}

std::vector<float> collapseBands(const BandArray& bandsDb, const std::vector<float>& upperHz) {
    std::vector<double> acc(upperHz.size(), 0.0);
    for (int b = 0; b < kNumMaskingBands; ++b) {
        const float center = std::sqrt(maskingBandEdgeHz(b) * maskingBandEdgeHz(b + 1));
        size_t mi = 0;
        while (mi + 1 < upperHz.size() && center >= upperHz[mi])
            ++mi;
        acc[mi] += std::pow(10.0, bandsDb[static_cast<size_t>(b)] / 10.0);
    }
    std::vector<float> out(upperHz.size());
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = acc[i] > 0.0 ? static_cast<float>(10.0 * std::log10(acc[i])) : -120.0f;
    return out;
}

// sub / low / low-mid / mid / high-mid / high
std::vector<float> collapseToMacro(const BandArray& bandsDb) {
    return collapseBands(bandsDb, {60.0f, 250.0f, 800.0f, 2500.0f, 6000.0f, 1.0e9f});
}

// low / mid / high (timeline)
std::vector<float> collapseTo3(const BandArray& bandsDb) {
    return collapseBands(bandsDb, {250.0f, 2500.0f, 1.0e9f});
}

struct SpectralFeatures {
    float centroidHz = 0.0f;
    float flatness = 0.0f;
    float rolloffHz = 0.0f;
};

SpectralFeatures spectralFeatures(const BandArray& bandsDb) {
    std::array<double, kNumMaskingBands> e{};
    std::array<float, kNumMaskingBands> center{};
    float peakDb = -1000.0f;
    for (int b = 0; b < kNumMaskingBands; ++b) {
        e[static_cast<size_t>(b)] = std::pow(10.0, bandsDb[static_cast<size_t>(b)] / 10.0);
        center[static_cast<size_t>(b)] = std::sqrt(maskingBandEdgeHz(b) * maskingBandEdgeHz(b + 1));
        peakDb = juce::jmax(peakDb, bandsDb[static_cast<size_t>(b)]);
    }
    double sumE = 0.0, sumFE = 0.0;
    for (int b = 0; b < kNumMaskingBands; ++b) {
        sumE += e[static_cast<size_t>(b)];
        sumFE += static_cast<double>(center[static_cast<size_t>(b)]) * e[static_cast<size_t>(b)];
    }
    SpectralFeatures f;
    f.centroidHz = sumE > 0.0 ? static_cast<float>(sumFE / sumE) : 0.0f;

    double logSum = 0.0, ariSum = 0.0;
    int cnt = 0;
    for (int b = 0; b < kNumMaskingBands; ++b) {
        if (bandsDb[static_cast<size_t>(b)] < peakDb - 60.0f)
            continue;
        logSum += std::log(e[static_cast<size_t>(b)] + 1.0e-20);
        ariSum += e[static_cast<size_t>(b)];
        ++cnt;
    }
    if (cnt > 0) {
        const double geo = std::exp(logSum / cnt);
        const double ari = ariSum / cnt;
        f.flatness = ari > 0.0 ? static_cast<float>(juce::jlimit(0.0, 1.0, geo / ari)) : 0.0f;
    }

    const double target = 0.85 * sumE;
    double cum = 0.0;
    for (int b = 0; b < kNumMaskingBands; ++b) {
        cum += e[static_cast<size_t>(b)];
        if (cum >= target) {
            f.rolloffHz = center[static_cast<size_t>(b)];
            break;
        }
    }
    return f;
}

void stereoCorrWidth(const juce::AudioBuffer<float>& buf, float& corr, float& width) {
    if (buf.getNumChannels() < 2) {
        corr = 1.0f;
        width = 0.0f;
        return;
    }
    const float* l = buf.getReadPointer(0);
    const float* r = buf.getReadPointer(1);
    const int n = buf.getNumSamples();
    double sumLR = 0, sumLL = 0, sumRR = 0, sumMid = 0, sumSide = 0;
    for (int i = 0; i < n; ++i) {
        const double xl = l[i], xr = r[i];
        sumLR += xl * xr;
        sumLL += xl * xl;
        sumRR += xr * xr;
        const double mid = 0.5 * (xl + xr), side = 0.5 * (xl - xr);
        sumMid += mid * mid;
        sumSide += side * side;
    }
    const double denom = std::sqrt(sumLL * sumRR);
    corr = denom > 1.0e-12 ? static_cast<float>(juce::jlimit(-1.0, 1.0, sumLR / denom)) : 1.0f;
    const double ms = sumMid + sumSide;
    width = ms > 1.0e-12 ? static_cast<float>(sumSide / ms) : 0.0f;
}

MixAnalysisAgent::TrackMix snapshotToMix(const juce::String& name, const std::string& role,
                                         const TrackMeasurementSnapshot& s) {
    MixAnalysisAgent::TrackMix t;
    t.name = name.toStdString();
    t.role = role;
    t.integratedLufs = s.integratedLufs;
    t.shortTermLufs = s.shortTermLufs;
    t.samplePeakDb = s.samplePeakDb;
    t.truePeakDb = s.truePeakDb;
    t.truePeakValid = s.truePeakValid;
    t.plr = s.plr;
    t.psr = s.psr;
    t.correlation = s.correlation;
    t.width = s.width;
    return t;
}

MixAnalysisAgent::TrackMix measureToMix(const juce::AudioBuffer<float>& buf, double sr,
                                        const juce::String& name, const std::string& role,
                                        bool truePeak, BandArray& bandsOut) {
    auto mix = snapshotToMix(name, role, measure(buf, sr, &bandsOut, truePeak));
    mix.tonalDb = collapseToMacro(bandsOut);
    const auto sf = spectralFeatures(bandsOut);
    mix.spectralCentroidHz = sf.centroidHz;
    mix.spectralFlatness = sf.flatness;
    mix.spectralRolloffHz = sf.rolloffHz;
    stereoCorrWidth(buf, mix.correlation, mix.width);
    return mix;
}

struct SectionBound {
    juce::String label;
    int start = 0;
    int len = 0;
};

std::vector<SectionBound> autoSections(int total, int n) {
    std::vector<SectionBound> out;
    for (int i = 0; i < n; ++i) {
        const int s = static_cast<int>(static_cast<int64_t>(total) * i / n);
        const int e = static_cast<int>(static_cast<int64_t>(total) * (i + 1) / n);
        out.push_back({juce::String(i + 1) + "/" + juce::String(n), s, e - s});
    }
    return out;
}

}  // namespace

MixAnalysisAgent::TrackMix MixAnalysisInput::fingerprint(const juce::AudioBuffer<float>& buf,
                                                         double sr, const juce::String& name,
                                                         const std::string& role) {
    BandArray bands{};
    return measureToMix(buf, sr, name, role, /*truePeak*/ true, bands);
}

MixAnalysisAgent::Input MixAnalysisInput::build(double sr, const std::vector<Source>& tracks,
                                                const juce::AudioBuffer<float>* masterIn,
                                                const std::vector<Source>& references,
                                                const Options& opts) {
    MixAnalysisAgent::Input input;
    std::vector<TrackBandEnergies> bandSet;
    int maxLen = 0;

    for (int i = 0; i < static_cast<int>(tracks.size()); ++i) {
        const auto& s = tracks[static_cast<size_t>(i)];
        if (s.audio == nullptr || s.audio->getNumSamples() == 0)
            continue;
        maxLen = juce::jmax(maxLen, s.audio->getNumSamples());

        BandArray bands{};
        input.tracks.push_back(
            measureToMix(*s.audio, sr, s.name, s.role, /*truePeak*/ false, bands));

        TrackBandEnergies be;
        be.trackId = i;
        be.name = s.name;
        be.bandDb = bands;
        bandSet.push_back(std::move(be));
    }

    // Master: provided, else a normalised (-1 dBFS) sum of the stems.
    juce::AudioBuffer<float> summed;
    const juce::AudioBuffer<float>* master = masterIn;
    if (master == nullptr && maxLen > 0) {
        summed.setSize(2, maxLen);
        summed.clear();
        for (const auto& s : tracks) {
            if (s.audio == nullptr)
                continue;
            const int n = s.audio->getNumSamples();
            const int nch = s.audio->getNumChannels();
            summed.addFrom(0, 0, *s.audio, 0, 0, n);
            summed.addFrom(1, 0, *s.audio, nch > 1 ? 1 : 0, 0, n);
        }
        const float pk = summed.getMagnitude(0, maxLen);
        if (pk > 0.0f)
            summed.applyGain(juce::Decibels::decibelsToGain(-1.0f) / pk);
        master = &summed;
    }
    if (master != nullptr && master->getNumSamples() > 0)
        input.master = fingerprint(*master, sr, "Master", "master");

    // Inter-track masking from the measured spectra.
    for (const auto& f : detectMasking(bandSet))
        input.masking.push_back(
            {f.nameA.toStdString(), f.nameB.toStdString(), f.loHz, f.hiHz, f.severity});

    // Timeline: slice the master over time.
    if (master != nullptr) {
        const int mnch = master->getNumChannels();
        for (const auto& sec : autoSections(master->getNumSamples(), opts.numSegments)) {
            if (sec.len < 2048)
                continue;
            juce::AudioBuffer<float> win(2, sec.len);
            win.copyFrom(0, 0, *master, 0, sec.start, sec.len);
            win.copyFrom(1, 0, *master, mnch > 1 ? 1 : 0, sec.start, sec.len);

            BandArray segBands{};
            auto segSnap = measure(win, sr, &segBands, /*truePeak*/ false);

            MixAnalysisAgent::Segment seg;
            seg.label = sec.label.toStdString();
            seg.startSec = static_cast<float>(sec.start / sr);
            seg.endSec = static_cast<float>((sec.start + sec.len) / sr);
            seg.integratedLufs = segSnap.integratedLufs;
            seg.spectralCentroidHz = spectralFeatures(segBands).centroidHz;
            float segCorr = 1.0f;
            stereoCorrWidth(win, segCorr, seg.width);
            seg.tonalDb = collapseTo3(segBands);
            input.timeline.push_back(std::move(seg));
        }
    }

    // References (genre targets).
    for (const auto& r : references)
        if (r.audio != nullptr && r.audio->getNumSamples() > 0)
            input.references.push_back(fingerprint(*r.audio, sr, r.name, r.role));

    return input;
}

}  // namespace magda::daw::audio
