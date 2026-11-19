#pragma once

#include "data/inspection_record.h"
#include <QByteArray>
#include <QList>

class InspectionReportExporter
{
public:
    enum class Format { Csv, Json };

    static QByteArray csv(const QList<InspectionRecord>& records);
    static QByteArray json(const QList<InspectionRecord>& records);
    static bool save(const QString& path, const QList<InspectionRecord>& records,
                     Format format, QString* error = nullptr);
};
