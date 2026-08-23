#include "ConnectionsDialog.hpp"

#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

// For the AppImage-safe /proc/self/exe lookup in RemoteApiPage::bridgeExecutable.
#if !JUCE_WINDOWS && !JUCE_MAC
    #include <unistd.h>

    #include <climits>
#endif

#include "../../api/remote_api_host.hpp"
#include "../../api/remote_audit.hpp"
#include "../../api/remote_clients.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"
#include "core/Config.hpp"
#include "core/StringTable.hpp"
#include "core/TechnicalText.hpp"
#include "osc_app.hpp"

namespace magda {

namespace {

void styleLabel(juce::Label& label, float size = 12.0f) {
    label.setFont(FontManager::getInstance().getUIFont(size));
    label.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    label.setJustificationType(juce::Justification::centredLeft);
}

/**
 * @brief One sentence, wrapped into `//` lines for a JSON snippet the user pastes.
 *
 * The English text used to be three hand-wrapped comment lines in the source.
 * A translator gets one sentence and no way to know where the line breaks
 * belong, so the wrapping happens here instead. A language that does not put
 * spaces between words falls out as a single long line, which is still valid
 * where this lands.
 */
juce::String commentBlock(const juce::String& sentence) {
    constexpr int kWrapColumn = 68;

    juce::String out;
    juce::String line;
    // Split on any whitespace, not just spaces: a translation that arrives with
    // its own line breaks would otherwise put one inside a token and emit a
    // continuation line with no `//` in front of it, which is no longer JSON.
    for (const auto& word : juce::StringArray::fromTokens(sentence, " \n\t", {})) {
        if (word.isEmpty())
            continue;
        if (line.isNotEmpty() && line.length() + 1 + word.length() > kWrapColumn) {
            out << "// " << line << "\n";
            line.clear();
        }
        line << (line.isEmpty() ? "" : " ") << word;
    }
    if (line.isNotEmpty())
        out << "// " << line << "\n";
    return out;
}

void styleReadOnlyEditor(juce::TextEditor& ed) {
    ed.setFont(FontManager::getInstance().getUIFont(12.0f));
    ed.setColour(juce::TextEditor::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    ed.setColour(juce::TextEditor::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    ed.setColour(juce::TextEditor::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
}

}  // namespace
// ============================================================================
// TransportPage — the shape both remote transports share (#2142)
//
// MCP and the WebSocket are two listeners, each with its own port, its own
// credential, and its own switch. What a user does with either is the same
// though: turn it on, see whether it came up, copy the thing a client needs,
// and occasionally throw the credential away. So the widgets and the layout
// live here and the two subclasses supply only what differs — the status
// sentence and the text in the copyable field.
//
// Everything reads live state through `activeHost()` on a one-second tick
// rather than on notifications. Starting a listener is not instantaneous and
// the port is only known once bound, so a slow poll keeps the status honest
// without polling hard at something that changes twice a session.
// ============================================================================

class TransportPage : public juce::Component, private juce::Timer {
  public:
    TransportPage(magda::remote::Transport transport, juce::String keyPrefix)
        : transport_(transport), keyPrefix_(std::move(keyPrefix)) {
        blurbLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        blurbLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        blurbLabel_.setJustificationType(juce::Justification::topLeft);
        blurbLabel_.setText(named("blurb"), juce::dontSendNotification);
        addAndMakeVisible(blurbLabel_);

        enableToggle_.setButtonText(
            named("enable").replace("{1}", magda::technicalText(magda::TechnicalTextToken::Api)));
        enableToggle_.setColour(juce::ToggleButton::textColourId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        enableToggle_.setColour(juce::ToggleButton::tickColourId,
                                DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY));
        // Applied on the spot: this toggle owns a socket, and one that leaves it
        // in the opposite state to what the checkbox shows is worse than no
        // feedback at all. Only this transport is touched — the other one keeps
        // listening, and its clients never notice.
        enableToggle_.onClick = [this]() { applyEnabled(enableToggle_.getToggleState()); };
        addAndMakeVisible(enableToggle_);

        styleLabel(statusLabel_, 11.0f);
        addAndMakeVisible(statusLabel_);

        // Rotation is not a routine action, so it asks first and says what it
        // costs. Every client connected over *this* transport was admitted by
        // the credential being thrown away, so every one of them is dropped — a
        // well-behaved client re-reads the discovery record and reconnects on
        // its own, but a user who pressed this by accident would otherwise
        // watch their session silently disconnect with no explanation.
        rotateButton_.setButtonText(tr("connections.rotate"));
        rotateButton_.onClick = [this]() { confirmRotate(); };
        addAndMakeVisible(rotateButton_);

        fieldCaption_.setText(named("field_caption"), juce::dontSendNotification);
        styleLabel(fieldCaption_);
        addAndMakeVisible(fieldCaption_);

        field_.setMultiLine(true, false);
        field_.setReadOnly(true);
        field_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                                    11.0f, juce::Font::plain)));
        styleReadOnlyEditor(field_);
        addAndMakeVisible(field_);

        copyButton_.setButtonText(tr("connections.copy"));
        copyButton_.onClick = [this]() {
            juce::SystemClipboard::copyTextToClipboard(field_.getText());
            copyButton_.setButtonText(tr("connections.copied"));
            // Back to "Copy" on the next refresh tick, so the confirmation is
            // visible without a second timer to manage.
            copiedAt_ = juce::Time::getCurrentTime();
        };
        addAndMakeVisible(copyButton_);

        hintLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
        hintLabel_.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_DIM));
        hintLabel_.setJustificationType(juce::Justification::topLeft);
        hintLabel_.setText(
            named("hint").replace("{1}", magda::technicalText(magda::TechnicalTextToken::Magda)),
            juce::dontSendNotification);
        addAndMakeVisible(hintLabel_);

