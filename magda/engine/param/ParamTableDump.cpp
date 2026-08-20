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
std::string rateText(const LfoSettings& lfo) {
    if (!lfo.tempoSync)
        return number(lfo.rate.hz) + "Hz";

    return "sync[" + std::to_string(lfo.rate.rateType) + "]";
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

            if (modifier.kind == ModKind::Lfo) {
                line << " " << waveText(lfo.wave) << " " << rateText(lfo) << " "
                     << syncText(lfo.sync);

                if (lfo.phaseOffset != 0.0f)
                    line << " phase=" << number(lfo.phaseOffset);
                if (lfo.oneShot)
                    line << " oneshot";
                if (lfo.invertOutput)
                    line << " inverted";
                if (lfo.useLoopRegion)
                    line << " loop[" << number(lfo.loopStart) << "," << number(lfo.loopEnd) << "]";
                if (lfo.gateOnTrigger)
                    line << " gated";
                if (!table.modCurveFor(index).empty())
                    line << " curve=" << table.modCurveFor(index).size();
            }

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
