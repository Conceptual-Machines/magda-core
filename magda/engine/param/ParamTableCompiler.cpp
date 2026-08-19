#include "param/ParamTableCompiler.hpp"

#include <algorithm>
#include <queue>
#include <set>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"

namespace magda::engine {

namespace {

/// A parameter index past this is a device misreporting rather than a device
/// with a great many parameters. The table allocates a slot per index up to the
/// highest one a device declares, so without a ceiling the model would be
/// deciding how much the engine allocates.
constexpr int kMaxDeviceParamIndex = 4096;

/// What a macro knob is: a position between nothing and everything, with the
/// meaning of both left to whatever it is linked to.
ParamSpec macroSpec() {
    ParamSpec spec;
    spec.domain.scale = magda::ParameterScale::Linear;
    spec.domain.minValue = 0.0f;
    spec.domain.maxValue = 1.0f;
    return spec;
}

/// A link as this pass needs it, whichever kind of source it came from.
struct PendingLink {
    ParamSourceRef source;
    std::string sourceName;
    magda::ControlTarget target;
    float amount = 0.0f;
    bool bipolar = false;
};

/// One thing in the model that owns parameters: a track, a rack, or a device.
/// Collected first and allocated second, because whether a macro is worth a
/// slot depends on what every other node's links point at.
struct Node {
    ParamKey scope;
    const magda::MacroArray* macros = nullptr;
    const magda::ModArray* mods = nullptr;
    const magda::DeviceInfo* device = nullptr;
};

class Builder {
  public:
    ParamTable run(const RenderPlan& plan, const std::vector<magda::TrackInfo>& tracks,
                   const magda::TrackInfo& master);

  private:
    ParamId add(const ParamKey& key, const ParamSpec& spec, float base);

    void walkTrack(const magda::TrackInfo& track);
    void walkRack(const magda::RackInfo& rack, magda::TrackId trackId);
    void walkElements(const std::vector<magda::ChainElement>& elements, magda::TrackId trackId,
                      magda::RackId rackId);
    void walkFlat(const std::vector<magda::PostFxChainElement>& elements, magda::TrackId trackId,
                  ChainSegment segment);

    void collectTargets();
    void allocate();
    void allocateDevice(const Node& node);
    void allocateMacros(const Node& node);
    void allocateMods(const Node& node);

    void resolveLinks();
    void orderAndBreakCycles();
    void flattenLinks();

    void diagnose(const std::string& what) {
        table_.diagnostics.push_back(what);
    }

    ParamTable table_;
    std::vector<Node> nodes_;
    std::vector<PendingLink> pending_;

    /// Every parameter something links to, whether or not it exists. What makes
    /// a macro worth a slot: one with no links of its own that a modifier
    /// drives is a macro the project uses.
    std::set<ParamKey> targeted_;

