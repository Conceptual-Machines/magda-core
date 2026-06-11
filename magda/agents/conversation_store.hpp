#pragma once

#include <deque>
#include <map>
#include <mutex>
#include <string>

namespace magda {

/**
 * @brief Centralised rolling conversation memory for the AI console.
 *
 * The mixer agent already expected a "prior context" block (MixAnalysisAgent::
 * Input::priorContext) so it could build on earlier advice instead of repeating
 * it. This generalises that idea: one conversation thread per console view, so
 * the arrangement and mixing agents both remember what was said earlier in the
 * same view.
 *
 * The console records a completed exchange on the message thread and renders the
 * thread on the request background thread, so all access is mutex-guarded.
 */
class ConversationStore {
  public:
    /// One thread per console view (#1402). The console maps ViewMode -> Channel.
    enum class Channel { Arrangement, Mixing, Session };

    struct Turn {
        std::string user;
        std::string assistant;
    };

    /// Record a completed exchange. Empty user+assistant pairs are ignored.
    void record(Channel channel, const std::string& user, const std::string& assistant);

    /**
     * Render the most recent exchanges as a compact context block suitable for
     * prepending to an agent prompt, or "" when the thread is empty. Each entry
     * is truncated so a long transcript can't blow the token budget.
     */
    std::string render(Channel channel, int maxTurns = kDefaultRenderTurns) const;

    /// Drop a single thread (e.g. the user cleared the chat for that view).
    void clear(Channel channel);

    /// Drop every thread (e.g. a different project was opened).
    void clearAll();

  private:
    static constexpr int kDefaultRenderTurns = 6;
    // Keep a little more than we render so trimming is cheap and stable.
    static constexpr size_t kMaxStoredTurns = 16;
    // Per-entry cap when rendering, in characters — keeps the block bounded.
    static constexpr int kMaxEntryChars = 600;

    mutable std::mutex mutex_;
    std::map<Channel, std::deque<Turn>> threads_;
};

}  // namespace magda
