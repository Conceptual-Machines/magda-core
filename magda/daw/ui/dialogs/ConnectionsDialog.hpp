#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

class McpPage;
class WebSocketPage;
class OscSurfacesPage;
class RemoteClientsPage;

/**
 * Four-tab dialog for everything outside MAGDA that drives it (#2142):
 *
 *   [ MCP ] [ WebSocket ] [ OSC ] [ Clients ]
 *
 * MCP and WebSocket are the remote API's two transports. They are separate tabs
 *   because they are separate listeners: each has its own switch, its own port,
 *   and its own token, so one can be turned on, restarted, or re-credentialled
 *   while the other keeps serving. A user driving MAGDA from a script wants the
 *   WebSocket and no MCP endpoint; a user with only an AI host wants the
 *   reverse. Neither should have to open a listener they will not use.
 *
 * OSC tab: whether MAGDA listens, on which interface and port, and where it
 *   answers. Not part of the remote API — its own stack, its own socket, and no
 *   token, no client registry, no scopes. It is here because it is another way
 *   in from outside, not because it shares any machinery.
 *
 * Clients tab: what each remote client is allowed to do, and what it has done.
 *   Both remote-API transports feed it, so a row is tagged with the one it came
 *   in over. OSC never appears here.
 *   The permission vocabulary — `read`, `edit`, `transport`, `session`,
 *   `hardware-midi` — is about what a *client* may do, and the same
 *   `RemoteClientRegistry` backs every transport, so a Lua script or a bespoke
 *   controller holding the token is listed here beside any AI host.
 *
 * The first two lived in AI Settings and the third in Controllers, which split
 * one question — "what is allowed to drive my DAW" — across two dialogs by
 * which feature shipped first. None of it is AI-specific and none of it is a
 * control surface in the MIDI-profile sense, so it is its own section; AI
 * Settings keeps model and provider configuration, which is what its name
 * promises, and Controllers keeps MIDI profiles and Lua scripts.
 *
 * Commit-on-change like Controllers, not OK/Cancel like AI Settings: every
 * control here owns a socket or a live grant, and a switch whose result you
 * have to press OK to see is one nobody trusts.
 */
class ConnectionsDialog : public juce::Component {
  public:
    ConnectionsDialog();
    ~ConnectionsDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    static void showDialog(juce::Component* parent);

  private:
    juce::TabbedComponent tabbedComponent_{juce::TabbedButtonBar::TabsAtTop};
    std::unique_ptr<McpPage> mcpPage_;
    std::unique_ptr<WebSocketPage> webSocketPage_;
    std::unique_ptr<OscSurfacesPage> oscPage_;
    std::unique_ptr<RemoteClientsPage> remoteClientsPage_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConnectionsDialog)
};

}  // namespace magda
