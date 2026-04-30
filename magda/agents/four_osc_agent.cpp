#include "four_osc_agent.hpp"

#include <set>

#include "../daw/core/Config.hpp"
#include "llm_client_factory.hpp"
#include "llm_presets.hpp"

namespace magda {

// ============================================================================
// System prompt
// ============================================================================
//
// Parameter names match the auto-alias suffixes that AutoAliasGenerator
// produces for the built-in 4OSC plugin (see logs at startup —
// "@4_osc_synth.amp_attack", etc.). Keep this list in sync with the plugin
// when 4OSC parameters change. Values are normalized 0..1 across the
// board; the executor maps to TE's real-unit ranges.

const char* FourOscAgent::getSystemPrompt() {
    return R"PROMPT(You are a sound designer for the MAGDA built-in 4OSC synth. Given a user
description, produce a single preset as JSON. Output ONLY the JSON object —
no prose, no markdown fences.

OUTPUT SCHEMA:
{
  "name": "<2-4 word preset name>",
  "category": "<Bass|Lead|Pad|Pluck|Keys|FX|Other>",
  "description": "<one short sentence describing the sound>",
  "waves": { "<osc_number 1..4>": "<wave_name>", ... },
  "filter_type": "<lp|hp|bp|notch|off>",
  "voice_mode": "<mono|leg|poly>",
  "fx": { "distortion": <bool>, "reverb": <bool>, "delay": <bool>, "chorus": <bool> },
  "params": { "<param_name>": <0.0..1.0 float>, ... }
}

CATEGORY (REQUIRED) — picks the folder the preset is filed under so the
user's bank stays organised. Choose exactly one of:
  "Bass"  — sub bass, reese, 808, acid bass, growl, pluck-bass, etc.
  "Lead"  — leads, solos, mono leads, fat detuned saws used as a lead
  "Pad"   — pads, atmospheres, drones, evolving textures
  "Pluck" — short percussive plucks, key-stabs that aren't quite leads
  "Keys"  — electric piano / organ / clav / harmonic keys
  "FX"    — risers, drops, sweeps, impacts, sound design noise
  "Other" — anything that genuinely doesn't fit the above

FX TOGGLES (REQUIRED if the corresponding FX params are set):
  Each FX block has an on/off gate that is OFF by default. Setting the
  param values does NOTHING unless the gate is also turned on. Always
  include the `fx` field; emit `true` for any FX you want audible and
  `false` (or omit the key) to keep it bypassed.
    "distortion" — gates the `distortion` param (drive amount)
    "reverb"     — gates `size`, `mix`     (reverb size and dry/wet)
    "delay"      — gates `feedback`, `mix` (delay feedback / dry-wet)
    "chorus"     — gates `speed`, `width`  (chorus modulation)
  → If you set `"distortion": 0.3` in params but don't set
    `"distortion": true` in fx, the user hears no distortion.
  → For dry/clean sounds keep all four false.
  → For ambient pads turn on reverb. For dub-style basses turn on delay.
    For lush leads turn on chorus. Distortion is for grit/saturation.

VOICE MODE (set in `voice_mode`, default = leave unchanged):
  "poly" — polyphonic (default for pads, leads, plucks). Multiple notes
           sound at once.
  "mono" — monophonic, retriggers envelopes on every note. Use for tight
           bass / acid lines / lead solos where chords would muddy.
  "leg"  — monophonic with legato (envelopes only retrigger when there's
           a gap between notes). Smooth slurred lines, classic synth lead
           glide. Pair with `legato` param > 0 for portamento glide.

FILTER TYPE (REQUIRED for any sound that should be filtered):
  "off"   — filter bypassed; filter_freq / filter_resonance do NOTHING
  "lp"    — low-pass, the most common; use for warm / dark sounds
  "hp"    — high-pass; use for thinning out bass
  "bp"    — band-pass; use for vocal / focused sounds
  "notch" — band-reject; specialty
  → If you want a filter at all (you usually do — basses, leads, pads,
    plucks all sound dead with the filter off), set "lp". Only use "off"
    for clean / bright sounds where filter_freq doesn't matter.

OSCILLATOR WAVES (set per-oscillator in `waves`; default "none" turns the
oscillator off). EXACTLY these five values (plus "none") — anything else
is invalid:
  "none" — oscillator disabled
  "sine" — pure tone, fundamental only
  "square" — hollow, woody (use pulse_width_N to morph the duty cycle)
  "saw" — bright, rich harmonics, the classic analog saw
  "triangle" — soft, slightly hollow, fewer harmonics than saw
  "noise" — broadband noise (no pitch)

ALWAYS set `waves` for at least osc 1. Match a non-"none" wave with
level_N >= 0.85 on the same osc — silent oscs at level > 0 are wasted.
For thicker sounds use 2-3 oscillators with different waves (saw + saw
detuned, or saw + square) at slightly different detune_N values.

PARAMETER REFERENCE (all values normalized 0.0 to 1.0 — omit any param to
leave it at its current value):

Per-oscillator (replace N with 1, 2, 3, or 4):
  tune_N           coarse pitch (0=center, lower=down, higher=up)
  fine_tune_N      fine detune
  detune_N         per-voice detune amount
  level_N          oscillator level
  pan_N            stereo pan (0=L, 0.5=C, 1=R)
  pulse_width_N    PWM amount (square waves)
  spread_N         stereo spread

Filter:
  filter_freq      cutoff (0=closed/dark, 1=open/bright)
  filter_resonance Q amount
  filter_amount    envelope mod amount
  filter_key       keyboard tracking
  filter_velocity  velocity-to-cutoff
  filter_attack | filter_decay | filter_sustain | filter_release  ADSR

Amp:
  amp_attack | amp_decay | amp_sustain | amp_release  ADSR
  amp_velocity     velocity-to-level
  level            output level

Mod envelopes (replace N with 1 or 2):
  mod_attack_N | mod_decay_N | mod_sustain_N | mod_release_N
  depth_N          envelope depth

LFO/mod rates (replace N with 1 or 2):
  rate_N           LFO rate (0=slow, 1=fast)

Global / FX:
  mix              dry/wet
  feedback         FX feedback
  distortion       drive amount
  size | speed     mod / FX size + speed
  width | width_2  stereo width

VALUE-CURVE NOTES (READ CAREFULLY — most mistakes happen here):
- LEVEL parameters (level, level_1, level_2, level_3, level_4) are
  normalized 0.0-1.0 but dB-scaled internally. Mapping:
    1.00  → 0 dB (unity, full)
    0.95  → -6 dB
    0.85  → -12 dB
    0.70  → -30 dB (already very quiet)
    0.50  → -50 dB (basically silent)
  → For audible oscillators always emit level_N >= 0.85. For "loud" use
    1.0. For balance 0.92-0.97. NEVER use 0.5-0.7 for moderate volume.
- TUNE parameters — UNIT EXCEPTION. tune_N is emitted in SEMITONES
  (signed integer, range -36 to +36), and fine_tune_N is in CENTS
  (signed, -100 to +100). NOT normalized.
  Examples:
    "tune_1": 0      → unison (root)
    "tune_2": 12     → one octave up
    "tune_3": -12    → one octave down (sub)
    "tune_2": 7      → perfect fifth up
    "tune_2": 4      → major third up
    "fine_tune_2": 7 → +7 cents (subtle detune)
  → ALWAYS include at least one oscillator at tune_N: 0 — the root.
    Without it the patch sounds rootless / detached.
  → Musical intervals (3rds, 4ths, 5ths, octaves) are fine when the
    user asks for "chord stack" / "fifths" / "thirds" / "fat lead".
  → For thickness without intervals, leave all tune_N: 0 and use
    detune_N (0.2-0.5, normalized) for chorus spread.
  → Avoid dissonant intervals (tritone +6, minor 2nd +1) unless
    explicitly asked — the LLM tends to scatter oscs to those
    accidentally.
- ADSR TIME PARAMS — UNIT EXCEPTION. The following params are emitted
  as REAL SECONDS (not normalized): amp_attack, amp_decay, amp_release,
  filter_attack, filter_decay, filter_release. Range 0.001-60 seconds.
  Examples:
    0.005  → 5 ms   (instant)
    0.05   → 50 ms  (snappy)
    0.15   → 150 ms (short)
    0.4    → 400 ms (medium)
    1.0    → 1 s    (longish)
    3.0    → 3 s    (long, ambient)
    10.0   → 10 s   (extreme — only for explicit drones)
  → Most amp_release values land between 0.1 and 0.6. PADS top out at
    2.0. PLUCKS use 0.05-0.15. Don't emit > 4.0 unless the user
    explicitly asked for ambient / drone / infinite tail.
  → All OTHER params remain normalized 0.0-1.0.

DESIGN GUIDELINES:
- For BASSES: low tune_1 (~0.4), short amp_attack (0.0), medium amp_release
  (0.3-0.5), filter_freq 0.2-0.5, filter_resonance 0.2-0.5, level_1 1.0.
- For LEADS: medium tune (0.5), short attack, longer release, filter_freq
  0.6-0.9, some pulse width, level_N 0.95+ on active oscs.
- For PADS: long amp_attack (0.5-0.8) and amp_release (0.6-1.0), open
  filter, slow LFO rates (0.1-0.3), level_N 0.92+ on active oscs.
- For PLUCKS: zero attack, short decay (0.1-0.3), low sustain (0.0-0.2),
  level_1 1.0.
- Avoid extreme resonance (>0.8) unless explicitly asked — it self-oscillates.
- Always set at least one waves entry to a non-"none" value AND its
  matching level_N >= 0.85, otherwise the patch is silent.
- Master `level` (no underscore) is also dB-scaled. Always EMIT it (don't
  omit) — the host post-processes a safety cap based on osc count + drive +
  resonance, but only if there's a value to clamp. Aim for 0.92-0.96 (about
  -10 to -5 dB headroom). Never emit > 0.97 — there is no reason to push
  master into the red, you'll just clip the rest of the mix.

EXAMPLES:

User: "deep sub bass"
{"name":"Deep Sub","category":"Bass","description":"Pure sine sub bass with snappy envelope","waves":{"1":"sine","2":"none","3":"none","4":"none"},"filter_type":"lp","voice_mode":"mono","fx":{"distortion":false,"reverb":false,"delay":false,"chorus":false},"params":{"tune_1":-12,"level_1":1.0,"amp_attack":0.005,"amp_decay":0.15,"amp_sustain":0.7,"amp_release":0.2,"filter_freq":0.4,"filter_resonance":0.1,"level":0.96}}

User: "warm analog pad"
{"name":"Warm Pad","category":"Pad","description":"Slow-evolving detuned saws with gentle filter movement and a touch of reverb","waves":{"1":"saw","2":"saw","3":"none","4":"none"},"filter_type":"lp","voice_mode":"poly","fx":{"distortion":false,"reverb":true,"delay":false,"chorus":true},"params":{"tune_1":0,"tune_2":0,"detune_1":0.3,"detune_2":0.4,"level_1":0.95,"level_2":0.92,"amp_attack":1.5,"amp_decay":0.6,"amp_sustain":0.8,"amp_release":1.5,"filter_freq":0.55,"filter_resonance":0.15,"filter_attack":1.0,"filter_amount":0.3,"rate_1":0.15,"depth_1":0.25,"width":0.6,"mix":0.3,"size":0.7,"speed":0.4,"level":0.94}}

User: "snappy pluck"
{"name":"Snap Pluck","category":"Pluck","description":"Tight saw pluck with quick decay and filter presence","waves":{"1":"saw","2":"none","3":"none","4":"none"},"filter_type":"lp","voice_mode":"poly","fx":{"distortion":false,"reverb":false,"delay":false,"chorus":false},"params":{"tune_1":0,"level_1":1.0,"amp_attack":0.005,"amp_decay":0.12,"amp_sustain":0.0,"amp_release":0.1,"filter_freq":0.65,"filter_resonance":0.4,"filter_decay":0.15,"filter_amount":0.6,"level":0.96}}

User: "fat detuned saw lead with octave layer"
{"name":"Fat Lead","category":"Lead","description":"Two detuned saws at root + octave-up square, bright filter, lush chorus","waves":{"1":"saw","2":"saw","3":"square","4":"none"},"filter_type":"lp","voice_mode":"leg","fx":{"distortion":false,"reverb":false,"delay":false,"chorus":true},"params":{"tune_1":0,"tune_2":0,"tune_3":12,"detune_1":0.4,"detune_2":0.5,"fine_tune_2":-7,"fine_tune_1":7,"level_1":1.0,"level_2":1.0,"level_3":0.92,"pulse_width_3":0.5,"spread_1":0.5,"spread_2":0.5,"amp_attack":0.02,"amp_decay":0.2,"amp_sustain":0.7,"amp_release":0.3,"filter_freq":0.7,"filter_resonance":0.3,"filter_amount":0.4,"width":0.7,"speed":0.5,"level":0.92,"legato":0.25}}
)PROMPT";
}