        enableToggle_.setToggleState(enabledInConfig(), juce::dontSendNotification);
    }

    ~TransportPage() override {
        stopTimer();
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(12);
        const int rowH = 24;

        blurbLabel_.setBounds(bounds.removeFromTop(56));
        bounds.removeFromTop(8);

        auto toggleRow = bounds.removeFromTop(rowH);
        rotateButton_.setBounds(toggleRow.removeFromRight(110).reduced(0, 1));
        enableToggle_.setBounds(toggleRow);
        bounds.removeFromTop(2);
        statusLabel_.setBounds(bounds.removeFromTop(18));
        bounds.removeFromTop(12);

        auto captionRow = bounds.removeFromTop(rowH);
        copyButton_.setBounds(captionRow.removeFromRight(70).reduced(0, 1));
        fieldCaption_.setBounds(captionRow);
        bounds.removeFromTop(4);

        hintLabel_.setBounds(bounds.removeFromBottom(48));
        bounds.removeFromBottom(6);
        field_.setBounds(bounds);
    }

  protected:
    /**
     * @brief Start refreshing. The last thing every subclass constructor does.
     *
     * Not folded into this constructor: it drives `fieldText`, which is pure
     * virtual, and calling it before the subclass exists would dispatch to
     * nothing.
     */
    void beginRefreshing() {
        refresh();
        startTimer(1000);
    }

    /// What goes in the copyable field, and whether there is anything worth
    /// copying — false greys the button out rather than handing over a
    /// placeholder.
    virtual juce::String fieldText(bool running, bool& copyable) const = 0;

    /// This transport's port, or 0.
    int port() const {
        auto* host = magda::remote::activeHost();
        if (host == nullptr)
            return 0;
        return transport_ == magda::remote::Transport::WebSocket ? host->boundPort()
                                                                 : host->mcpPort();
    }

    /// A `connections.<prefix>.<leaf>` string.
    juce::String key(const char* leaf) const {
        return tr("connections." + keyPrefix_ + "." + leaf);
    }

    /// The same, with this transport's name substituted for `{0}`.
    ///
    /// "MCP" and "WebSocket" are product names, not words to translate, so no
    /// string spells either of them: they arrive through `technicalText` like
    /// every other technical term. A string that names a second such term uses
    /// `{1}` and substitutes it at its own call site, since which term that is
    /// differs between the two transports.
    juce::String named(const char* leaf) const {
        return key(leaf).replace("{0}", displayName());
    }

    /// This transport's name, as it is written in every language.
    juce::String displayName() const {
        return magda::technicalText(transport_ == magda::remote::Transport::WebSocket
                                        ? magda::TechnicalTextToken::WebSocket
                                        : magda::TechnicalTextToken::Mcp);
    }

  private:
    bool enabledInConfig() const {
        const auto& config = magda::Config::getInstance();
        return transport_ == magda::remote::Transport::WebSocket
                   ? config.getRemoteApiWebSocketEnabled()
                   : config.getRemoteApiMcpEnabled();
    }

    bool isRunning() const {
        auto* host = magda::remote::activeHost();
        return host != nullptr && host->isRunning(transport_);
    }

    void timerCallback() override {
        if (copiedAt_ != juce::Time() &&
            juce::Time::getCurrentTime().toMilliseconds() - copiedAt_.toMilliseconds() > 1200) {
            copyButton_.setButtonText(tr("connections.copy"));
            copiedAt_ = juce::Time();
        }
        refresh();
    }

    void applyEnabled(bool enabled) {
        auto& config = magda::Config::getInstance();
        if (transport_ == magda::remote::Transport::WebSocket)
            config.setRemoteApiWebSocketEnabled(enabled);
        else
            config.setRemoteApiMcpEnabled(enabled);
        config.save();

        // Absent in the headless CLI and in tests. The config is still written,
        // so the next launch honours it — there is simply no live listener here
        // to reconcile.
        if (auto* host = magda::remote::activeHost()) {
            if (enabled)
                host->startTransport(transport_);
            else
                host->stopTransport(transport_);
        }
        refresh();
    }

    /// How many clients this rotation would drop. Per transport, because that is
    /// all it drops.
    int connectedHere() const {
        auto* host = magda::remote::activeHost();
        if (host == nullptr)
            return 0;

        const juce::String name = transport_ == magda::remote::Transport::WebSocket
                                      ? magda::remote::TRANSPORT_WEBSOCKET
                                      : magda::remote::TRANSPORT_MCP;
        int count = 0;
        for (const auto& connection : host->clients().connections())
            if (connection.transport == name)
                ++count;
        return count;
    }

    void confirmRotate() {
        if (!isRunning())
            return;

        const auto transportName = displayName();
        const auto connected = connectedHere();
        auto message = tr("connections.rotate_message").replace("{0}", transportName);
        if (connected > 0) {
            // Singular and plural as separate keys rather than one string with a
            // count in it: a language whose plural rules are not English's has
            // no way to recover the distinction from "{0} clients".
            message += " " + (connected == 1 ? tr("connections.rotate_one_connected")
                                             : tr("connections.rotate_many_connected")
                                                   .replace("{0}", juce::String(connected)));
        }
        message += " " + tr("connections.rotate_reconnect");

        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::QuestionIcon)
                .withTitle(tr("connections.rotate_title").replace("{0}", transportName))
                .withMessage(message)
                .withButton(tr("connections.rotate_confirm"))
                .withButton(tr("connections.rotate_cancel")),
            [this](int result) {
                if (result != 1)
                    return;
                // Re-fetched rather than captured: this callback runs after the
                // dialog returns, and the host can be gone by then — the user
                // may have switched the transport off while the box was up.
                if (auto* live = magda::remote::activeHost())
                    live->rotateToken(transport_);
                refresh();
            });
    }

    void refresh() {
        const auto running = isRunning();
        const auto enabled = enabledInConfig();

        if (running) {
            statusLabel_.setText(key("status_listening").replace("{0}", juce::String(port())),
                                 juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId,
                                   DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY));
        } else if (enabled) {
            // Enabled and not listening. At startup that means "not up yet"; a
            // second later it means the port was taken, which is the one failure
            // a user can act on and the one they cannot otherwise see.
            statusLabel_.setText(key("status_enabled"), juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId,
                                   DarkTheme::getColour(DarkTheme::TEXT_DIM));
        } else {
            statusLabel_.setText(key("status_off"), juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId,
                                   DarkTheme::getColour(DarkTheme::TEXT_DIM));
        }

        bool copyable = false;
        const auto text = fieldText(running, copyable);
        if (text != field_.getText())
            field_.setText(text, juce::dontSendNotification);
        copyButton_.setEnabled(copyable);

        // Nothing to rotate when nothing is listening: there is no live
        // credential, and minting one would publish a record for a closed port.
        rotateButton_.setEnabled(running);
    }

    const magda::remote::Transport transport_;
    const juce::String keyPrefix_;

    juce::Label blurbLabel_;
    juce::ToggleButton enableToggle_;
    juce::TextButton rotateButton_;
    juce::Label statusLabel_;
    juce::Label fieldCaption_;
    juce::TextEditor field_;
    juce::TextButton copyButton_;
    juce::Label hintLabel_;
    juce::Time copiedAt_;
};

