#include "ConfigWriter.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

namespace {

QString yamlBool(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString yamlPath(const QString& path)
{
    QString normalized = QDir::fromNativeSeparators(QDir(path).absolutePath());
    normalized.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(normalized);
}

QString yamlScalar(QString value, bool forceQuote)
{
    value = value.trimmed();
    if (forceQuote) {
        value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
        value.replace(QStringLiteral("\""), QStringLiteral("\\\""));
        return QStringLiteral("\"%1\"").arg(value);
    }
    if (value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("true");
    }
    if (value.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("false");
    }
    bool numberOk = false;
    value.toDouble(&numberOk);
    if (numberOk) {
        return value;
    }
    if (value.isEmpty()) {
        return QStringLiteral("\"\"");
    }
    value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    value.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(value);
}

bool isTransitionMetaKey(const QString& key)
{
    return key == QStringLiteral("material_transition_enabled")
        || key == QStringLiteral("material_transition_current")
        || key == QStringLiteral("material_transition_time");
}

bool isTransitionPairKey(const QString& key)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^m\d+Tom\d+(?:_current|_time)?$)"));
    return re.match(key).hasMatch();
}

bool isUiManagedConfigKey(const QString& key)
{
    static const QSet<QString> exactKeys = {
        QStringLiteral("machine_name"),
        QStringLiteral("material_num"),
        QStringLiteral("material_count"),
        QStringLiteral("output_width"),
        QStringLiteral("output_height"),
        QStringLiteral("res_width"),
        QStringLiteral("res_height"),
        QStringLiteral("projector_width"),
        QStringLiteral("projector_height"),
        QStringLiteral("pixel_size_mm"),
        QStringLiteral("layer_height"),
        QStringLiteral("bottom_layers"),
        QStringLiteral("output_dir"),
        QStringLiteral("groove"),
        QStringLiteral("slide_0"),
        QStringLiteral("slide_1"),
        QStringLiteral("slide_2"),
    };
    if (exactKeys.contains(key)) {
        return true;
    }

    static const QStringList prefixes = {
        QStringLiteral("img_path_"),
        QStringLiteral("bottom_exposure_time_"),
        QStringLiteral("bottom_exposure_current_"),
        QStringLiteral("bottom_exposure_strength_"),
        QStringLiteral("standard_exposure_time_"),
        QStringLiteral("standard_exposure_current_"),
        QStringLiteral("standard_exposure_strength_"),
        QStringLiteral("material_preset_"),
    };
    for (const QString& prefix : prefixes) {
        if (key.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

bool configBoolValue(const QString& raw, bool fallback)
{
    const QString value = raw.trimmed().toLower();
    if (value == QStringLiteral("true") || value == QStringLiteral("1")
        || value == QStringLiteral("yes") || value == QStringLiteral("on")) {
        return true;
    }
    if (value == QStringLiteral("false") || value == QStringLiteral("0")
        || value == QStringLiteral("no") || value == QStringLiteral("off")) {
        return false;
    }
    return fallback;
}

QString numericConfigValue(const QMap<QString, QString>& values,
                           const QString& primaryKey,
                           const QString& fallbackKey,
                           const QString& hardFallback)
{
    const QString raw = values.value(primaryKey,
        values.value(fallbackKey, hardFallback)).trimmed();
    bool ok = false;
    raw.toDouble(&ok);
    return ok ? raw : hardFallback;
}

void writeAdvancedConfigParams(QTextStream& out, const QMap<QString, QString>& values)
{
    QSet<QString> written;
    for (const AdvancedConfigParam& param : defaultAdvancedConfigParams()) {
        written.insert(param.key);
        if (isTransitionMetaKey(param.key) || isTransitionPairKey(param.key)) {
            continue;
        }
        const QString value = values.value(param.key, param.defaultValue);
        out << param.key << ": " << yamlScalar(value, param.quoteValue) << "\n";
    }

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const QString key = it.key().trimmed();
        if (key.isEmpty() || written.contains(key)
            || isUiManagedConfigKey(key)
            || isTransitionMetaKey(key) || isTransitionPairKey(key)) {
            continue;
        }
        out << key << ": " << yamlScalar(it.value(), false) << "\n";
    }
}

} // namespace

bool ConfigWriter::writeYaml(const SliceSettings& settings,
                             const QStringList& materialDirs,
                             const QString& configPath,
                             QString* errorMessage)
{
    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QTextStream out(&file);
    out.setCodec("UTF-8");
    if (!settings.selectedMachineName.isEmpty()) {
        out << "machine_name: \"" << settings.selectedMachineName << "\"\n";
    }
    out << "layer_height: " << settings.layerHeightMm << "\n";
    out << "pixel_size_mm: " << settings.pixelSizeMm << "\n";
    out << "output_width: " << settings.outputWidth << "\n";
    out << "output_height: " << settings.outputHeight << "\n";
    out << "material_num: " << settings.materialCount << "\n";

    for (int i = 0; i < settings.materialCount; ++i) {
        const QString dir = i < materialDirs.size() ? materialDirs[i] : QString();
        out << "img_path_" << i << ": " << yamlPath(dir) << "\n";
    }

    out << "bottom_layers: " << settings.bottomLayers << "\n";
    for (int i = 0; i < settings.materialCount; ++i) {
        const MaterialExposure exposure = i < settings.materials.size()
            ? settings.materials[i]
            : MaterialExposure();
        // The UI works in light strength; convert to the integer machine current
        // the backend / GCode expects, using the selected machine's map.
        const int bottomCurrent = settings.selectedMachine.currentFromStrength(exposure.bottomExposureStrength);
        const int standardCurrent = settings.selectedMachine.currentFromStrength(exposure.standardExposureStrength);

        out << "bottom_exposure_time_" << i << ": " << exposure.bottomExposureTime << "\n";
        out << "bottom_exposure_current_" << i << ": " << bottomCurrent << "\n";
        out << "standard_exposure_time_" << i << ": " << exposure.standardExposureTime << "\n";
        out << "standard_exposure_current_" << i << ": " << standardCurrent << "\n";
        // Also record the light strength for debugging / round-trip.
        out << "bottom_exposure_strength_" << i << ": " << exposure.bottomExposureStrength << "\n";
        out << "standard_exposure_strength_" << i << ": " << exposure.standardExposureStrength << "\n";
        if (!exposure.presetId.isEmpty()) {
            out << "material_preset_" << i << ": " << yamlScalar(exposure.presetId, true) << "\n";
        }
    }

    // Regular options come from the UI.
    out << "groove: " << yamlBool(settings.groove) << "\n";
    out << "slide_0: " << yamlBool(settings.slide0) << "\n";
    out << "slide_1: " << yamlBool(settings.slide1) << "\n";
    out << "slide_2: " << yamlBool(settings.slide2) << "\n";

    // Machine/GCode advanced parameters come from the editable table.
    writeAdvancedConfigParams(out, settings.advancedParams);

    for (int src = 0; src < settings.materialCount; ++src) {
        for (int dst = 0; dst < settings.materialCount; ++dst) {
            if (src == dst) {
                continue;
            }
            const QString pairKey = QStringLiteral("m%1Tom%2").arg(src).arg(dst);
            const bool enabled = configBoolValue(settings.advancedParams.value(
                                                    pairKey,
                                                    settings.advancedParams.value(
                                                        QStringLiteral("material_transition_enabled"),
                                                        QStringLiteral("true"))),
                                                true);
            const QString current = numericConfigValue(settings.advancedParams,
                                                       pairKey + QStringLiteral("_current"),
                                                       QStringLiteral("material_transition_current"),
                                                       QStringLiteral("18"));
            const QString time = numericConfigValue(settings.advancedParams,
                                                    pairKey + QStringLiteral("_time"),
                                                    QStringLiteral("material_transition_time"),
                                                    QStringLiteral("2.0"));
            out << pairKey << ": " << yamlBool(enabled) << "\n";
            out << pairKey << "_current: " << current << "\n";
            out << pairKey << "_time: " << time << "\n";
        }
    }

    return true;
}
