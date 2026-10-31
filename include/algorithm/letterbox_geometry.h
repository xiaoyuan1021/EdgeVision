#pragma once

#include <opencv2/core/types.hpp>
#include <algorithm>
#include <cmath>

// YOLO center/size coordinates on the padded input -> clipped source-image box.
// Keep this independent of model execution so geometry can be regression-tested.
inline cv::Rect restoreLetterboxBox(float cx, float cy, float width, float height,
                                    float scale, int padLeft, int padTop,
                                    int sourceWidth, int sourceHeight)
{
    if (!std::isfinite(cx) || !std::isfinite(cy) ||
        !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(scale) || scale <= 0 || width <= 0 || height <= 0 ||
        sourceWidth <= 0 || sourceHeight <= 0) {
        return {};
    }

    // Undo padding before scaling. Clip both endpoints, not origin + old size.
    const auto restore = [](double value, int padding, float ratio, int limit) {
        return std::clamp((value - padding) / ratio, 0.0, static_cast<double>(limit));
    };
    const double left = restore(static_cast<double>(cx) - width / 2.0, padLeft, scale, sourceWidth);
    const double top = restore(static_cast<double>(cy) - height / 2.0, padTop, scale, sourceHeight);
    const double right = restore(static_cast<double>(cx) + width / 2.0, padLeft, scale, sourceWidth);
    const double bottom = restore(static_cast<double>(cy) + height / 2.0, padTop, scale, sourceHeight);
    if (right <= left || bottom <= top) return {};

    // Round outward so the integer box contains the floating-point box.
    const int x = static_cast<int>(std::floor(left));
    const int y = static_cast<int>(std::floor(top));
    return {x, y, static_cast<int>(std::ceil(right)) - x,
                  static_cast<int>(std::ceil(bottom)) - y};
}
