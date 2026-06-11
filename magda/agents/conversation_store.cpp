#include "conversation_store.hpp"

#include <algorithm>

namespace magda {

namespace {
/// Collapse whitespace runs and clip to maxChars so one verbose turn can't
/// dominate the rendered block. Appends an ellipsis marker when truncated.
std::string compact(const std::string& text, int maxChars) {
    std::string out;
    out.reserve(text.size());
    bool inSpace = false;
    for (char c : text) {
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
        if (c == ' ') {
            if (inSpace)
                continue;
            inSpace = true;
        } else {
            inSpace = false;
        }
        out.push_back(c);
    }
    // Trim trailing space left by the collapse.
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    if (maxChars > 0 && static_cast<int>(out.size()) > maxChars)
        out = out.substr(0, static_cast<size_t>(maxChars)) + " [...]";
    return out;
}
}  // namespace

void ConversationStore::record(Channel channel, const std::string& user,
                               const std::string& assistant) {
    if (user.empty() && assistant.empty())
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    auto& thread = threads_[channel];
    thread.push_back({user, assistant});
    while (thread.size() > kMaxStoredTurns)
        thread.pop_front();
}

std::string ConversationStore::render(Channel channel, int maxTurns) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = threads_.find(channel);
    if (it == threads_.end() || it->second.empty() || maxTurns <= 0)
        return {};

    const auto& thread = it->second;
    size_t count = std::min(static_cast<size_t>(maxTurns), thread.size());
    size_t start = thread.size() - count;

    std::string out = "Conversation so far (most recent last):\n";
    for (size_t i = start; i < thread.size(); ++i) {
        const auto& turn = thread[i];
        if (!turn.user.empty())
            out += "User: " + compact(turn.user, kMaxEntryChars) + "\n";
        if (!turn.assistant.empty())
            out += "You: " + compact(turn.assistant, kMaxEntryChars) + "\n";
    }
    return out;
}

void ConversationStore::clear(Channel channel) {
    std::lock_guard<std::mutex> lock(mutex_);
    threads_.erase(channel);
}

void ConversationStore::clearAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    threads_.clear();
}

}  // namespace magda
