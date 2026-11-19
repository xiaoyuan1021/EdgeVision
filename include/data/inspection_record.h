#pragma once

#include "data/roi_detection_result.h"
#include <QtGlobal>

enum class InspectionStatus { Pending, Passed, Failed, Skipped };

struct InspectionRecord
{
    ImageDetectionResult result;
    InspectionStatus status = InspectionStatus::Pending;
    qint64 completedAtMs = 0;
    qint64 elapsedMs = 0;
};