// ============================================================================
// McpPage — the endpoint an AI host connects to (#1856, #1858)
//
// The config snippet is composed from the running install rather than
// documented, because the one thing a user cannot look up is where their own
// copy of the bridge lives.
// ============================================================================

class McpPage : public TransportPage {
  public:
    McpPage() : TransportPage(magda::remote::Transport::Mcp, "mcp") {
        beginRefreshing();
    }

  private:
    /**
     * @brief Where this install's bridge lives.
     *
     * The same resolution `PluginScanCoordinator::getScannerExecutable()` uses,
     * and for the same reasons — this is the second helper binary MAGDA stages
     * beside itself, and "beside itself" means something different on each
     * platform.
     *
     * `paths::executableDir()` is deliberately not used: it is built on JUCE's
     * `currentApplicationFile`, which for a bundled Mac app returns the `.app`
     * itself, so its parent is `/Applications` rather than the directory holding
     * the binary. And on Linux that same call goes through `dladdr`, which
     * inside an AppImage yields the user's download folder rather than the
     * mounted `usr/bin`.
     *
     * A user pastes whatever this returns into their MCP host's config, so a
     * plausible-but-wrong path is the worst outcome available: it fails at
     * launch, inside another application, with no hint of where it came from.
     */
    static juce::File bridgeExecutable() {
        const auto appBundle = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

#if JUCE_MAC
        // Installed: /Applications/MAGDA.app/Contents/MacOS/magda-mcp
        if (const auto bundled = appBundle.getChildFile("Contents/MacOS/magda-mcp");
            bundled.existsAsFile())
            return bundled;
        // Unbundled, as a console or test build produces.
        return appBundle.getParentDirectory().getChildFile("magda-mcp");
#elif JUCE_WINDOWS
        return appBundle.getParentDirectory().getChildFile("magda-mcp.exe");
#else
        // Inside an AppImage there is no usable answer. The bridge is in there,
        // under a /tmp/.mount_… path — but that mount is torn down when MAGDA
        // exits and gets a different name every launch, so publishing it would
        // hand the user a command that stops existing the moment they quit. The
        // Linux release ships `magda-mcp` as its own asset for exactly this
        // reason, and the page says so instead of guessing.
        if (isRunningFromAppImage())
            return {};

        // Resolve the real binary rather than argv[0]: even outside an AppImage,
        // JUCE's lookup goes through dladdr, which on glibc yields argv[0].
        char buffer[PATH_MAX];
        if (const auto length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
            length > 0) {
            buffer[length] = '\0';
            const juce::File selfExe(juce::String::fromUTF8(buffer));
            if (const auto sibling = selfExe.getParentDirectory().getChildFile("magda-mcp");
                sibling.existsAsFile())
                return sibling;
        }
        return appBundle.getParentDirectory().getChildFile("magda-mcp");
#endif
    }

#if !JUCE_WINDOWS && !JUCE_MAC
    /// The type-2 AppImage runtime exports both of these; neither is set for an
    /// ordinary install.
    static bool isRunningFromAppImage() {
        return std::getenv("APPIMAGE") != nullptr || std::getenv("APPDIR") != nullptr;
    }
#endif