    /// Per parameter, until the cycle pass has decided which survive.
    std::vector<std::vector<ParamLink>> perParam_;
};

ParamId Builder::add(const ParamKey& key, const ParamSpec& spec, float base) {
    const auto existing = table_.byKey.find(key);
    if (existing != table_.byKey.end()) {
        // Two things in the model claiming one address. The plan compiler
        // refuses a duplicate OpKey for the same reason: an address that names
        // two things is an address that names neither.
        diagnose(toString(key) + ": two parameters claim this address; the second is ignored");
        return existing->second;
    }

    const auto id = static_cast<ParamId>(table_.keys.size());
    table_.keys.push_back(key);
    table_.specs.push_back(spec);
    table_.base.push_back(base);
    perParam_.emplace_back();
    table_.byKey.emplace(key, id);
    return id;
}

void Builder::allocateMacros(const Node& node) {
    if (node.macros == nullptr)
        return;

    for (std::size_t i = 0; i < node.macros->size(); ++i) {
        const auto& macro = (*node.macros)[i];

        ParamKey key = node.scope;
        key.kind = ParamKey::Kind::Macro;
        key.index = static_cast<int>(i);

        // Every scope in the model comes with sixteen macros whether or not
        // anything uses them, and a project of any size would otherwise spend
        // most of its table on knobs with nothing behind them. A macro is worth
        // a slot when it drives something or something drives it; the rest are
        // stored values the model keeps and the engine has no use for.
        //
        // An automation lane targeting one is the third way to be worth a slot,
        // and it lands with the lanes (#2118).
        if (macro.links.empty() && targeted_.count(key) == 0)
            continue;

        // The macro's own value is a parameter like any other: it is stored, a
        // knob moves it, and an automation lane will play over it.
        const auto id = add(key, macroSpec(), macro.value);

        for (const auto& link : macro.links)
            pending_.push_back(PendingLink{ParamSourceRef{ParamSourceRef::Kind::Parameter, id},
                                           toString(key), link.target, link.amount, link.bipolar});
    }
}

void Builder::allocateMods(const Node& node) {
    if (node.mods == nullptr)
        return;

    for (const auto& mod : *node.mods) {
        ParamKey key = node.scope;
        key.kind = ParamKey::Kind::ModParam;
        key.modId = mod.id;
        key.index = -1;

        // The model's own reading of the modifier, which is a constant until
        // the engines land (#2119, #2120). The two rules that are already
        // invariants rather than implementation get applied here: an inactive
        // modifier outputs nothing, and a curve drawn as a level is applied as
        // its complement.
        const float output = mod.enabled ? (mod.invertOutput ? 1.0f - mod.value : mod.value) : 0.0f;

        const auto index = static_cast<int>(table_.modifiers.size());
        table_.modifiers.push_back(ParamModifier{key, output});

        for (const auto& link : mod.links) {
            // A link the model keeps but does not apply. Not a diagnostic: it
            // is a stored amount waiting to be switched back on.
            if (!link.enabled)
                continue;

            pending_.push_back(PendingLink{ParamSourceRef{ParamSourceRef::Kind::Modifier, index},
                                           toString(key), link.target, link.amount, link.bipolar});
        }
    }
}

void Builder::allocateDevice(const Node& node) {
    if (node.device == nullptr)
        return;

    const auto& device = *node.device;
    const auto& scope = node.scope;

    // A device's parameters are one index space: its own, plus whatever its
    // wrapper injected, which addresses the same slots.
    int highest = -1;
    const auto scan = [&](const std::vector<magda::ParameterInfo>& list) {
        for (const auto& info : list) {
            if (info.paramIndex < 0 || info.paramIndex > kMaxDeviceParamIndex) {
                diagnose(toString(scope) + ": parameter index " + std::to_string(info.paramIndex) +
                         " is out of range and is ignored");
                continue;
            }
            highest = std::max(highest, info.paramIndex);
        }
    };
    scan(device.parameters);
    scan(device.wrapperParameters);

    const auto find = [&](int index) -> const magda::ParameterInfo* {
        for (const auto* list : {&device.parameters, &device.wrapperParameters})
            for (const auto& info : *list)
                if (info.paramIndex == index)
                    return &info;
        return nullptr;
    };

    // Every index from zero, so a device reads its own parameters by the index
    // it declared them at rather than by where they landed in the table. A gap
    // is a slot nothing declared and nothing reads.
    const auto first = static_cast<ParamId>(table_.keys.size());
    int gaps = 0;
    for (int index = 0; index <= highest; ++index) {
        ParamKey key = scope;
        key.kind = ParamKey::Kind::DeviceParam;
        key.index = index;

        const auto* info = find(index);
        if (info == nullptr) {
            ++gaps;
            add(key, ParamSpec{}, 0.0f);
            continue;
        }

        // The model's own inverse, rather than a straight read of the range:
        // an external plugin's stored value and its display range are not the
        // same number, and only ParameterUtils knows which parameters those
        // are.
        const auto normalised = magda::ParameterUtils::modelToNormalizedValue(
            magda::ParameterModelValue{info->currentValue}, *info);
        add(key, paramSpecFrom(*info), normalised.value);
    }

    if (gaps > 0)
        diagnose(toString(scope) + ": parameter indices are not contiguous, so " +
                 std::to_string(gaps) + " slot(s) of the window belong to no parameter");

    if (highest >= 0)
        table_.deviceWindows.emplace(scope.device, ParamTable::DeviceWindow{first, highest + 1});
}

void Builder::walkElements(const std::vector<magda::ChainElement>& elements, magda::TrackId trackId,
                           magda::RackId rackId) {
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            const auto& device = magda::getDevice(element);
            Node node;
            node.scope.scope = ParamKey::Scope::Device;
            node.scope.trackId = trackId;
            node.scope.rackId = rackId;
            node.scope.device = DeviceKey{ChainSegment::Fx, device.id};
            node.macros = &device.macros;
            node.mods = &device.mods;
            node.device = &device;
            nodes_.push_back(node);
        } else if (magda::isRack(element)) {
            walkRack(magda::getRack(element), trackId);
        }
    }
}