// ============================================================================
// JSON parser
// ============================================================================

FourOscAgent::Preset FourOscAgent::parseJson(const juce::String& text, std::string& outError) {
    Preset preset;

    // LLMs sometimes wrap JSON in ```json ... ``` even when told not to.
    // Strip the fence before parsing rather than failing on it.
    juce::String trimmed = text.trim();
    if (trimmed.startsWith("```")) {
        auto firstNewline = trimmed.indexOf("\n");
        if (firstNewline > 0)
            trimmed = trimmed.substring(firstNewline + 1);
        if (trimmed.endsWith("```"))
            trimmed = trimmed.dropLastCharacters(3).trim();
    }

    auto parsed = juce::JSON::parse(trimmed);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) {
        outError = "preset JSON must be an object";
        return preset;
    }

    if (auto name = obj->getProperty("name"); name.isString())
        preset.name = name.toString().toStdString();
    if (auto cat = obj->getProperty("category"); cat.isString())
        preset.category = cat.toString().toStdString();
    if (auto desc = obj->getProperty("description"); desc.isString())
        preset.description = desc.toString().toStdString();

    auto paramsVar = obj->getProperty("params");
    auto* paramsObj = paramsVar.getDynamicObject();
    if (paramsObj == nullptr) {
        outError = "preset JSON missing 'params' object";
        return preset;
    }

    // Per-param clamp ranges. Most params are normalized 0..1, but a few
    // are emitted in real units (per the UNIT EXCEPTION rules in the
    // system prompt). Keys not listed here default to normalized.
    auto rangeFor = [](const juce::String& key) -> std::pair<float, float> {
        if (key == "amp_attack" || key == "amp_decay" || key == "amp_release" ||
            key == "filter_attack" || key == "filter_decay" || key == "filter_release")
            return {0.0f, 60.0f};  // seconds
        if (key == "tune_1" || key == "tune_2" || key == "tune_3" || key == "tune_4")
            return {-36.0f, 36.0f};  // semitones
        if (key == "fine_tune_1" || key == "fine_tune_2" || key == "fine_tune_3" ||
            key == "fine_tune_4")
            return {-100.0f, 100.0f};  // cents
        return {0.0f, 1.0f};           // normalized
    };

    for (const auto& kv : paramsObj->getProperties()) {
        if (!kv.value.isDouble() && !kv.value.isInt())
            continue;  // skip non-numeric entries silently
        const auto raw = static_cast<float>(static_cast<double>(kv.value));
        const auto key = kv.name.toString();
        const auto [lo, hi] = rangeFor(key);
        preset.params.emplace(key.toStdString(), juce::jlimit(lo, hi, raw));
    }

    // Optional `waves` map: { "1": "sine", "2": "saw", ... }. Values
    // that aren't strings or known wave names are skipped — the applier
    // will leave the corresponding oscillator at its current setting.
    if (auto* wavesObj = obj->getProperty("waves").getDynamicObject()) {
        for (const auto& kv : wavesObj->getProperties()) {
            if (!kv.value.isString())
                continue;
            const auto oscNum = kv.name.toString().getIntValue();
            if (oscNum < 1 || oscNum > 4)
                continue;
            preset.waves.emplace(oscNum, kv.value.toString().toStdString());
        }
    }

    if (auto ft = obj->getProperty("filter_type"); ft.isString())
        preset.filterType = ft.toString().toStdString();
    if (auto vm = obj->getProperty("voice_mode"); vm.isString())
        preset.voiceMode = vm.toString().toStdString();

    // Optional `fx` map: { "distortion": bool, "reverb": bool, ... }.
    // Only the four known keys are accepted; everything else is dropped.
    if (auto* fxObj = obj->getProperty("fx").getDynamicObject()) {
        static const std::set<juce::String> kFxKeys = {"distortion", "reverb", "delay", "chorus"};
        for (const auto& kv : fxObj->getProperties()) {
            const auto key = kv.name.toString();
            if (!kFxKeys.count(key))
                continue;
            if (kv.value.isBool() || kv.value.isInt() || kv.value.isDouble())
                preset.fx.emplace(key.toStdString(), static_cast<bool>(kv.value));
        }
    }

    return preset;
}