    juce::String fieldText(bool /*running*/, bool& copyable) const override {
        const auto bridge = bridgeExecutable();
        const auto haveBridge = bridge != juce::File() && bridge.existsAsFile();

        // Named as it will be typed, JSON-escaped so a Windows path's
        // backslashes survive being pasted.
        auto* server = new juce::DynamicObject();
        server->setProperty("command", haveBridge ? bridge.getFullPathName()
                                                  : juce::String("/path/to/magda-mcp"));
        auto* servers = new juce::DynamicObject();
        servers->setProperty("magda", juce::var(server));
        auto* root = new juce::DynamicObject();
        root->setProperty("mcpServers", juce::var(servers));

        auto snippet = juce::JSON::toString(juce::var(root), false);
        if (!haveBridge) {
            // A wrong path is worse than an obvious placeholder: it fails inside
            // another application, at launch, with nothing pointing back here.
            snippet =
                commentBlock(
                    key("bridge_missing")
                        .replace("{1}", magda::technicalText(magda::TechnicalTextToken::Magda))) +
                snippet;
        }

        // Nothing worth copying when the path is a placeholder.
        copyable = haveBridge;
        return snippet;
    }
};

// ============================================================================
// WebSocketPage — the socket a script or a control surface connects to
//
// The transport the remote API had first, and the one that had no page of its
// own while the feature was filed under AI: everything here was reachable only
// by finding the discovery record and reading it by hand.
//
// What it publishes is the URL, not the token. The token changes every run and
// is written to an owner-only file precisely so it never has to be copied
// around; a client reads it there. Showing it would invite pasting it into
// something that outlives the run it belongs to.
// ============================================================================

class WebSocketPage : public TransportPage {
  public:
    WebSocketPage() : TransportPage(magda::remote::Transport::WebSocket, "ws") {
        beginRefreshing();
    }

  private:
    juce::String fieldText(bool running, bool& copyable) const override {
        if (!running) {
            copyable = false;
            return key("url_pending");
        }

        copyable = true;
        juce::String text;
        text << "ws://127.0.0.1:" << juce::String(port()) << "/rpc?client=my-tool"
             << "\n\n";
        // The record, not the token: it is regenerated every run, so the path is
        // the durable thing and the credential inside it is not.
        if (auto* host = magda::remote::activeHost())
            text << key("token_file").replace("{0}", host->tokenFile().getFullPathName());
        return text;
    }
};

// ============================================================================
// RemoteClientsPage — who may do what, and what they have done (#1860)
//
// The permission model is only worth having if the user can see and change it,
// and this is the only place that is true. Two halves: a row per client MAGDA
// has heard from, with a checkbox per scope; and a tail of the audit log
// underneath, so "why did my assistant say it could not do that" has an answer
// on the same screen as the checkbox that fixes it.
//
// Everything here reads live state through `activeHost()` and refreshes on a
// timer rather than on notifications, because the things it displays change on
// transport threads and a UI that repainted from those would need a hop per
// event for no benefit at one second's resolution.
// ============================================================================

