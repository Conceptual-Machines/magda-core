#include "ChainNodePathDrag.hpp"

namespace magda::daw::ui {

namespace {

juce::String pathKey(int index) {
    return index < 0 ? juce::String("path") : "path" + juce::String(index);
}

}  // namespace

void writeChainNodePathToDragInfo(juce::DynamicObject& obj, const ChainNodePath& path, int index) {
    obj.setProperty(pathKey(index), toVar(path));
}

std::optional<ChainNodePath> readChainNodePathFromDragInfo(const juce::DynamicObject& obj,
                                                           int index) {
    const auto key = pathKey(index);
    if (!obj.hasProperty(key))
        return std::nullopt;

    ChainNodePath path;
    if (!fromVar(obj.getProperty(key), path))
        return std::nullopt;
    if (!path.isValid())
        return std::nullopt;

    // Post-fx and mixer-analysis are flat, rail-managed sections with no
    // reorder or reparent semantics, so the drop handlers have never accepted
    // them. The previous decoders enforced this by refusing to decode a Segment
    // step at all; state it directly instead.
    if (path.isPostFx() || path.isMixerAnalysis())
        return std::nullopt;

    return path;
}

std::vector<ChainNodePath> readChainNodePathsFromDragInfo(const juce::DynamicObject& obj) {
    std::vector<ChainNodePath> paths;

    const auto count = static_cast<int>(obj.getProperty("pathCount"));
    for (int i = 0; i < count; ++i) {
        if (auto path = readChainNodePathFromDragInfo(obj, i))
            paths.push_back(*path);
    }

    // Single-node drags from older call sites carry only the unsuffixed copy.
    if (paths.empty()) {
        if (auto path = readChainNodePathFromDragInfo(obj))
            paths.push_back(*path);
    }

    return paths;
}

}  // namespace magda::daw::ui
