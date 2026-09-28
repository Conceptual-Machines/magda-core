#include "AppServices.hpp"

#include <juce_events/juce_events.h>

#include <cstdlib>

#include "../audio/DeviceParameterDisplayTextProvider.hpp"
#include "../core/Config.hpp"
#include "../core/ViewModeController.hpp"
#include "../core/controllers/BindingRegistry.hpp"
#include "../core/controllers/ControllerProfileRegistry.hpp"
#include "../project/ProjectManager.hpp"
#include "../ui/state/TimelineController.hpp"
#include "../ui/state/TimelineEvents.hpp"
#include "PluginService.hpp"

namespace magda::app_services {

namespace {

void hookProjectState() {
    // Wire up state capture before project save
    ProjectManager::getInstance().onBeforeSave = []() {
        // The service is fed by whichever engine renders: only that instance
        // holds the chunk a project saves (#2758).
        PluginService::getInstance().captureAllPluginStates();

        // Capture zoom/scroll state
        if (auto* tc = TimelineController::getCurrent()) {
            const auto& timelineState = tc->getState();
            const auto& zoom = timelineState.zoom;
            auto& proj = ProjectManager::getInstance().getMutableProjectInfo();
            proj.horizontalZoom = zoom.horizontalZoom;
            proj.verticalZoom = zoom.verticalZoom;
            proj.scrollX = zoom.scrollX;
            proj.scrollY = zoom.scrollY;

            proj.markers.clear();
            proj.markers.reserve(timelineState.markers.size());
            for (const auto& marker : timelineState.markers) {
                ProjectTimelineMarker projectMarker;
                projectMarker.id = marker.id;
                projectMarker.positionBeats = marker.positionBeats;
                projectMarker.name = marker.name;
                projectMarker.colourArgb = marker.colour.getARGB();
                proj.markers.push_back(projectMarker);
            }
        }

        // Capture active view mode
        auto viewMode = ViewModeController::getInstance().getViewMode();
        ProjectManager::getInstance().getMutableProjectInfo().activeView =
            static_cast<int>(viewMode);

        // Capture project-scoped bindings
        ProjectManager::getInstance().getMutableProjectInfo().projectBindings =
            BindingRegistry::getInstance().saveProject();
    };

    // Wire up state restore after project load
    ProjectManager::getInstance().onAfterLoad = [](const ProjectInfo& info) {
        // Restore active view mode
        auto viewMode = static_cast<ViewMode>(info.activeView);
        ViewModeController::getInstance().setViewMode(viewMode);

        // Restore project-scoped bindings
        BindingRegistry::getInstance().loadProject(info.projectBindings);

        // Restore zoom/scroll state
        if (info.horizontalZoom > 0.0) {
            double hz = info.horizontalZoom;
            int sx = info.scrollX;
            int sy = info.scrollY;
            // Try immediate dispatch first
            if (auto* tc = TimelineController::getCurrent()) {
                tc->dispatch(SetZoomEvent{hz});
                tc->dispatch(SetScrollPositionEvent{sx, sy});
            }
            // Also defer to catch cases where UI isn't ready yet
            juce::MessageManager::callAsync([hz, sx, sy]() {
                if (auto* tc = TimelineController::getCurrent()) {
                    tc->dispatch(SetZoomEvent{hz});
                    tc->dispatch(SetScrollPositionEvent{sx, sy});
                }
            });
        }
    };
}

}  // namespace

bool isHeadless(bool asked) {
    if (asked)
        return true;

    if (const auto* value = std::getenv("MAGDA_HEADLESS")) {
        const auto flag = juce::String(value).trim().toLowerCase();
        return flag.isNotEmpty() && flag != "0" && flag != "false" && flag != "off" && flag != "no";
    }
    return false;
}

void bringUp() {
    static bool done = false;
    if (std::exchange(done, true))
        return;

    // Asked of whichever engine renders a device, so registered with nothing tied to one.
    installDeviceParameterDisplayTextProviderFactory();

    juce::Logger::writeToLog("[Init] Loading config...");
    Config::getInstance().load();

    juce::Logger::writeToLog("[Init] Loading controller profiles...");
    ControllerProfileRegistry::getInstance().load();

    hookProjectState();
}

}  // namespace magda::app_services