class RemoteClientsPage : public juce::Component, private juce::Timer {
  public:
    RemoteClientsPage() {
        blurbLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        blurbLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        blurbLabel_.setJustificationType(juce::Justification::topLeft);
        blurbLabel_.setText(tr("connections.clients.blurb"), juce::dontSendNotification);
        addAndMakeVisible(blurbLabel_);

        clientsViewport_.setViewedComponent(&clientsHolder_, false);
        clientsViewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(clientsViewport_);

        emptyLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
        emptyLabel_.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_DIM));
        emptyLabel_.setJustificationType(juce::Justification::centred);
        emptyLabel_.setText(tr("connections.clients.none"), juce::dontSendNotification);
        addAndMakeVisible(emptyLabel_);

        activityCaption_.setText(tr("connections.clients.activity"), juce::dontSendNotification);
        styleLabel(activityCaption_);
        addAndMakeVisible(activityCaption_);

        activityView_.setMultiLine(true, false);
        activityView_.setReadOnly(true);
        activityView_.setFont(juce::Font(juce::FontOptions(
            juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
        styleReadOnlyEditor(activityView_);
        addAndMakeVisible(activityView_);

        rebuild();
        startTimer(1000);
    }

    ~RemoteClientsPage() override {
        stopTimer();
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(12);
        blurbLabel_.setBounds(bounds.removeFromTop(40));
        bounds.removeFromTop(8);

        // The activity tail takes a fixed share off the bottom; the client list
        // gets whatever is left, which is the part that grows with use.
        auto activity = bounds.removeFromBottom(juce::jmin(140, bounds.getHeight() / 2));
        activityView_.setBounds(activity);
        bounds.removeFromBottom(4);
        activityCaption_.setBounds(bounds.removeFromBottom(20));
        bounds.removeFromBottom(8);

        clientsViewport_.setBounds(bounds);
        emptyLabel_.setBounds(bounds);
        layoutRows();
    }

  private:
    /// One client: its name, whether it is connected, and a checkbox per scope.
    class ClientRow : public juce::Component {
      public:
        static constexpr int HEIGHT = 46;

        explicit ClientRow(juce::String clientName) : name_(std::move(clientName)) {
            nameLabel_.setText(name_, juce::dontSendNotification);
            nameLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
            nameLabel_.setColour(juce::Label::textColourId,
                                 DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            addAndMakeVisible(nameLabel_);

            statusLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
            statusLabel_.setColour(juce::Label::textColourId,
                                   DarkTheme::getColour(DarkTheme::TEXT_DIM));
            addAndMakeVisible(statusLabel_);

            for (const auto scope : magda::remote::allScopeValues()) {
                auto toggle = std::make_unique<juce::ToggleButton>();
                // Not translated, deliberately. These are the wire names: the
                // same tokens appear in the API docs, in a client's own error
                // handling, and in the activity tail below as `tracks.create
                // denied (edit)`. A translated checkbox beside an untranslated
                // refusal would break the one link this page exists to make.
                toggle->setButtonText(magda::remote::scopeName(scope));
                toggle->setColour(juce::ToggleButton::textColourId,
                                  DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
                toggle->setColour(juce::ToggleButton::tickColourId,
                                  DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY));
                // `read` is what being admitted means, so it is shown ticked
                // and cannot be cleared — a row granting nothing at all would
                // be indistinguishable from a client that was never granted,
                // while still being one that can connect.
                if (scope == magda::remote::Scope::Read) {
                    toggle->setToggleState(true, juce::dontSendNotification);
                    toggle->setEnabled(false);
                } else {
                    toggle->onClick = [this]() { commitScopes(); };
                }
                addAndMakeVisible(*toggle);
                toggles_.push_back(std::move(toggle));
            }

            disconnectButton_.setButtonText(tr("connections.clients.disconnect"));
            disconnectButton_.onClick = [this]() {
                if (auto* host = magda::remote::activeHost())
                    host->clients().disconnectClient(name_);
            };
            addAndMakeVisible(disconnectButton_);

            forgetButton_.setButtonText(tr("connections.clients.forget"));
            forgetButton_.onClick = [this]() {
                if (auto* host = magda::remote::activeHost())
                    host->clients().forget(name_);
            };
            addAndMakeVisible(forgetButton_);
        }

        const juce::String& clientName() const {
            return name_;
        }

        /// Push current state in. Called every tick, so it must not fight the
        /// user: a toggle is only written when it actually differs.
        void update(magda::remote::ScopeSet scopes, int connections,
                    const juce::String& transports) {
            const auto& values = magda::remote::allScopeValues();
            for (std::size_t index = 0; index < toggles_.size() && index < values.size(); ++index) {
                const auto granted = scopes.has(values[index]);
                if (toggles_[index]->getToggleState() != granted)
                    toggles_[index]->setToggleState(granted, juce::dontSendNotification);
            }

            const auto connected = connections > 0;
            statusLabel_.setText(
                connected ? tr("connections.clients.connected").replace("{0}", transports)
                          : tr("connections.clients.not_connected"),
                juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId,
                                   connected ? DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY)
                                             : DarkTheme::getColour(DarkTheme::TEXT_DIM));
            disconnectButton_.setEnabled(connected);
        }

        void resized() override {
            auto bounds = getLocalBounds().reduced(6, 4);

            auto header = bounds.removeFromTop(18);
            forgetButton_.setBounds(header.removeFromRight(64).reduced(0, 1));
            header.removeFromRight(4);
            disconnectButton_.setBounds(header.removeFromRight(84).reduced(0, 1));
            header.removeFromRight(8);
            nameLabel_.setBounds(header.removeFromLeft(juce::jmin(160, header.getWidth() / 2)));
            statusLabel_.setBounds(header);

            bounds.removeFromTop(2);
            auto row = bounds.removeFromTop(18);
            const auto each =
                toggles_.empty() ? 0 : row.getWidth() / static_cast<int>(toggles_.size());
            for (auto& toggle : toggles_)
                toggle->setBounds(row.removeFromLeft(each));
        }

        void paint(juce::Graphics& g) override {
            const auto card = getLocalBounds().toFloat().reduced(1.0f);
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
            g.fillRoundedRectangle(card, 3.0f);
            // Outlined, not just tinted (#2142). A name sits directly above its
            // own checkboxes and the next name directly below them, so with only
            // a fill to separate them a list scrolled to mid-row reads as a row
            // of checkboxes belonging to the name underneath. The border says
            // where one client ends.
            g.setColour(DarkTheme::getBorderColour());
            g.drawRoundedRectangle(card, 3.0f, 1.0f);
        }

      private:
        void commitScopes() {
            auto* host = magda::remote::activeHost();
            if (host == nullptr)
                return;

            magda::remote::ScopeSet scopes;
            const auto& values = magda::remote::allScopeValues();
            for (std::size_t index = 0; index < toggles_.size() && index < values.size(); ++index)
                scopes.set(values[index], toggles_[index]->getToggleState());
            // `setScopes` forces `read` back on and persists, so this is the
            // whole of applying a change — there is no separate save step, and
            // the next request the client makes reads the new grant.
            host->clients().setScopes(name_, scopes);
        }

        const juce::String name_;
        juce::Label nameLabel_;
        juce::Label statusLabel_;
        std::vector<std::unique_ptr<juce::ToggleButton>> toggles_;
        juce::TextButton disconnectButton_;
        juce::TextButton forgetButton_;
    };

    void timerCallback() override {
        rebuild();
    }

    /**
     * @brief Reconcile the rows with the registry, and refresh the activity tail.
     *
     * Rows are rebuilt only when the *set* of client names changes, not on every
     * tick: recreating a row under the pointer would swallow the click that was
     * about to land on one of its checkboxes.
     */
    void rebuild() {
        auto* host = magda::remote::activeHost();
        if (host == nullptr) {
            if (!rows_.empty()) {
                rows_.clear();
                clientsHolder_.removeAllChildren();
            }
            emptyLabel_.setText(
                tr("connections.clients.unavailable")
                    .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Api)),
                juce::dontSendNotification);
            emptyLabel_.setVisible(true);
            return;
        }

        const auto grants = host->clients().grants();
        const auto connections = host->clients().connections();

        // The union of the two, not just the grants. `forget()` drops a grant
        // without disconnecting — deliberately, since the client reverts to
        // read-only rather than being cut off — and a list built from grants
        // alone would make that row vanish while its client was still
        // connected, leaving no way to disconnect it until it happened to ask
        // for something and recreate its entry.
        juce::StringArray names;
        for (const auto& grant : grants)
            names.addIfNotAlreadyThere(grant.name);
        for (const auto& connection : connections)
            names.addIfNotAlreadyThere(connection.name);
        names.sort(false);

        if (names != rowNames_) {
            rowNames_ = names;
            rows_.clear();
            clientsHolder_.removeAllChildren();
            for (const auto& name : rowNames_) {
                auto row = std::make_unique<ClientRow>(name);
                clientsHolder_.addAndMakeVisible(*row);
                rows_.push_back(std::move(row));
            }
            layoutRows();
        }

        emptyLabel_.setVisible(rows_.empty());
        emptyLabel_.setText(tr("connections.clients.none"), juce::dontSendNotification);

        // Driven by the rows rather than by the grants, because a row can exist
        // without one: a client that was forgotten while still connected keeps
        // its row until it disconnects. It shows the read-only default, which is
        // what it will actually get on its next request.
        for (const auto& row : rows_) {
            const auto& name = row->clientName();

            auto scopes = magda::remote::defaultClientScopes();
            for (const auto& grant : grants) {
                if (grant.name == name) {
                    scopes = grant.scopes;
                    break;
                }
            }

            int count = 0;
            juce::StringArray transports;
            for (const auto& connection : connections) {
                if (connection.name != name)
                    continue;
                ++count;
                transports.addIfNotAlreadyThere(connection.transport);
            }
            row->update(scopes, count, transports.joinIntoString(", "));
        }

        refreshActivity(host->audit());
    }

    ClientRow* rowFor(const juce::String& name) {
        for (auto& row : rows_) {
            if (row->clientName() == name)
                return row.get();
        }
        return nullptr;
    }

    void layoutRows() {
        const auto width = juce::jmax(0, clientsViewport_.getMaximumVisibleWidth());
        int y = 0;
        for (auto& row : rows_) {
            row->setBounds(0, y, width, ClientRow::HEIGHT);
            y += ClientRow::HEIGHT + 4;
        }
        clientsHolder_.setSize(width, y);
    }

    void refreshActivity(const magda::remote::RemoteAuditLog& log) {
        // Cheap change detection: the counter only moves when something was
        // recorded, so a quiet session never rebuilds this string.
        const auto recorded = log.totalRecorded();
        if (recorded == shownActivity_)
            return;
        shownActivity_ = recorded;

        juce::StringArray lines;
        for (const auto& entry : log.recent(kActivityLines)) {
            const auto time = juce::Time(entry.timestampMs).toString(false, true, false, true);
            auto line = time + "  " + entry.client + "  " + entry.operation + "  " +
                        magda::remote::toString(entry.outcome);
            if (entry.detail.isNotEmpty())
                line += " (" + entry.detail + ")";
            lines.add(line);
        }

        activityView_.setText(lines.joinIntoString("\n"), juce::dontSendNotification);
        // Newest last, so show the end.
        activityView_.moveCaretToEnd();
    }

    static constexpr std::size_t kActivityLines = 200;

    juce::Label blurbLabel_;
    juce::Viewport clientsViewport_;
    juce::Component clientsHolder_;
    juce::Label emptyLabel_;
    std::vector<std::unique_ptr<ClientRow>> rows_;
    juce::StringArray rowNames_;

    juce::Label activityCaption_;
    juce::TextEditor activityView_;
    std::uint64_t shownActivity_ = 0;
};  // =============================================================================
// OscSurfacesPage (#1757 §4, #2091, #2096)
// =============================================================================