// ============================================================================
// Master-level safety cap
// ============================================================================
//
// LLMs occasionally emit `level: 0` (silent), `level: 1.0` with four oscs
// and high distortion (clipping), or omit `level` entirely. None of those
// are speaker-friendly. This post-process estimates the worst-case peak
// gain of the patch and clamps `level` so the output sits at roughly
// -3 to -10 dB headroom — without overriding sensible LLM choices.
//
// Heuristic only — we don't render audio, so coherent vs. uncorrelated
// summing, filter envelope timing, and phase cancellation are all
// ignored. The point is to stop obviously-too-loud patches from blowing
// monitors, not to be a real loudness model.

namespace {

void applyMasterLevelSafetyCap(FourOscAgent::Preset& preset) {
    // Active oscs: wave != "none" AND level_N >= 0.5 (silent oscs don't
    // contribute to peak).
    int activeOscs = 0;
    for (int n = 1; n <= 4; ++n) {
        auto wIt = preset.waves.find(n);
        if (wIt == preset.waves.end() || wIt->second == "none")
            continue;
        const auto lvlKey = "level_" + std::to_string(n);
        auto lIt = preset.params.find(lvlKey);
        const float lvl = lIt != preset.params.end() ? lIt->second : 1.0f;
        if (lvl >= 0.5f)
            ++activeOscs;
    }
    if (activeOscs == 0)
        return;

    // Base cap by active osc count. Worst-case coherent peak summing is
    // ~6 dB per doubling; we cap a bit gentler than that to avoid being
    // overly opinionated on patches the LLM tuned by ear.
    float cap = 1.0f;
    if (activeOscs == 2)
        cap = 0.96f;  // ~ -5 dB
    else if (activeOscs == 3)
        cap = 0.94f;  // ~ -7 dB
    else if (activeOscs >= 4)
        cap = 0.92f;  // ~ -10 dB

    // Distortion drives further into clipping — only enabled if the
    // distortion FX gate is on. Without the toggle, `distortion` value
    // is inert.
    auto fxOn = [&](const std::string& key) {
        auto it = preset.fx.find(key);
        return it != preset.fx.end() && it->second;
    };
    if (fxOn("distortion")) {
        auto distIt = preset.params.find("distortion");
        if (distIt != preset.params.end() && distIt->second > 0.3f)
            cap -= 0.02f;
    }
    // High filter resonance can ring +6 dB around cutoff regardless of
    // FX state.
    auto resIt = preset.params.find("filter_resonance");
    if (resIt != preset.params.end() && resIt->second > 0.7f)
        cap -= 0.02f;

    cap = juce::jlimit(0.85f, 1.0f, cap);

    auto levelIt = preset.params.find("level");
    if (levelIt == preset.params.end() || levelIt->second < 0.5f) {
        // LLM omitted master level (or emitted something near-silent —
        // the prompt is explicit that level is dB-scaled and 0.5 already
        // means basically inaudible). Snap it to the cap so the patch is
        // actually audible at a safe level.
        preset.params["level"] = cap;
        return;
    }
    if (levelIt->second > cap)
        preset.params["level"] = cap;
    // Otherwise leave the LLM's value alone — already at or below cap.
}

}  // namespace