void Builder::walkFlat(const std::vector<magda::PostFxChainElement>& elements,
                       magda::TrackId trackId, ChainSegment segment) {
    for (const auto& element : elements) {
        Node node;
        node.scope.scope = ParamKey::Scope::Device;
        node.scope.trackId = trackId;
        node.scope.device = DeviceKey{segment, element.device.id};
        node.macros = &element.device.macros;
        node.mods = &element.device.mods;
        node.device = &element.device;
        nodes_.push_back(node);
    }
}

void Builder::walkRack(const magda::RackInfo& rack, magda::TrackId trackId) {
    Node node;
    node.scope.scope = ParamKey::Scope::Rack;
    node.scope.trackId = trackId;
    node.scope.rackId = rack.id;
    node.macros = &rack.macros;
    node.mods = &rack.mods;
    nodes_.push_back(node);

    for (const auto& chain : rack.chains)
        walkElements(chain.elements, trackId, rack.id);
}

void Builder::walkTrack(const magda::TrackInfo& track) {
    Node node;
    node.scope.scope = ParamKey::Scope::Track;
    node.scope.trackId = track.id;
    node.macros = &track.macros;
    node.mods = &track.mods;
    nodes_.push_back(node);

    walkElements(track.chain.fxChainElements, track.id, INVALID_RACK_ID);
    walkFlat(track.chain.postFxChainElements, track.id, ChainSegment::PostFx);
    walkFlat(track.chain.mixerAnalysisElements, track.id, ChainSegment::MixerAnalysis);
}

void Builder::collectTargets() {
    const auto note = [&](const magda::ControlTarget& target) {
        if (const auto key = paramKeyFor(target))
            targeted_.insert(*key);
    };

    for (const auto& node : nodes_) {
        if (node.macros != nullptr)
            for (const auto& macro : *node.macros)
                for (const auto& link : macro.links)
                    note(link.target);

        if (node.mods != nullptr)
            for (const auto& mod : *node.mods)
                for (const auto& link : mod.links)
                    if (link.enabled)
                        note(link.target);
    }
}

void Builder::allocate() {
    for (const auto& node : nodes_) {
        // Devices first within a node, so a device's window is contiguous.
        allocateDevice(node);
        allocateMacros(node);
        allocateMods(node);
    }
}

void Builder::resolveLinks() {
    for (const auto& pending : pending_) {
        const auto key = paramKeyFor(pending.target);
        if (!key.has_value()) {
            // A target this table does not carry rather than a target that is
            // wrong. Both mixer values and the tempo are resolved elsewhere;
            // what is missing is the crossing, and it is named so a project
            // that relies on one is a report rather than a silence.
            diagnose(pending.sourceName + ": links to " +
                     std::string(magda::toString(pending.target.kind)) +
                     ", which the parameter table does not carry yet");
            continue;
        }

        if (key->kind == ParamKey::Kind::ModParam) {
            diagnose(pending.sourceName + ": links to " + toString(*key) +
                     ", and a modifier's own parameters arrive with the engines that define "
                     "them (#2119)");
            continue;
        }

        const auto id = table_.find(*key);
        if (id == INVALID_PARAM_ID) {
            diagnose(pending.sourceName + ": links to " + toString(*key) +
                     ", which the project does not have");
            continue;
        }

        if (!table_.specs[static_cast<std::size_t>(id)].modulatable) {
            diagnose(pending.sourceName + ": links to " + toString(*key) +
                     ", which takes no modulation");
            continue;
        }

        perParam_[static_cast<std::size_t>(id)].push_back(
            ParamLink{pending.source, pending.amount, pending.bipolar});
    }
}

