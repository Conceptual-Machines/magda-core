#include "param/ParamTableDump.hpp"

#include <iomanip>
#include <sstream>

#include "plan/DumpFormat.hpp"

namespace magda::engine {

namespace {

using dump_format::padded;
using dump_format::rightAligned;
using dump_format::writeLine;

std::string number(float value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << value;
    return out.str();
}

/// The scale, short enough to sit in a column: what a position means, which is
/// the part of a spec a reader is checking.
std::string domainText(const magda::ParameterUtils::ParameterDomain& domain) {
    const auto range = "[" + number(domain.minValue) + "," + number(domain.maxValue) + "]";

    switch (domain.scale) {
        case magda::ParameterScale::Linear:
            return "lin" + range;
        case magda::ParameterScale::Logarithmic:
            return "log" + range;
        case magda::ParameterScale::Exponential:
            return "exp" + range;
        case magda::ParameterScale::Discrete:
            return "step[" + std::to_string(domain.choiceCount) + "]";
        case magda::ParameterScale::Boolean:
            return "bool";
        case magda::ParameterScale::FaderDB:
            return "fader" + range;
    }
    return "?" + range;
}

std::string waveText(magda::LFOWaveform wave) {
    switch (wave) {
        case magda::LFOWaveform::Sine:
            return "sine";
        case magda::LFOWaveform::Triangle:
            return "triangle";
        case magda::LFOWaveform::Square:
            return "square";
        case magda::LFOWaveform::Saw:
            return "saw";
        case magda::LFOWaveform::ReverseSaw:
            return "revsaw";
        case magda::LFOWaveform::Custom:
            return "custom";
    }
    return "?";
}

std::string syncText(ModSync sync) {
    switch (sync) {
        case ModSync::Free:
            return "free";
        case ModSync::Transport:
            return "transport";
        case ModSync::Note:
            return "note";
    }
    return "?";
}

/// What kind of engine drives it, for a table where only one of them runs.
std::string kindText(ModKind kind) {
    switch (kind) {
        case ModKind::Lfo:
            return "lfo";
        case ModKind::Adsr:
            return "adsr";
        case ModKind::Random:
            return "random";
        case ModKind::Follower:
            return "follower";
    }
    return "?";
}

/// The rate, as the thing a reader is checking: a frequency, or the division
/// ordinal a synced modifier's period comes from.
std::string rateText(bool tempoSync, const LfoRate& rate) {
    if (!tempoSync)
        return number(rate.hz) + "Hz";

    return "sync[" + std::to_string(rate.rateType) + "]";
}

/// A stage length, which is a time unless the envelope is synced, in which case
/// every stage runs at the one division the model carries.
std::string stageText(float milliseconds, const AdsrSettings& adsr) {
    if (adsr.tempoSync && adsr.rateType != static_cast<int>(magda::ModRateType::Hertz))
        return "sync[" + std::to_string(adsr.rateType) + "]";

    return number(milliseconds) + "ms";
}

/// What a random modulator does with its cycle.
std::string randomTypeText(RandomShape type) {
    return type == RandomShape::Noise ? "noise" : "stepped";
}

}  // namespace

std::string dumpParamTable(const ParamTable& table) {
    std::ostringstream out;

    out << "magda-param-table v1\n";
    out << "params=" << table.size() << " modifiers=" << table.modifiers.size()
        << " links=" << table.links.size() << "\n";

    for (int param = 0; param < table.size(); ++param) {
        const auto index = static_cast<std::size_t>(param);
        const auto links = table.linksFor(param);

        std::ostringstream line;
        line << "[" << rightAligned(param, 3) << "] " << padded(toString(table.keys[index]), 28)
             << " " << padded(domainText(table.specs[index].domain), 16) << " "
             << "base=" << number(table.base[index]) << "  links=" << links.size();

        if (!table.curveFor(param).empty())
            line << " lane=" << table.curveFor(param).size();
        if (!table.specs[index].modulatable)
            line << " fixed";
        if (table.specs[index].segmentAccurate)
            line << " segmented";

        writeLine(out, line.str());

        for (const auto& link : links) {
            std::ostringstream detail;
            const auto source =
                link.source.kind == ParamSourceRef::Kind::Parameter ? "param" : "mod";
            detail << "      <- " << source << "[" << rightAligned(link.source.index, 3) << "]"
                   << " amount=" << number(link.amount) << " bipolar=" << (link.bipolar ? 1 : 0);
            writeLine(out, detail.str());
        }
    }

    if (!table.modifiers.empty()) {
        out << "modifiers:\n";
        for (std::size_t i = 0; i < table.modifiers.size(); ++i) {
            const auto& modifier = table.modifiers[i];
            const auto& lfo = modifier.lfo;
            const auto index = static_cast<int>(i);

            std::ostringstream line;
            line << "[" << rightAligned(index, 3) << "] " << padded(toString(modifier.key), 28)
                 << " " << padded(kindText(modifier.kind), 9)
                 << " value=" << number(modifier.value);

            if (!modifier.enabled)
                line << " off";

            // Only the live kind's settings, because the other three are at
            // their defaults and printing them would be four modifiers'
            // worth of noise around the one that is there.
            switch (modifier.kind) {
                case ModKind::Lfo: {
                    line << " " << waveText(lfo.wave) << " " << rateText(lfo.tempoSync, lfo.rate)
                         << " " << syncText(lfo.sync);

                    if (lfo.phaseOffset != 0.0f)
                        line << " phase=" << number(lfo.phaseOffset);
                    if (lfo.oneShot)
                        line << " oneshot";
                    if (lfo.invertOutput)
                        line << " inverted";
                    if (lfo.useLoopRegion)
                        line << " loop[" << number(lfo.loopStart) << "," << number(lfo.loopEnd)
                             << "]";
                    if (lfo.gateOnTrigger)
                        line << " gated";
                    if (!table.modCurveFor(index).empty())
                        line << " curve=" << table.modCurveFor(index).size();
                    break;
                }

                case ModKind::Adsr: {
                    const auto& adsr = modifier.adsr;
                    line << " a=" << stageText(adsr.attackMs, adsr)
                         << " d=" << stageText(adsr.decayMs, adsr) << " s=" << number(adsr.sustain)
                         << " r=" << stageText(adsr.releaseMs, adsr) << " " << syncText(adsr.sync);

                    if (adsr.attackCurve != 0.0f || adsr.decayCurve != 0.0f ||
                        adsr.releaseCurve != 0.0f)
                        line << " curves[" << number(adsr.attackCurve) << ","
                             << number(adsr.decayCurve) << "," << number(adsr.releaseCurve) << "]";
                    break;
                }

                case ModKind::Random: {
                    const auto& random = modifier.random;
                    line << " " << randomTypeText(random.type) << " "
                         << rateText(random.tempoSync, random.rate) << " " << syncText(random.sync)
                         << " shape=" << number(random.shape) << " smooth=" << number(random.smooth)
                         << " step=" << number(random.stepDepth);
                    break;
                }

                case ModKind::Follower: {
                    const auto& follower = modifier.follower;
                    line << " gain=" << number(follower.gainDb) << "dB"
                         << " a=" << number(follower.attackMs) << "ms"
                         << " h=" << number(follower.holdMs) << "ms"
                         << " r=" << number(follower.releaseMs) << "ms";

                    if (follower.highPass)
                        line << " hp=" << number(follower.highPassHz) << "Hz";
                    if (follower.lowPass)
                        line << " lp=" << number(follower.lowPassHz) << "Hz";
                    break;
                }
            }

            // What it listens to, where it listens at all. The far end of the
            // plan's modulation tap, printed so a golden shows the edge from
            // both sides rather than only from the plan's.
            if (modifier.source != magda::INVALID_TRACK_ID)
                line << " source=track[" << modifier.source << "]";

            if (modifier.rate != INVALID_PARAM_ID)
                line << " rate=param[" << modifier.rate << "]";

            writeLine(out, line.str());
        }
    }

    std::ostringstream order;
    order << "order:";
    for (const auto& step : table.order)
        order << " " << (step.kind == ParamStep::Kind::Modifier ? "m" : "") << step.index;
    writeLine(out, order.str());

    if (!table.diagnostics.empty()) {
        out << "diagnostics:\n";
        for (const auto& message : table.diagnostics)
            writeLine(out, "  " + message);
    }

    return out.str();
}

}  // namespace magda::engine
