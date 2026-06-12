#pragma once

#include <juce_core/juce_core.h>

namespace magda {

/**
 * @brief Type of curve interpolation between points
 */
enum class CurveType { Linear, Bezier, Step, HardCorner };

/**
 * @brief Drawing/editing mode for curve editors
 */
enum class CurveDrawMode { Select, Pencil, Line, Curve };

/**
 * @brief Bezier handle data for smooth curve control
 *
 * Handles are offsets relative to their parent point.
 * When linked=true, moving one handle mirrors the other.
 */
struct CurveHandleData {
    double x = 0.0;      // X offset from point (normalized or time)
    double y = 0.0;      // Y offset from point (normalized value)
    bool linked = true;  // Mirror handles when one is moved

    bool isZero() const {
        return x == 0.0 && y == 0.0;
    }
};

/**
 * @brief A single point on an editable curve
 *
 * Generic representation used by both automation and LFO editors.
 * X coordinate represents position (time or phase).
 * Y coordinate represents value (0-1 normalized).
 */
struct CurvePoint {
    uint32_t id = 0;
    double x = 0.0;  // Position (time in seconds or phase 0-1)
    double y = 0.5;  // Normalized value 0-1
    CurveType curveType = CurveType::Linear;
    double tension = 0.0;  // -3 to +3 for curve shape
    CurveHandleData inHandle;
    CurveHandleData outHandle;

    bool operator<(const CurvePoint& other) const {
        return x < other.x;
    }

    bool operator==(const CurvePoint& other) const {
        return id == other.id;
    }
};

/**
 * @brief Invalid point ID constant
 */
constexpr uint32_t INVALID_CURVE_POINT_ID = 0xFFFFFFFF;

/**
 * @brief Convert between CurveType and numeric representation
 */
inline int curveTypeToInt(CurveType type) {
    switch (type) {
        case CurveType::Linear:
            return 0;
        case CurveType::Bezier:
            return 1;
        case CurveType::Step:
            return 2;
        case CurveType::HardCorner:
            return 3;
    }
    return 0;
}

inline CurveType intToCurveType(int value) {
    switch (value) {
        case 0:
            return CurveType::Linear;
        case 1:
            return CurveType::Bezier;
        case 2:
            return CurveType::Step;
        case 3:
            return CurveType::HardCorner;
        default:
            return CurveType::Linear;
    }
}

/**
 * @brief Get display name for curve type
 */
inline const char* getCurveTypeName(CurveType type) {
    switch (type) {
        case CurveType::Linear:
            return "Linear";
        case CurveType::Bezier:
            return "Bezier";
        case CurveType::Step:
            return "Step";
        case CurveType::HardCorner:
            return "Hard Corner";
    }
    return "Unknown";
}

/**
 * @brief Get display name for draw mode
 */
inline const char* getDrawModeName(CurveDrawMode mode) {
    switch (mode) {
        case CurveDrawMode::Select:
            return "Select";
        case CurveDrawMode::Pencil:
            return "Pencil";
        case CurveDrawMode::Line:
            return "Line";
        case CurveDrawMode::Curve:
            return "Curve";
    }
    return "Unknown";
}

}  // namespace magda