/**
 * The OSC settings, and the first UI any of them have had: everything below was
 * reachable only by hand-editing the config file, which made a shipped feature
 * effectively unavailable.
 *
 * Nothing here reconfigures the OSC stack directly. `Config::save` notifies its
 * listeners, and both `OscService` and `OscFeedbackProjector` are among them, so
 * a field that loses focus rebinds the socket or re-aims the senders without
 * this page knowing either exists. What it does know is how to ask them what
 * happened, through `osc_app`, which is what the status area reports.
 *
 * There is no "send feedback to" field, and that is #2096: MAGDA reads its own
 * datagrams, so it answers whoever is talking. What the status area shows
 * instead is the surfaces it has heard from — which is the question anyone
 * debugging a silent surface actually has, and one a message counter could
 * never answer.
 */
class OscSurfacesPage : public juce::Component, private juce::Timer {
  public:
    OscSurfacesPage() {
        auto& config = Config::getInstance();

        enable_.setButtonText(
            tr("connections.osc.enable")
                .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Osc)));
        enable_.setToggleState(config.getOscEnabled(), juce::dontSendNotification);
        enable_.onClick = [this] {
            Config::getInstance().setOscEnabled(enable_.getToggleState());
            commit();
        };
        addAndMakeVisible(enable_);

        // Which interfaces the socket answers on is the whole of OSC's access
        // control — the protocol has no authentication — and there are exactly
        // two answers worth offering. So this is a list and not free text: a
        // typo in a hand-typed address fails closed, which looks like MAGDA
        // being broken rather than like a typo.
        //
        // An address that is neither, set by hand in the config file for a
        // machine with several interfaces, is added as a third entry and
        // selected, so opening this page does not quietly replace it.
        // The addresses are substituted rather than written into the strings:
        // they are protocol, not prose, and a translator has no reason to guess
        // that the dots in 127.0.0.1 are load-bearing.
        bindAddress_.addItem(tr("connections.osc.bind_all").replace("{0}", kAllInterfacesAddress),
                             kBindAll);
        bindAddress_.addItem(tr("connections.osc.bind_loopback").replace("{0}", kLoopbackAddress),
                             kBindLoopback);

        const juce::String configured(config.getOscBindAddress());
        if (configured == kLoopbackAddress) {
            bindAddress_.setSelectedId(kBindLoopback, juce::dontSendNotification);
        } else if (configured.isEmpty() || configured == kAllInterfacesAddress) {
            bindAddress_.setSelectedId(kBindAll, juce::dontSendNotification);
        } else {
            bindAddress_.addItem(configured, kBindCustom);
            bindAddress_.setSelectedId(kBindCustom, juce::dontSendNotification);
        }

        bindAddress_.onChange = [this] {
            Config::getInstance().setOscBindAddress(selectedBindAddress());
            commit();
        };
        addAndMakeVisible(bindAddress_);
        addLabel(bindLabel_, tr("connections.osc.bind_address"));

        setUpPortField(receivePort_, config.getOscReceivePort(),
                       [](int port) { Config::getInstance().setOscReceivePort(port); });
        addLabel(receiveLabel_, tr("connections.osc.receive_port"));

        setUpPortField(feedbackPort_, config.getOscFeedbackPort(),
                       [](int port) { Config::getInstance().setOscFeedbackPort(port); });
        addLabel(feedbackPortLabel_, tr("connections.osc.feedback_port"));

        status_.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(status_);

        applyTheme();
        refreshStatus();
        startTimer(kStatusIntervalMs);
    }

    ~OscSurfacesPage() override {
        stopTimer();
    }

    /// Both a theme switch and a change of UI font family arrive here —
    /// `MainWindow::refreshThemedLookAndFeels` sends a look-and-feel change for
    /// each — so this is the one hook that keeps the page in step.
    void lookAndFeelChanged() override {
        applyTheme();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(12);

        enable_.setBounds(area.removeFromTop(kRowHeight));
        area.removeFromTop(kGap);

        layoutRow(area, bindLabel_, bindAddress_);
        layoutRow(area, receiveLabel_, receivePort_);
        area.removeFromTop(kGap);
        layoutRow(area, feedbackPortLabel_, feedbackPort_);

        area.removeFromTop(kGap * 2);
        status_.setBounds(area);
    }

  private:
    static constexpr int kRowHeight = 24;
    static constexpr int kGap = 8;
    static constexpr int kLabelWidth = 150;
    static constexpr int kFieldWidth = 200;
    static constexpr int kStatusIntervalMs = 1000;

    /// The size `DialogLookAndFeel` gives a combo box and a button, so the
    /// labels and text fields beside them have to be told it: a `juce::Label`
    /// left alone is 14, and a `juce::TextEditor` is whatever the platform
    /// default font happens to be, which is not Inter at all.
    static constexpr float kFieldFontSize = 13.0f;
    static constexpr float kStatusFontSize = 12.0f;

    static constexpr int kBindAll = 1;
    static constexpr int kBindLoopback = 2;
    static constexpr int kBindCustom = 3;
    static constexpr const char* kAllInterfacesAddress = "0.0.0.0";
    static constexpr const char* kLoopbackAddress = "127.0.0.1";

    /**
     * @brief Fonts, and the one colour this page caches.
     *
     * Called at construction and again on every look-and-feel change. A font or
     * a colour taken once in a constructor is the standing bug in this
     * codebase's dialogs: `setColour` on a component survives a look-and-feel
     * change, so it goes stale exactly when the theme moves, and a `juce::Font`
     * captured at construction ignores a change of UI font family.
     */
    void applyTheme() {
        const auto fieldFont = FontManager::getInstance().getUIFont(kFieldFontSize);
        for (auto* label : {&bindLabel_, &receiveLabel_, &feedbackPortLabel_})
            label->setFont(fieldFont);
        for (auto* editor : {&receivePort_, &feedbackPort_})
            editor->applyFontToAllText(fieldFont);

        status_.setFont(FontManager::getInstance().getUIFont(kStatusFontSize));
        status_.setColour(juce::Label::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    }

    /// The address behind the selected entry. The two presets are spelled out
    /// in the item text for the user, so the address itself cannot be read back
    /// out of it; the custom entry is the address, because that is what it was
    /// built from.
    std::string selectedBindAddress() const {
        switch (bindAddress_.getSelectedId()) {
            case kBindLoopback:
                return kLoopbackAddress;
            case kBindCustom:
                return bindAddress_.getItemText(bindAddress_.indexOfItemId(kBindCustom))
                    .toStdString();
            default:
                return kAllInterfacesAddress;
        }
    }

    void addLabel(juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);
    }

    void layoutRow(juce::Rectangle<int>& area, juce::Label& label, juce::Component& field) {
        auto row = area.removeFromTop(kRowHeight);
        label.setBounds(row.removeFromLeft(kLabelWidth));
        field.setBounds(row.removeFromLeft(kFieldWidth));
        area.removeFromTop(4);
    }

    /// A port field, with the same rule at both: a number the OS could bind, or
    /// the stored value put back. Committing on focus loss rather than on every
    /// keystroke, so typing "9" on the way to "9001" does not rebind the socket
    /// to port 9.
    void setUpPortField(juce::TextEditor& field, int initial, std::function<void(int)> store) {
        field.setInputRestrictions(5, "0123456789");
        field.setText(juce::String(initial), juce::dontSendNotification);
        field.onFocusLost = [this, &field, store = std::move(store)] {
            const int port = field.getText().getIntValue();
            if (port > 0 && port <= 65535) {
                store(port);
                commit();
            } else {
                refreshFields();
            }
        };
        field.onReturnKey = [&field] { field.giveAwayKeyboardFocus(); };
        addAndMakeVisible(field);
    }

    /// Persist, which is also what reconfigures: `Config::save` notifies the
    /// listeners, and the OSC service and the feedback projector are two of them.
    void commit() {
        Config::getInstance().save();
        refreshStatus();
    }

    void refreshFields() {
        auto& config = Config::getInstance();
        receivePort_.setText(juce::String(config.getOscReceivePort()), juce::dontSendNotification);
        feedbackPort_.setText(juce::String(config.getOscFeedbackPort()),
                              juce::dontSendNotification);
    }

    void timerCallback() override {
        refreshStatus();
    }

    void refreshStatus() {
        juce::String text;

        if (osc_app::isListening()) {
            text << tr("connections.osc.status_listening")
                        .replace("{0}", osc_app::boundAddress())
                        .replace("{1}", juce::String(osc_app::boundPort()))
                 << "\n"
                 << tr("connections.osc.status_received")
                        .replace("{0}", juce::String(osc_app::acceptedMessageCount()));
        } else if (Config::getInstance().getOscEnabled()) {
            // Enabled and not listening means the port was taken, which is the
            // one failure a user can act on and the one they cannot see.
            text << tr("connections.osc.status_bind_failed");
        } else {
            text << tr("connections.osc.status_off");
        }

        text << "\n\n" << tr("connections.osc.surfaces") << "\n";

        const auto surfaces = osc_app::surfaces();
        if (surfaces.empty()) {
            // The state a silent surface is actually in, and the one a message
            // counter reading zero could never distinguish from a dropped
            // packet: nothing has ever arrived, so there is nobody to answer.
            text << tr("connections.osc.surfaces_none")
                        .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Magda));
        } else {
            const auto now = juce::Time::currentTimeMillis();
            for (const auto& surface : surfaces)
                text << tr("connections.osc.surface_line")
                            .replace("{0}", surface.host)
                            .replace("{1}", juce::String(surface.received))
                            .replace("{2}", juce::String(surface.sent))
                            .replace("{3}", lastSeenText(now - surface.lastSeenMs))
                     << "\n";
        }

        status_.setText(text, juce::dontSendNotification);
    }

    /// "just now" up to a few seconds, then whole seconds, then minutes. The
    /// number matters only for telling "talking" from "went away", so it is not
    /// worth a unit past that.
    static juce::String lastSeenText(juce::int64 ageMs) {
        if (ageMs < 3000)
            return tr("connections.osc.seen_now");
        if (ageMs < 60000)
            return tr("connections.osc.seen_seconds").replace("{0}", juce::String(ageMs / 1000));
        return tr("connections.osc.seen_minutes").replace("{0}", juce::String(ageMs / 60000));
    }

    juce::ToggleButton enable_;
    juce::Label bindLabel_, receiveLabel_, feedbackPortLabel_;
    juce::ComboBox bindAddress_;
    juce::TextEditor receivePort_, feedbackPort_;
    juce::Label status_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscSurfacesPage)
};
// =============================================================================
// ConnectionsDialog
// =============================================================================