// ============================================================================
// LLM call
// ============================================================================

namespace {

void logFourOscAgentConfig(const Config::AgentLLMConfig& agentConfig,
                           const llm::ProviderConfig& pc) {
    DBG("MAGDA FourOscAgent config:");
    DBG("  provider (string) = " + juce::String(agentConfig.provider));
    DBG("  provider (enum)   = " + juce::String(static_cast<int>(pc.provider)));
    DBG("  model             = " + pc.model);
    DBG("  baseUrl           = " + pc.baseUrl);
    DBG("  apiKey present    = " + juce::String(pc.apiKey.isNotEmpty() ? "yes" : "NO"));
    DBG("  noTemperature     = " + juce::String(pc.noTemperature ? "yes" : "no"));
}

void logFourOscAgentResult(const FourOscAgent::GenerateResult& result) {
    DBG("MAGDA FourOscAgent raw output (" +
        juce::String(static_cast<int>(result.rawOutput.size())) + " chars):");
    DBG("---8<---");
    DBG(juce::String(result.rawOutput));
    DBG("--->8---");
    DBG("MAGDA FourOscAgent parsed: name='" + juce::String(result.preset.name) +
        "' params=" + juce::String(static_cast<int>(result.preset.params.size())));
    if (!result.error.empty())
        DBG("MAGDA FourOscAgent ERROR: " + juce::String(result.error));
}

}  // namespace

