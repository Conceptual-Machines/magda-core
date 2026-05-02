#pragma once

namespace magda {

/**
 * @brief Type of automation lane
 *
 * Absolute lanes have a single curve spanning the entire timeline.
 * ClipBased lanes contain automation clips that can be moved, looped, and stretched.
 */
enum class AutomationLaneType {
    Absolute,  // Single curve spanning entire timeline
    ClipBased  // Automation clips that can loop/stretch
};

/**
 * @brief Curve interpolation type between automation points
 */
enum class AutomationCurveType {
    Linear,  // Straight line between points
    Bezier,  // Smooth bezier curve with control handles
    Step     // Instant jump to next value (no interpolation)
};

/**
 * @brief Drawing/editing mode for automation curves
 */
enum class AutomationDrawMode {
    Select,  // Select and move existing points
    Pencil,  // Freehand drawing creates points
    Line,    // Draw straight line segments
    Curve    // Draw smooth curves
};

/**
 * @brief Get display name for lane type
 */
inline const char* getLaneTypeName(AutomationLaneType type) {
    switch (type) {
        case AutomationLaneType::Absolute:
            return "Absolute";
        case AutomationLaneType::ClipBased:
            return "Clip-Based";
    }
    return "Unknown";
}

/**
 * @brief Get display name for curve type
 */
inline const char* getCurveTypeName(AutomationCurveType type) {
    switch (type) {
        case AutomationCurveType::Linear:
            return "Linear";
        case AutomationCurveType::Bezier:
            return "Bezier";
        case AutomationCurveType::Step:
            return "Step";
    }
    return "Unknown";
}

/**
 * @brief Get display name for draw mode
 */
inline const char* getDrawModeName(AutomationDrawMode mode) {
    switch (mode) {
        case AutomationDrawMode::Select:
            return "Select";
        case AutomationDrawMode::Pencil:
            return "Pencil";
        case AutomationDrawMode::Line:
            return "Line";
        case AutomationDrawMode::Curve:
            return "Curve";
    }
    return "Unknown";
}

}  // namespace magda
