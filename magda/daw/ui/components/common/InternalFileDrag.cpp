#include "ui/components/common/InternalFileDrag.hpp"

namespace magda::dnd {

namespace {
constexpr const char* kTypeKey = "type";
constexpr const char* kPathsKey = "paths";
constexpr const char* kFilesType = "files";
}  // namespace

juce::var makeFilesDragDescription(const juce::StringArray& paths) {
    juce::Array<juce::var> pathArray;
    for (const auto& p : paths)
        pathArray.add(p);

    auto* obj = new juce::DynamicObject();
    obj->setProperty(kTypeKey, juce::var(kFilesType));
    obj->setProperty(kPathsKey, juce::var(pathArray));
    return juce::var(obj);  // ref-counted from here, so no leak if it goes unused
}

void startFilesDrag(juce::Component* sourceComponent, const juce::StringArray& paths,
                    const juce::ScaledImage& dragImage) {
    if (sourceComponent == nullptr || paths.isEmpty())
        return;

#if JUCE_LINUX
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(sourceComponent)) {
        // The internal route returns immediately, so a caller sitting in
        // mouseDrag would start a fresh drag on every subsequent drag event.
        // The OS route could not do this: it blocks on a modal native loop
        // until the drop finishes.
        if (container->isDragAndDropActive())
            return;

        container->startDragging(makeFilesDragDescription(paths), sourceComponent, dragImage);
    }
#else
    juce::ignoreUnused(dragImage);
    juce::DragAndDropContainer::performExternalDragDropOfFiles(paths, /*canMoveFiles=*/false,
                                                               sourceComponent);
#endif
}

bool isFilesDrag(const juce::var& description) {
    if (auto* obj = description.getDynamicObject())
        return obj->getProperty(kTypeKey).toString() == kFilesType;
    return false;
}

juce::StringArray filesDragPaths(const juce::var& description) {
    juce::StringArray paths;
    if (auto* obj = description.getDynamicObject()) {
        if (obj->getProperty(kTypeKey).toString() == kFilesType) {
            if (auto* arr = obj->getProperty(kPathsKey).getArray()) {
                for (const auto& v : *arr)
                    paths.add(v.toString());
            }
        }
    }
    return paths;
}

bool acceptsFilesDrag(juce::FileDragAndDropTarget& target, const SourceDetails& details) {
    return isFilesDrag(details.description) &&
           target.isInterestedInFileDrag(filesDragPaths(details.description));
}

bool forwardFilesDragEnter(juce::FileDragAndDropTarget& target, const SourceDetails& details) {
    if (!isFilesDrag(details.description))
        return false;

    target.fileDragEnter(filesDragPaths(details.description), details.localPosition.getX(),
                         details.localPosition.getY());
    return true;
}

bool forwardFilesDragMove(juce::FileDragAndDropTarget& target, const SourceDetails& details) {
    if (!isFilesDrag(details.description))
        return false;

    target.fileDragMove(filesDragPaths(details.description), details.localPosition.getX(),
                        details.localPosition.getY());
    return true;
}

bool forwardFilesDragExit(juce::FileDragAndDropTarget& target, const SourceDetails& details) {
    if (!isFilesDrag(details.description))
        return false;

    target.fileDragExit(filesDragPaths(details.description));
    return true;
}

bool forwardFilesDrop(juce::FileDragAndDropTarget& target, const SourceDetails& details) {
    if (!isFilesDrag(details.description))
        return false;

    target.filesDropped(filesDragPaths(details.description), details.localPosition.getX(),
                        details.localPosition.getY());
    return true;
}

}  // namespace magda::dnd
