#pragma once

#include "ModelTypes.h"

#include <QString>
#include <QStringList>

class ConfigWriter {
public:
    static bool writeYaml(const SliceSettings& settings,
                          const QStringList& materialDirs,
                          const QString& configPath,
                          QString* errorMessage);
};