FourOscAgent::GenerateResult FourOscAgent::generate(const std::string& message) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    // Reuse the MUSIC role for now — the user already has it configured
    // for creative generation, and we don't yet have enough signal to
    // justify a dedicated role + LLM provider preset.
    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    auto providerConfig = toLLMProviderConfig(agentConfig, "four_osc");
    logFourOscAgentConfig(agentConfig, providerConfig);

    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.error = "FourOsc agent API key not configured.";
        result.hasError = true;
        return result;
    }

    auto client = createLLMClient(agentConfig, "four_osc");

    llm::Request request;
    request.systemPrompt = juce::String::fromUTF8(getSystemPrompt());
    request.userMessage = juce::String::fromUTF8(message.c_str());
    // Lower temperature than music — sound design rewards consistency
    // (e.g. "bass" should reliably produce bass-shaped envelopes), and
    // the parameter space is large enough that high temperature wanders
    // into noise.
    request.temperature = 0.2f;

    auto response = client->sendRequest(request);
    if (!response.success) {
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    auto trimmedText = response.text.trim();
    result.rawOutput = trimmedText.toStdString();

    std::string parseError;
    result.preset = parseJson(trimmedText, parseError);
    if (!parseError.empty() || result.preset.params.empty()) {
        result.error = parseError.empty() ? "preset JSON contained no parameters" : parseError;
        result.hasError = true;
    } else {
        applyMasterLevelSafetyCap(result.preset);
    }

    logFourOscAgentResult(result);
    return result;
}

