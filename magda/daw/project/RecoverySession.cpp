#include "RecoverySession.hpp"

#include <algorithm>
#if JUCE_WINDOWS
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/file.h>
    #include <unistd.h>
#endif

namespace magda {

// Lock the same file on every platform. JUCE's macOS InterProcessLock can fall
// back to a second path when the first is busy, which cannot establish ownership.
class RecoverySessionLock {
  public:
    explicit RecoverySessionLock(const juce::File& directory) {
        const auto file = directory.getChildFile(".lock");
#if JUCE_WINDOWS
        handle_ =
            CreateFileW(file.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        OVERLAPPED overlap{};
        if (handle_ != INVALID_HANDLE_VALUE &&
            !LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0,
                        &overlap)) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        descriptor_ =
            ::open(file.getFullPathName().toRawUTF8(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (descriptor_ >= 0 && ::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            ::close(descriptor_);
            descriptor_ = -1;
        }
#endif
    }
    ~RecoverySessionLock() {
#if JUCE_WINDOWS
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
#else
        if (descriptor_ >= 0)
            ::close(descriptor_);
#endif
    }
    explicit operator bool() const {
#if JUCE_WINDOWS
        return handle_ != INVALID_HANDLE_VALUE;
#else
        return descriptor_ >= 0;
#endif
    }
    RecoverySessionLock(const RecoverySessionLock&) = delete;
    RecoverySessionLock& operator=(const RecoverySessionLock&) = delete;

  private:
#if JUCE_WINDOWS
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

namespace {
bool atomicText(const juce::File& file, const juce::String& text) {
    juce::TemporaryFile temporary(file);
    {
        juce::FileOutputStream stream(temporary.getFile());
        if (stream.failedToOpen() || !stream.writeText(text, false, false, nullptr))
            return false;
        stream.flush();
        if (stream.getStatus().failed())
            return false;
    }
    return temporary.overwriteTargetFileWithTemporary();
}
}  // namespace

RecoveryEntry RecoverySession::read(const juce::File& directory) {
    RecoveryEntry entry;
    entry.directory = directory;
    auto json = juce::JSON::parse(directory.getChildFile("session.json"));
    auto* object = json.getDynamicObject();
    if (object == nullptr)
        return entry;
    const auto filename = object->getProperty("snapshot").toString();
    if (filename.endsWith(".autosave") &&
        juce::Uuid(filename.upToLastOccurrenceOf(".autosave", false, false)).toString() +
                ".autosave" ==
            filename)
        entry.snapshot = directory.getChildFile(filename);
    entry.build = object->getProperty("build").toString();
    entry.version = object->getProperty("version").toString();
    entry.name = object->getProperty("name").toString();
    entry.originalFile = object->getProperty("originalFile").toString();
    entry.mediaDirectory = object->getProperty("mediaDirectory").toString();
    entry.created = juce::Time::fromISO8601(object->getProperty("created").toString());
    entry.edited = juce::Time::fromISO8601(object->getProperty("edited").toString());
    entry.saved = juce::Time::fromISO8601(object->getProperty("saved").toString());
    entry.tracks = object->getProperty("tracks");
    entry.clips = object->getProperty("clips");
    entry.running = object->getProperty("running");
    entry.offered = object->getProperty("offered");
    return entry;
}

bool RecoverySession::save(const RecoveryEntry& entry) {
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("snapshot", entry.snapshot.getFileName());
    object->setProperty("build", entry.build);
    object->setProperty("version", entry.version);
    object->setProperty("name", entry.name);
    object->setProperty("originalFile", entry.originalFile);
    object->setProperty("mediaDirectory", entry.mediaDirectory);
    object->setProperty("created", entry.created.toISO8601(true));
    object->setProperty("edited", entry.edited.toISO8601(true));
    object->setProperty("saved", entry.saved.toISO8601(true));
    object->setProperty("tracks", entry.tracks);
    object->setProperty("clips", entry.clips);
    object->setProperty("running", entry.running);
    object->setProperty("offered", entry.offered);
    return atomicText(entry.directory.getChildFile("session.json"),
                      juce::JSON::toString(juce::var(object.release())));
}

RecoverySession::RecoverySession(const juce::File& root, juce::String build)
    : root_(root),
      directory_(root.getChildFile(juce::Uuid().toString())),
      build_(std::move(build)) {
    if (!directory_.createDirectory())
        return;
    lock_ = std::make_unique<RecoverySessionLock>(directory_);
    if (!*lock_) {
        lock_.reset();
        return;
    }
    const auto latest =
        root_.getChildFile("latest-" + juce::String::toHexString(build_.hashCode64()));
    const auto previousName = latest.loadFileAsString().trim();
    if (previousName.isNotEmpty() && juce::Uuid(previousName).toString() == previousName)
        previous_ = root_.getChildFile(previousName);
    RecoveryEntry entry;
    entry.directory = directory_;
    entry.build = build_;
    entry.running = true;
    ready_ = save(entry) && atomicText(latest, directory_.getFileName());
}

// Destruction alone is not a clean quit; the OS also releases this lock on a crash.
RecoverySession::~RecoverySession() = default;

juce::File RecoverySession::snapshot() const {
    return read(directory_).snapshot;
}

std::vector<RecoveryEntry> RecoverySession::entries(bool includeLive) const {
    std::vector<RecoveryEntry> result;
    for (const auto& directory : root_.findChildFiles(juce::File::findDirectories, false)) {
        RecoverySessionLock guard(directory);
        if (!includeLive && !guard)
            continue;
        auto entry = read(directory);
        if (entry.snapshot.existsAsFile())
            result.push_back(std::move(entry));
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.saved > b.saved; });
    return result;
}

RecoveryEntry RecoverySession::startupCandidate() const {
    for (const auto& entry : entries())
        if (entry.directory == previous_ && entry.build == build_ && entry.running &&
            !entry.offered && entry.originalFile.isEmpty())
            return entry;
    return {};
}

RecoveryEntry RecoverySession::projectCandidate(const juce::File& project,
                                                bool includeOffered) const {
    for (const auto& entry : entries())
        if (entry.build == build_ && entry.running && (includeOffered || !entry.offered) &&
            entry.originalFile == project.getFullPathName() &&
            entry.saved > project.getLastModificationTime())
            return entry;
    return {};
}

bool RecoverySession::write(RecoveryEntry entry,
                            const std::function<bool(const juce::File&)>& writer) {
    if (!ready_ || !directory_.isDirectory())
        return false;
    const auto oldSnapshot = snapshot();
    entry.directory = directory_;
    entry.build = build_;
    entry.running = true;
    entry.offered = false;
    entry.snapshot = directory_.getChildFile(juce::Uuid().toString() + ".autosave");
    if (!writer(entry.snapshot) || !save(entry)) {
        entry.snapshot.deleteFile();
        return false;
    }
    if (oldSnapshot != juce::File())
        oldSnapshot.deleteFile();
    return true;
}

bool RecoverySession::belongsToStore(const RecoveryEntry& entry) const {
    return entry.directory.getParentDirectory() == root_ && entry.directory != directory_ &&
           entry.snapshot.getParentDirectory() == entry.directory;
}

bool RecoverySession::adopt(const RecoveryEntry& entry) {
    if (!belongsToStore(entry))
        return false;
    RecoverySessionLock guard(entry.directory);
    if (!guard)
        return false;
    auto current = read(entry.directory);
    if (current.snapshot != entry.snapshot)
        return false;
    current.saved = juce::Time::getCurrentTime();
    if (!write(current, [&](const juce::File& destination) {
            return entry.snapshot.copyFileTo(destination);
        }))
        return false;
    // Ownership of the media moves with the snapshot, so never delete it here.
    entry.directory.deleteRecursively();
    return true;
}

bool RecoverySession::markOffered(const RecoveryEntry& entry) {
    if (!belongsToStore(entry))
        return false;
    RecoverySessionLock guard(entry.directory);
    if (!guard)
        return false;
    auto current = read(entry.directory);
    if (current.snapshot != entry.snapshot || current.offered)
        return false;
    current.offered = true;
    return save(current);
}

bool RecoverySession::discard(const RecoveryEntry& entry) {
    if (!belongsToStore(entry))
        return false;
    RecoverySessionLock guard(entry.directory);
    return guard && read(entry.directory).snapshot == entry.snapshot &&
           entry.directory.deleteRecursively();
}

void RecoverySession::clear() {
    auto entry = read(directory_);
    const auto oldSnapshot = entry.snapshot;
    entry.snapshot = juce::File();
    entry.mediaDirectory.clear();
    if (save(entry) && oldSnapshot != juce::File())
        oldSnapshot.deleteFile();
}

void RecoverySession::finish(const juce::File& copiedRoot) {
    if (copiedRoot != juce::File() && copiedRoot != root_) {
        const auto copiedDirectory = copiedRoot.getChildFile(directory_.getFileName());
        RecoverySessionLock guard(copiedDirectory);
        if (guard && copiedDirectory.isDirectory()) {
            auto copied = read(copiedDirectory);
            copied.running = false;
            copied.offered = true;
            if (save(copied))
                copiedDirectory.deleteRecursively();
        }
    }
    auto entry = read(directory_);
    entry.running = false;
    entry.offered = true;
    if (save(entry))
        clear();
    ready_ = false;
}

void RecoverySession::pruneEmptySessions(juce::Time cutoff) {
    for (const auto& directory : root_.findChildFiles(juce::File::findDirectories, false)) {
        RecoverySessionLock guard(directory);
        if (guard && directory.getLastModificationTime() < cutoff &&
            !read(directory).snapshot.existsAsFile())
            directory.deleteRecursively();
    }
}

}  // namespace magda
