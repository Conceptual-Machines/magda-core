#include "../core/AutomationManager.hpp"
#include "../core/ClipManager.hpp"
#include "../core/SelectionManager.hpp"
#include "../core/TrackManager.hpp"
#include "../project/ProjectManager.hpp"
#include "remote_model_bridge.hpp"
#include "remote_service.hpp"
#include "transport_api.hpp"

namespace magda::remote {

/**
 * Implements every manager listener interface and funnels each callback to a
 * topic.
 *
 * Two kinds of notification, deliberately distinguished:
 *
 * - **Committed changes** call `noteModelChanged`, which bumps the revision. A
 *   remote client's `expectedRevision` should go stale when the user really
 *   edits the project.
 * - **Continuous motion** calls `noteModelActivity`, which does not. Parameter
 *   values, macro sweeps, and drag previews fire throughout playback; bumping
 *   the revision for those would make optimistic concurrency useless whenever
 *   the transport is rolling.
 *
 * Where a callback is ambiguous — it can come from a user edit *or* from
 * automation playback — it is treated as motion. The cost is that a write
 * racing an automated parameter change is not rejected; the alternative makes
 * every write during playback fail, which is worse and far more common.
 */
class ModelChangeBridge::Impl final : public TrackManagerListener,
                                      public ClipManagerListener,
                                      public AutomationManagerListener,
                                      public SelectionManagerListener,
                                      public ProjectManagerListener {
  public:
    explicit Impl(RemoteApiService& service, TransportApi* transport)
        // ProjectManager starts an autosave timer in its constructor, so
        // constructing it without an event loop is invalid. A host with no
        // MessageManager has no project open/close lifecycle to observe in the
        // first place, so skip it there rather than forcing the singleton into
        // existence just to listen to it.
        : service_(service),
          transport_(transport),
          observingProject_(juce::MessageManager::getInstanceWithoutCreating() != nullptr) {
        TrackManager::getInstance().addListener(this);
        ClipManager::getInstance().addListener(this);
        AutomationManager::getInstance().addListener(this);
        SelectionManager::getInstance().addListener(this);
        if (transport_ != nullptr) {
            transportListener_ = transport_->addStateListener(
                [this] { service_.noteModelActivity(Topic::Transport); });
        }
        if (observingProject_)
            ProjectManager::getInstance().addListener(this);
    }

    ~Impl() override {
        if (observingProject_)
            ProjectManager::getInstance().removeListener(this);
        if (transport_ != nullptr && transportListener_ != 0)
            transport_->removeStateListener(transportListener_);
        SelectionManager::getInstance().removeListener(this);
        AutomationManager::getInstance().removeListener(this);
        ClipManager::getInstance().removeListener(this);
        TrackManager::getInstance().removeListener(this);
    }

    // ---- TrackManagerListener -------------------------------------------

    void tracksChanged() override {
        // Session slots are keyed by track, so a track appearing or going away
        // adds or removes a whole column of the grid.
        service_.noteModelChanged({Topic::Tracks, Topic::Session});
    }
    void trackPropertyChanged(int) override {
        if (AutomationManager::getInstance().isApplyingAutomationWrite())
            service_.noteModelActivity(Topic::Tracks);
        else
            service_.noteModelChanged(Topic::Tracks);
    }
    void masterChannelChanged() override {
        if (AutomationManager::getInstance().isApplyingAutomationWrite())
            service_.noteModelActivity(Topic::Tracks);
        else
            service_.noteModelChanged(Topic::Tracks);
    }
    void trackSelectionChanged(TrackId) override {
        service_.noteModelChanged(Topic::Selection);
    }
    void trackDevicesChanged(TrackId) override {
        service_.noteModelChanged(Topic::Devices);
    }
    void deviceModifiersChanged(TrackId) override {
        service_.noteModelChanged(Topic::Devices);
    }
    void modulationNamesChanged(TrackId) override {
        service_.noteModelChanged(Topic::Devices);
    }
    void devicePropertyChanged(const ChainNodePath&) override {
        service_.noteModelChanged(Topic::Devices);
    }
    void deviceAdded(const ChainNodePath&, const DeviceInfo&) override {
        service_.noteModelChanged(Topic::Devices);
    }
    void deviceParameterChanged(const ChainNodePath&, int, float) override {
        service_.noteModelActivity(Topic::Devices);
    }
    void macroValueChanged(TrackId, ChainScope, int, int, float) override {
        service_.noteModelActivity(Topic::Devices);
    }
    void modParameterChanged(TrackId, const ChainNodePath&, ModId, int, float) override {
        service_.noteModelActivity(Topic::Devices);
    }
    // audioSidechainTriggered is deliberately not forwarded: it reports that a
    // gate opened, not that project state changed, and has no topic.

    // ---- ClipManagerListener --------------------------------------------

    // The session grid is projected out of the clips rather than stored beside
    // them, so anything that adds, removes, or moves a clip can change it. The
    // extra topic costs nothing when it did not: an unchanged projection is
    // coalesced away before an event is built.
    void clipsChanged() override {
        service_.noteModelChanged({Topic::Clips, Topic::Session});
    }
    void clipPropertyChanged(ClipId) override {
        service_.noteModelChanged({Topic::Clips, Topic::Session});
    }
    void clipPropertiesChanged(const std::vector<ClipId>&) override {
        // Overridden rather than inherited: the base implementation fans out to
        // one clipPropertyChanged per id, which would be one revision bump per
        // clip for what the user did as a single multi-selection drag.
        service_.noteModelChanged({Topic::Clips, Topic::Session});
    }
    void clipSelectionChanged(ClipId) override {
        service_.noteModelChanged(Topic::Selection);
    }
    void clipPlaybackStateChanged(ClipId) override {
        service_.noteModelActivity(Topic::Session);
    }
    void clipPlaybackRequested(ClipId, ClipPlaybackRequest) override {
        service_.noteModelActivity(Topic::Session);
    }
    void clipDragPreview(ClipId, double, double) override {
        service_.noteModelActivity(Topic::Clips);
    }

    // ---- AutomationManagerListener --------------------------------------

    void automationLanesChanged() override {
        service_.noteModelChanged(Topic::Automation);
    }
    void automationLanePropertyChanged(AutomationLaneId) override {
        service_.noteModelChanged(Topic::Automation);
    }
    void automationClipsChanged(AutomationLaneId) override {
        service_.noteModelChanged(Topic::Automation);
    }
    void automationPointsChanged(AutomationLaneId) override {
        service_.noteModelChanged(Topic::Automation);
    }
    void automationPointDragPreview(AutomationLaneId, AutomationPointId, double, double) override {
        service_.noteModelActivity(Topic::Automation);
    }
    void automationValueChanged(AutomationLaneId, double) override {
        // Fires continuously while the transport rolls.
        service_.noteModelActivity(Topic::Automation);
    }

    // ---- SelectionManagerListener ---------------------------------------

    void selectionTypeChanged(SelectionType) override {
        service_.noteModelChanged(Topic::Selection);
    }
    void multiTrackSelectionChanged(const std::unordered_set<TrackId>&) override {
        service_.noteModelChanged(Topic::Selection);
    }
    void multiClipSelectionChanged(const std::unordered_set<ClipId>&) override {
        service_.noteModelChanged(Topic::Selection);
    }
    void noteSelectionChanged(const NoteSelection&) override {
        service_.noteModelChanged(Topic::Selection);
    }
    void chainNodeSelectionChanged(const ChainNodePath&) override {
        service_.noteModelChanged(Topic::Selection);
    }
    void automationLaneSelectionChanged(const AutomationLaneSelection&) override {
        service_.noteModelChanged(Topic::Selection);
    }
    void automationClipSelectionChanged(const AutomationClipSelection&) override {
        service_.noteModelChanged(Topic::Selection);
    }

    // ---- ProjectManagerListener -----------------------------------------

    void projectOpened(const ProjectInfo&) override {
        if (transport_ != nullptr)
            transport_->refreshStateSource();
        // Everything queued against the outgoing project is now addressed at
        // state that no longer exists. This is the trigger the service's
        // cancellation path exists for, and it already bumps the revision and
        // publishes Topic::Project — bumping again here would advance it twice
        // for one swap.
        service_.projectReplaced();
    }
    void projectClosed() override {
        if (transport_ != nullptr)
            transport_->refreshStateSource();
        service_.projectReplaced();
    }
    void projectSaved(const ProjectInfo&) override {
        service_.noteModelChanged(Topic::Project);
    }
    void projectPropertiesChanged() override {
        service_.noteModelChanged(Topic::Project);
    }

  private:
    RemoteApiService& service_;
    TransportApi* transport_ = nullptr;
    int transportListener_ = 0;
    bool observingProject_ = false;
};

ModelChangeBridge::ModelChangeBridge(RemoteApiService& service, TransportApi* transport)
    : impl_(std::make_unique<Impl>(service, transport)) {}

ModelChangeBridge::~ModelChangeBridge() = default;

}  // namespace magda::remote