ConnectionsDialog::ConnectionsDialog() {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    mcpPage_ = std::make_unique<McpPage>();
    webSocketPage_ = std::make_unique<WebSocketPage>();
    oscPage_ = std::make_unique<OscSurfacesPage>();
    remoteClientsPage_ = std::make_unique<RemoteClientsPage>();

    auto tabBg = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    // Transports first, in the order most users meet them, then the grants that
    // apply to whichever they used. Clients is last rather than beside a
    // transport because it belongs to both: turning an endpoint on and saying
    // what a client may do are separate decisions, made at different times, and
    // the second grows a row per client while the first stays one switch.
    tabbedComponent_.addTab(magda::technicalText(magda::TechnicalTextToken::Mcp), tabBg,
                            mcpPage_.get(), false);
    tabbedComponent_.addTab(magda::technicalText(magda::TechnicalTextToken::WebSocket), tabBg,
                            webSocketPage_.get(), false);
    tabbedComponent_.addTab(magda::technicalText(magda::TechnicalTextToken::Osc), tabBg,
                            oscPage_.get(), false);
    tabbedComponent_.addTab(tr("connections.tab.clients"), tabBg, remoteClientsPage_.get(), false);
    addAndMakeVisible(tabbedComponent_);

    setSize(560, 512);
}

ConnectionsDialog::~ConnectionsDialog() {
    setLookAndFeel(nullptr);
}

void ConnectionsDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void ConnectionsDialog::resized() {
    tabbedComponent_.setBounds(getLocalBounds().reduced(8));
}

// =============================================================================
// showDialog
// =============================================================================

namespace {
class SelfClosingDialogWindow : public juce::DialogWindow {
  public:
    SelfClosingDialogWindow(const juce::String& title, juce::Colour bg)
        : juce::DialogWindow(title, bg, false, true) {}

    void closeButtonPressed() override {
        juce::MessageManager::callAsync([self = this]() { delete self; });
    }
};
}  // namespace

void ConnectionsDialog::showDialog(juce::Component* /*parent*/) {
    auto* dialog = new ConnectionsDialog();
    auto bg = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);

    auto* window = new SelfClosingDialogWindow(tr("connections.title"), bg);
    window->setContentOwned(dialog, true);
    window->setUsingNativeTitleBar(true);
    window->setResizable(true, false);
    window->setAlwaysOnTop(true);
    window->centreWithSize(dialog->getWidth(), dialog->getHeight());
    window->setVisible(true);
}

}  // namespace magda
