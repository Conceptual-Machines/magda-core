#include "DrumkitManager.hpp"

#include "AppPaths.hpp"
#include "version.hpp"

namespace magda {

namespace {
constexpr const char* kDrumkitExtension = ".mdgk";
constexpr const char* kDrumkitKind = "drumkit";

juce::String sanitizeName(const juce::String& name) {
    auto sanitized = juce::File::createLegalFileName(name.trim());
    if (sanitized.isEmpty())
        sanitized = "Untitled";
    return sanitized;
}

// Mirrors ControllerProfileRegistry's lookup: bundle Resources first, then the
// portable layout next to the binary, then a dev-tree fallback.
juce::File findBundledDrumkitsDirectory() {
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

    juce::Array<juce::File> candidates;
#if JUCE_MAC
    candidates.add(appFile.getChildFile("Contents/Resources/drumkits"));
#endif
#if JUCE_LINUX
    if (auto real = juce::File("/proc/self/exe").getLinkedTarget(); real.exists())
        candidates.add(real.getParentDirectory().getChildFile("drumkits"));
#endif
    candidates.add(appFile.getParentDirectory().getChildFile("drumkits"));

    auto walk = appFile.getParentDirectory();
    for (int i = 0; i < 8 && walk.exists(); ++i) {
        auto maybe = walk.getChildFile("resources").getChildFile("drumkits");
        if (maybe.isDirectory()) {
            candidates.add(maybe);
            break;
        }
        walk = walk.getParentDirectory();
    }

    for (const auto& c : candidates)
        if (c.isDirectory())
            return c;
    return {};
}
}  // namespace

DrumkitManager& DrumkitManager::getInstance() {
    static DrumkitManager instance;
    return instance;
}

namespace {
// True iff the on-disk drumkit at `file` carries the stock sentinel — i.e.
// magdaVersion == "stock". User-saved kits use the actual MAGDA_VERSION
// string in that field (see saveDrumkit below), so the two are
// distinguishable. Used to decide whether refreshing from the bundle is safe.
bool isStockKitFile(const juce::File& file) {
    if (!file.existsAsFile())
        return false;
    auto root = juce::JSON::parse(file.loadFileAsString());
    auto* obj = root.getDynamicObject();
    if (obj == nullptr)
        return false;
    return obj->getProperty("magdaVersion").toString() == "stock";
}
}  // namespace

DrumkitManager::DrumkitManager() {
    auto userDir = getDrumkitsDirectory();
    userDir.createDirectory();
    // Sync bundled stock kits into the user dir on every launch. Files marked
    // magdaVersion=="stock" are reference data — if the bundled copy changes
    // (e.g. a kit file fix shipped in a new build), the user copy is refreshed
    // automatically. Files without the stock sentinel are user-saved (whether
    // via Save-as-drumkit or by editing a stock file enough that we'd want to
    // preserve it) and left alone.
    if (auto bundled = findBundledDrumkitsDirectory(); bundled.isDirectory()) {
        auto stock = bundled.findChildFiles(juce::File::findFiles, false,
                                            juce::String("*") + kDrumkitExtension);
        for (const auto& src : stock) {
            auto dst = userDir.getChildFile(src.getFileName());
            if (!dst.existsAsFile() || isStockKitFile(dst))
                src.copyFileTo(dst);
        }
    }
}

juce::File DrumkitManager::getDrumkitsDirectory() const {
    return magda::paths::drumkitsDir();
}

bool DrumkitManager::saveDrumkit(const juce::String& name, const std::vector<Row>& rows) {
    if (name.trim().isEmpty())
        return false;

    juce::Array<juce::var> rowArray;
    for (const auto& r : rows) {
        if (r.label.isEmpty() && r.role.isEmpty())
            continue;
        auto* rowObj = new juce::DynamicObject();
        rowObj->setProperty("note", r.noteNumber);
        if (r.label.isNotEmpty())
            rowObj->setProperty("label", r.label);
        if (r.role.isNotEmpty())
            rowObj->setProperty("role", r.role);
        rowArray.add(juce::var(rowObj));
    }

    auto* payload = new juce::DynamicObject();
    payload->setProperty("rows", rowArray);

    auto* envelope = new juce::DynamicObject();
    envelope->setProperty("magdaVersion", juce::String(MAGDA_VERSION));
    envelope->setProperty("kind", juce::String(kDrumkitKind));
    envelope->setProperty("payload", juce::var(payload));

    auto json = juce::JSON::toString(juce::var(envelope), false);
    auto target = getDrumkitsDirectory().getChildFile(sanitizeName(name) + kDrumkitExtension);
    target.getParentDirectory().createDirectory();
    return target.replaceWithText(json);
}

std::vector<DrumkitManager::Row> DrumkitManager::loadDrumkit(const juce::String& name) const {
    std::vector<Row> rows;
    auto file = getDrumkitsDirectory().getChildFile(sanitizeName(name) + kDrumkitExtension);
    if (!file.existsAsFile())
        return rows;

    auto root = juce::JSON::parse(file.loadFileAsString());
    auto* obj = root.getDynamicObject();
    if (obj == nullptr || obj->getProperty("kind").toString() != kDrumkitKind)
        return rows;

    auto* payload = obj->getProperty("payload").getDynamicObject();
    if (payload == nullptr)
        return rows;

    auto rowsVar = payload->getProperty("rows");
    if (!rowsVar.isArray())
        return rows;

    for (const auto& rowVar : *rowsVar.getArray()) {
        auto* rowObj = rowVar.getDynamicObject();
        if (rowObj == nullptr || !rowObj->hasProperty("note"))
            continue;
        Row r;
        r.noteNumber = juce::jlimit(0, 127, static_cast<int>(rowObj->getProperty("note")));
        if (rowObj->hasProperty("label"))
            r.label = rowObj->getProperty("label").toString();
        if (rowObj->hasProperty("role"))
            r.role = rowObj->getProperty("role").toString();
        rows.push_back(std::move(r));
    }
    return rows;
}

std::vector<DrumkitManager::Drumkit> DrumkitManager::listDrumkits() const {
    std::vector<Drumkit> out;
    auto dir = getDrumkitsDirectory();
    if (!dir.isDirectory())
        return out;
    auto files =
        dir.findChildFiles(juce::File::findFiles, false, juce::String("*") + kDrumkitExtension);
    files.sort();
    out.reserve(static_cast<size_t>(files.size()));
    for (const auto& f : files)
        out.push_back({f.getFileNameWithoutExtension(), f});
    return out;
}

bool DrumkitManager::deleteDrumkit(const juce::String& name) {
    auto file = getDrumkitsDirectory().getChildFile(sanitizeName(name) + kDrumkitExtension);
    if (!file.existsAsFile())
        return false;
    return file.deleteFile();
}

}  // namespace magda
