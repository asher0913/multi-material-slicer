#pragma once

#include "ModelTypes.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

struct SliceExportStats {
    int layerCount = 0;
    QVector<int> materialImageCounts;
};

using SliceProgressCallback = std::function<bool(int currentLayer, int totalLayers)>;

class SliceExporter {
public:
    static bool exportSlices(const QVector<ModelInstance>& models,
                             const SliceSettings& settings,
                             const QString& outputRoot,
                             QStringList* materialDirs,
                             SliceExportStats* stats,
                             QString* errorMessage,
                             const SliceProgressCallback& progressCallback = SliceProgressCallback());
};