void Builder::orderAndBreakCycles() {
    const auto count = static_cast<std::size_t>(table_.size());

    // What each parameter waits for, and who waits for it. A modifier source
    // contributes the modifier's own parameters, which is nothing until they
    // exist; the loop is written for what it will be rather than for what it is
    // today, because the order it produces is the thing that has to stay right.
    std::vector<int> waitingOn(count, 0);
    std::vector<std::vector<ParamId>> consumers(count);

    for (std::size_t target = 0; target < count; ++target) {
        for (const auto& link : perParam_[target]) {
            if (link.source.kind != ParamSourceRef::Kind::Parameter)
                continue;
            const auto source = static_cast<std::size_t>(link.source.index);
            if (source >= count)
                continue;

            consumers[source].push_back(static_cast<ParamId>(target));
            ++waitingOn[target];
        }
    }

    // Ascending id among everything that is ready, so the order is a property
    // of the model rather than of how the container happened to iterate.
    std::priority_queue<ParamId, std::vector<ParamId>, std::greater<>> ready;
    for (std::size_t i = 0; i < count; ++i)
        if (waitingOn[i] == 0)
            ready.push(static_cast<ParamId>(i));

    table_.order.reserve(count);
    while (!ready.empty()) {
        const auto id = ready.top();
        ready.pop();
        table_.order.push_back(id);

        for (const auto consumer : consumers[static_cast<std::size_t>(id)])
            if (--waitingOn[static_cast<std::size_t>(consumer)] == 0)
                ready.push(consumer);
    }

    if (table_.order.size() == count)
        return;

    // Whatever is left waits on something that is itself waiting: a cycle, and
    // there is no order that satisfies it. The links that close it are the ones
    // between the survivors, and dropping them is the only reading that leaves
    // every parameter with a value. A parameter is worth more than the link
    // that made it impossible.
    std::vector<char> stuck(count, 0);
    for (std::size_t i = 0; i < count; ++i)
        stuck[i] = waitingOn[i] > 0 ? 1 : 0;

    for (std::size_t target = 0; target < count; ++target) {
        if (stuck[target] == 0)
            continue;

        auto& links = perParam_[target];
        const auto removed = std::remove_if(links.begin(), links.end(), [&](const ParamLink& link) {
            return link.source.kind == ParamSourceRef::Kind::Parameter &&
                   static_cast<std::size_t>(link.source.index) < count &&
                   stuck[static_cast<std::size_t>(link.source.index)] != 0;
        });

        if (removed != links.end()) {
            diagnose(toString(table_.keys[target]) +
                     ": part of a modulation cycle; the links inside it are dropped");
            links.erase(removed, links.end());
        }

        table_.order.push_back(static_cast<ParamId>(target));
    }
}

void Builder::flattenLinks() {
    table_.linkOffsets.reserve(perParam_.size() + 1);
    table_.linkOffsets.push_back(0);

    for (const auto& links : perParam_) {
        table_.links.insert(table_.links.end(), links.begin(), links.end());
        table_.linkOffsets.push_back(static_cast<int>(table_.links.size()));
        table_.maxLinksPerParam = std::max(table_.maxLinksPerParam, static_cast<int>(links.size()));
    }
}

ParamTable Builder::run(const RenderPlan& plan, const std::vector<magda::TrackInfo>& tracks,
                        const magda::TrackInfo& master) {
    table_.planFingerprint = planFingerprint(plan);

    for (const auto& track : tracks)
        walkTrack(track);
    walkTrack(master);

    collectTargets();
    allocate();
    resolveLinks();
    orderAndBreakCycles();
    flattenLinks();

    return std::move(table_);
}

}  // namespace

ParamTable compileParamTable(const RenderPlan& plan, const std::vector<magda::TrackInfo>& tracks,
                             const magda::TrackInfo& master) {
    return Builder{}.run(plan, tracks, master);
}

}  // namespace magda::engine
