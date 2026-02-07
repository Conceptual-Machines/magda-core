#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <iostream>

namespace magda {

/**
 * @brief Simplified ZoomManager for testing purposes
 *
 * This is a minimal version that focuses only on the core zoom logic
 * without UI dependencies like BinaryData, FontManager, etc.
 */
class SimpleZoomManager {
  public:
    SimpleZoomManager() = default;
    ~SimpleZoomManager() = default;

    // Core zoom operations
    void setZoom(double newZoom) {
        currentZoom = juce::jlimit(minZoom, maxZoom, newZoom);
        if (onZoomChanged)
            onZoomChanged(currentZoom);
    }

    void setZoomCentered(double newZoom, double timePosition) {
        // Set new zoom
        setZoom(newZoom);

        // Calculate new scroll position to keep the time position centered
        int newPixelPosition =
            static_cast<int>(timePosition * currentZoom * viewportWidth / timelineLength);
        int newScrollX = newPixelPosition - viewportWidth / 2;

        setCurrentScrollPosition(newScrollX);
    }

    // Configuration
    void setTimelineLength(double lengthInSeconds) {
        timelineLength = lengthInSeconds;
        if (onContentSizeChanged)
            onContentSizeChanged(calculateContentWidth());
    }

    void setViewportWidth(int width) {
        viewportWidth = width;
        if (onContentSizeChanged)
            onContentSizeChanged(calculateContentWidth());
    }

    void setCurrentScrollPosition(int scrollX) {
        currentScrollX = scrollX;
        if (onScrollChanged)
            onScrollChanged(currentScrollX);
    }

    // Zoom properties
    double getCurrentZoom() const {
        return currentZoom;
    }
    double getMinZoom() const {
        return minZoom;
    }
    double getMaxZoom() const {
        return maxZoom;
    }
    double getTimelineLength() const {
        return timelineLength;
    }
    int getCurrentScrollPosition() const {
        return currentScrollX;
    }

    // Zoom bounds
    void setZoomBounds(double minZoom, double maxZoom) {
        this->minZoom = minZoom;
        this->maxZoom = maxZoom;
        // Clamp current zoom to new bounds
        if (currentZoom < minZoom || currentZoom > maxZoom) {
            setZoom(currentZoom);
        }
    }

    // Callbacks
    std::function<void(double newZoom)> onZoomChanged;
    std::function<void(int scrollX)> onScrollChanged;
    std::function<void(int contentWidth)> onContentSizeChanged;

  private:
    // Zoom and scroll state
    double currentZoom = 1.0;
    double minZoom = 0.1;
    double maxZoom = 100000.0;
    double timelineLength = 0.0;
    int viewportWidth = 800;
    int currentScrollX = 0;

    int calculateContentWidth() const {
        return static_cast<int>(timelineLength * currentZoom * viewportWidth / 100.0);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleZoomManager)
};

}  // namespace magda

using namespace magda;

class ZoomManagerTest final : public juce::UnitTest {
  public:
    ZoomManagerTest() : juce::UnitTest("ZoomManager Logic Tests") {}

    void runTest() override {
        testBasicZoomOperations();
        testZoomBounds();
        testZoomCentered();
        testCallbacks();
    }

  private:
    void testBasicZoomOperations() {
        beginTest("Basic zoom operations");

        SimpleZoomManager zoomManager;
        zoomManager.setTimelineLength(100.0);
        zoomManager.setViewportWidth(800);

        // Test initial state
        expect(zoomManager.getCurrentZoom() == 1.0);
        expect(zoomManager.getTimelineLength() == 100.0);
        expect(zoomManager.getCurrentScrollPosition() == 0);

        // Test zoom change
        zoomManager.setZoom(2.0);
        expect(zoomManager.getCurrentZoom() == 2.0);

        // Test zoom bounds
        zoomManager.setZoomBounds(0.5, 10.0);
        expect(zoomManager.getMinZoom() == 0.5);
        expect(zoomManager.getMaxZoom() == 10.0);

        // Test zoom clamping
        zoomManager.setZoom(0.1);  // Below minimum
        expect(zoomManager.getCurrentZoom() == 0.5);

        zoomManager.setZoom(20.0);  // Above maximum
        expect(zoomManager.getCurrentZoom() == 10.0);
    }

    void testZoomBounds() {
        beginTest("Zoom bounds enforcement");

        SimpleZoomManager zoomManager;
        zoomManager.setZoomBounds(0.25, 4.0);

        // Test setting zoom within bounds
        zoomManager.setZoom(1.5);
        expect(zoomManager.getCurrentZoom() == 1.5);

        // Test setting zoom below minimum
        zoomManager.setZoom(0.1);
        expect(zoomManager.getCurrentZoom() == 0.25);

        // Test setting zoom above maximum
        zoomManager.setZoom(10.0);
        expect(zoomManager.getCurrentZoom() == 4.0);

        // Test changing bounds with current zoom outside new bounds
        zoomManager.setZoom(2.0);
        zoomManager.setZoomBounds(0.5, 1.5);
        expect(zoomManager.getCurrentZoom() == 1.5);  // Should clamp to new max
    }

    void testZoomCentered() {
        beginTest("Centered zoom operations");

        SimpleZoomManager zoomManager;
        zoomManager.setTimelineLength(100.0);
        zoomManager.setViewportWidth(800);

        // Test zoom centered at specific time position
        zoomManager.setZoomCentered(2.0, 50.0);  // Zoom to 2x at 50 seconds
        expect(zoomManager.getCurrentZoom() == 2.0);

        // The scroll position should be adjusted to keep time 50.0 centered
        // This tests the mathematical relationship between zoom and scroll
        int expectedScrollX = static_cast<int>(50.0 * 2.0 * 800 / 100.0) - 800 / 2;
        expect(zoomManager.getCurrentScrollPosition() == expectedScrollX);
    }

    void testCallbacks() {
        beginTest("Callback notifications");

        SimpleZoomManager zoomManager;

        bool zoomCallbackCalled = false;
        bool scrollCallbackCalled = false;
        bool contentSizeCallbackCalled = false;

        double receivedZoom = 0.0;
        int receivedScrollX = 0;
        int receivedContentWidth = 0;

        // Set up callbacks
        zoomManager.onZoomChanged = [&](double newZoom) {
            zoomCallbackCalled = true;
            receivedZoom = newZoom;
        };

        zoomManager.onScrollChanged = [&](int scrollX) {
            scrollCallbackCalled = true;
            receivedScrollX = scrollX;
        };

        zoomManager.onContentSizeChanged = [&](int contentWidth) {
            contentSizeCallbackCalled = true;
            receivedContentWidth = contentWidth;
        };

        // Test zoom callback
        zoomManager.setZoom(1.5);
        expect(zoomCallbackCalled);
        expect(receivedZoom == 1.5);

        // Test scroll callback
        zoomManager.setCurrentScrollPosition(100);
        expect(scrollCallbackCalled);
        expect(receivedScrollX == 100);

        // Test content size callback by changing timeline length
        zoomManager.setTimelineLength(200.0);
        expect(contentSizeCallbackCalled);
        expect(receivedContentWidth > 0);
    }
};

// Static instance for auto-registration
static ZoomManagerTest zoomManagerTest;