FourOscAgent::GenerateResult FourOscAgent::generateStreaming(const std::string& message,
                                                             TokenCallback onToken) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    auto providerConfig = toLLMProviderConfig(agentConfig, "four_osc");
    logFourOscAgentConfig(agentConfig, providerConfig);

    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.error = "FourOsc agent API key not configured.";
        result.hasError = true;
        return result;
    }

    auto client = createLLMClient(agentConfig, "four_osc");

    llm::Request request;
    request.systemPrompt = juce::String::fromUTF8(getSystemPrompt());
    request.userMessage = juce::String::fromUTF8(message.c_str());
    request.temperature = 0.2f;

    auto response = client->sendStreamingRequest(request, [&](const juce::String& token) {
        if (shouldStop_.load())
            return false;
        if (onToken)
            return onToken(token);
        return true;
    });

    if (!response.success) {
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    auto trimmedText = response.text.trim();
    result.rawOutput = trimmedText.toStdString();

    std::string parseError;
    result.preset = parseJson(trimmedText, parseError);
    if (!parseError.empty() || result.preset.params.empty()) {
        result.error = parseError.empty() ? "preset JSON contained no parameters" : parseError;
        result.hasError = true;
    } else {
        applyMasterLevelSafetyCap(result.preset);
    }

    logFourOscAgentResult(result);
    return result;
}

}  // namespace magda
