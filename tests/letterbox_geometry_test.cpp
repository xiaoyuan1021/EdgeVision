#include "algorithm/letterbox_geometry.h"
#include <iostream>
#include <limits>

int main()
{
    int failures = 0;
    const auto check = [&](const char* name, const cv::Rect& actual, const cv::Rect& expected) {
        if (actual != expected) {
            std::cerr << "FAIL " << name << ": " << actual.x << ',' << actual.y
                      << ',' << actual.width << ',' << actual.height << '\n';
            ++failures;
        } else {
            std::cout << "PASS " << name << '\n';
        }
    };
    check("landscape padding", restoreLetterboxBox(100, 215, 100, 50, .5f, 0, 140, 1280, 720), {100, 100, 200, 100});
    check("portrait padding", restoreLetterboxBox(215, 100, 50, 100, .5f, 140, 0, 720, 1280), {100, 100, 100, 200});
    check("square no padding", restoreLetterboxBox(100, 75, 100, 50, .5f, 0, 0, 1280, 1280), {100, 100, 200, 100});
    check("non-square model input", restoreLetterboxBox(100, 135, 100, 50, .5f, 0, 60, 1280, 720), {100, 100, 200, 100});
    check("odd vertical padding", restoreLetterboxBox(100, 215, 100, 50, .5f, 0, 139, 1280, 723), {100, 102, 200, 100});
    check("clip left and top", restoreLetterboxBox(0, 140, 100, 100, .5f, 0, 140, 1280, 720), {0, 0, 100, 100});
    check("clip right and bottom", restoreLetterboxBox(640, 500, 100, 100, .5f, 0, 140, 1280, 720), {1180, 620, 100, 100});
    check("entirely in padding", restoreLetterboxBox(100, 20, 20, 20, .5f, 0, 140, 1280, 720), {});
    check("outside image", restoreLetterboxBox(700, 300, 20, 20, .5f, 0, 140, 1280, 720), {});
    check("negative size", restoreLetterboxBox(100, 215, -10, 50, .5f, 0, 140, 1280, 720), {});
    check("non-finite output", restoreLetterboxBox(std::numeric_limits<float>::quiet_NaN(), 215, 100, 50, .5f, 0, 140, 1280, 720), {});
    check("invalid scale", restoreLetterboxBox(100, 215, 100, 50, 0, 0, 140, 1280, 720), {});
    check("fractional coordinates", restoreLetterboxBox(10.5f, 10.5f, 2, 2, 1, 0, 0, 640, 640), {9, 9, 3, 3});
    return failures == 0 ? 0 : 1;
}
