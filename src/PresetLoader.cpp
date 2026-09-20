#include "PresetLoader.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <cmath>

namespace {

QString unquote(const QString& raw)
{
    QString v = raw.trimmed();
    if (v.size() >= 2 && ((v.startsWith('"') && v.endsWith('"')) || (v.startsWith('\'') && v.endsWith('\'')))) {
        v = v.mid(1, v.size() - 2);
    }
    return v;
}

QStringList splitList(const QString& value)
{
    QStringList out;
    const auto parts = value.split(',', QString::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString t = part.trimmed();
        if (!t.isEmpty()) {
            out << t;
        }
    }
    return out;
}

QVector<double> parseDoubleList(const QString& value, QStringList* errors, const QString& context)
{
    QVector<double> out;
    const auto parts = value.split(',', QString::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const QString token = unquote(part);
        const double d = token.toDouble(&ok);
        if (ok && std::isfinite(d)) {
            out << d;
        } else if (errors) {
            errors->append(QStringLiteral("%1: invalid number '%2'").arg(context, token));
        }
    }
    return out;
}

QVector<int> parseIntList(const QString& value, QStringList* errors, const QString& context)
{
    QVector<int> out;
    const auto parts = value.split(',', QString::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const QString token = unquote(part);
        const double d = token.toDouble(&ok);
        if (ok && std::isfinite(d)) {
            out << static_cast<int>(std::lround(d));
        } else if (errors) {
            errors->append(QStringLiteral("%1: invalid integer '%2'").arg(context, token));
        }
    }
    return out;
}

int leadingWhitespace(const QString& line)
{
    int n = 0;
    while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) {
        ++n;
    }
    return n;
}

} // namespace

bool PresetLoader::load(const QString& path, PresetLibrary* outLibrary, QString* errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法打开配置文件: %1").arg(path);
        }
        return false;
    }
    QTextStream in(&file);
    in.setCodec("UTF-8");
    const QStringList lines = in.readAll().split('\n');
    file.close();

    PresetLibrary lib;
    lib.sourcePath = path;

    // Pass 1: discover the machine and material names so we can recognise blocks.
    QStringList machineNames;
    QStringList materialNames;
    for (const QString& raw : lines) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }
        if (leadingWhitespace(raw) != 0) {
            continue;
        }
        const int colon = trimmed.indexOf(':');
        if (colon < 0) {
            continue;
        }
        const QString key = trimmed.left(colon).trimmed();
        const QString value = trimmed.mid(colon + 1).trimmed();
        if (key == QStringLiteral("device_name_list")) {
            machineNames = splitList(value);
        } else if (key == QStringLiteral("material_name_list")) {
            materialNames = splitList(value);
        }
    }

    for (const QString& name : machineNames) {
        MachinePreset m;
        m.id = name;
        m.displayName = name;
        lib.machines.append(m);
    }
    for (const QString& name : materialNames) {
        MaterialPreset m;
        m.id = name;
        m.displayName = name;
        lib.materials.append(m);
    }

    auto machinePtr = [&lib](const QString& id) -> MachinePreset* {
        for (MachinePreset& m : lib.machines) {
            if (m.id == id) {
                return &m;
            }
        }
        return nullptr;
    };
    auto materialPtr = [&lib](const QString& id) -> MaterialPreset* {
        for (MaterialPreset& m : lib.materials) {
            if (m.id == id) {
                return &m;
            }
        }
        return nullptr;
    };

    // Pass 2: fill in block fields.
    enum class Mode { None, Machine, Material };
    Mode mode = Mode::None;
    MachinePreset* machine = nullptr;
    MaterialPreset* material = nullptr;
    QStringList errors;

    auto parseIntField = [&errors](const QString& value, const QString& context, int* target) {
        bool ok = false;
        const QString token = unquote(value);
        const int parsed = token.toInt(&ok);
        if (!ok) {
            errors.append(QStringLiteral("%1: invalid integer '%2'").arg(context, token));
            return;
        }
        *target = parsed;
    };

    auto parseDoubleField = [&errors](const QString& value, const QString& context, double* target) {
        bool ok = false;
        const QString token = unquote(value);
        const double parsed = token.toDouble(&ok);
        if (!ok || !std::isfinite(parsed)) {
            errors.append(QStringLiteral("%1: invalid number '%2'").arg(context, token));
            return;
        }
        *target = parsed;
    };

    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString& raw = lines[lineIndex];
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }
        const int colon = trimmed.indexOf(':');
        if (colon < 0) {
            continue;
        }
        const QString key = trimmed.left(colon).trimmed();
        const QString value = trimmed.mid(colon + 1).trimmed();
        const bool topLevel = leadingWhitespace(raw) == 0;
        const QString context = QStringLiteral("line %1 '%2'").arg(lineIndex + 1).arg(key);

        if (topLevel) {
            if (key == QStringLiteral("device_name_list") || key == QStringLiteral("material_name_list")) {
                mode = Mode::None;
                continue;
            }
            if (machineNames.contains(key)) {
                machine = machinePtr(key);
                mode = Mode::Machine;
                continue;
            }
            if (materialNames.contains(key)) {
                material = materialPtr(key);
                mode = Mode::Material;
                continue;
            }
            mode = Mode::None;
            continue;
        }

        // Nested field.
        if (mode == Mode::Machine && machine) {
            if (key == QStringLiteral("projector_width")) {
                parseIntField(value, context, &machine->projectorWidth);
            } else if (key == QStringLiteral("projector_height")) {
                parseIntField(value, context, &machine->projectorHeight);
            } else if (key == QStringLiteral("pixel_width")) {
                parseDoubleField(value, context, &machine->pixelWidthMm);
            } else if (key == QStringLiteral("pixel_height")) {
                parseDoubleField(value, context, &machine->pixelHeightMm);
            } else if (key == QStringLiteral("current_map")) {
                machine->currentMap = parseIntList(value, &errors, context);
            } else if (key == QStringLiteral("strength_map")) {
                machine->strengthMap = parseDoubleList(value, &errors, context);
            } else if (key == QStringLiteral("material_num_limit")) {
                parseIntField(value, context, &machine->materialNumLimit);
            } else if (key == QStringLiteral("name") || key == QStringLiteral("display_name")) {
                machine->displayName = unquote(value);
            }
        } else if (mode == Mode::Material && material) {
            if (key == QStringLiteral("color")) {
                const QColor c(unquote(value));
                if (c.isValid()) {
                    material->color = c;
                }
            } else if (key == QStringLiteral("bottom_exposure_time")) {
                parseDoubleField(value, context, &material->bottomExposureTime);
            } else if (key == QStringLiteral("standard_exposure_time")) {
                parseDoubleField(value, context, &material->standardExposureTime);
            } else if (key == QStringLiteral("bottom_exposure_strength")) {
                parseDoubleField(value, context, &material->bottomExposureStrength);
                material->hasStrength = true;
            } else if (key == QStringLiteral("standard_exposure_strength")) {
                parseDoubleField(value, context, &material->standardExposureStrength);
                material->hasStrength = true;
            } else if (key == QStringLiteral("bottom_exposure_current")) {
                double parsed = material->legacyBottomExposureCurrent;
                parseDoubleField(value, context, &parsed);
                material->legacyBottomExposureCurrent = static_cast<int>(std::lround(parsed));
            } else if (key == QStringLiteral("standard_exposure_current")) {
                double parsed = material->legacyStandardExposureCurrent;
                parseDoubleField(value, context, &parsed);
                material->legacyStandardExposureCurrent = static_cast<int>(std::lround(parsed));
            } else if (key == QStringLiteral("name") || key == QStringLiteral("display_name")) {
                material->displayName = unquote(value);
            }
        }
    }

    for (const MachinePreset& m : lib.machines) {
        const QString name = m.id.isEmpty() ? QStringLiteral("<unnamed machine>") : m.id;
        if (m.projectorWidth <= 0) {
            errors.append(QStringLiteral("%1: projector_width must be > 0").arg(name));
        }
        if (m.projectorHeight <= 0) {
            errors.append(QStringLiteral("%1: projector_height must be > 0").arg(name));
        }
        if (!std::isfinite(m.pixelWidthMm) || m.pixelWidthMm <= 0.0) {
            errors.append(QStringLiteral("%1: pixel_width must be > 0").arg(name));
        }
        if (!std::isfinite(m.pixelHeightMm) || m.pixelHeightMm <= 0.0) {
            errors.append(QStringLiteral("%1: pixel_height must be > 0").arg(name));
        }
        if (m.materialNumLimit <= 0) {
            errors.append(QStringLiteral("%1: material_num_limit must be > 0").arg(name));
        }
        if ((!m.currentMap.isEmpty() || !m.strengthMap.isEmpty()) && !m.hasMap()) {
            errors.append(QStringLiteral("%1: current_map and strength_map must both exist and have the same length").arg(name));
        }
    }
    for (const MaterialPreset& m : lib.materials) {
        const QString name = m.id.isEmpty() ? QStringLiteral("<unnamed material>") : m.id;
        if (!std::isfinite(m.bottomExposureTime) || m.bottomExposureTime < 0.0) {
            errors.append(QStringLiteral("%1: bottom_exposure_time must be >= 0").arg(name));
        }
        if (!std::isfinite(m.standardExposureTime) || m.standardExposureTime < 0.0) {
            errors.append(QStringLiteral("%1: standard_exposure_time must be >= 0").arg(name));
        }
        if (!std::isfinite(m.bottomExposureStrength) || !std::isfinite(m.standardExposureStrength)) {
            errors.append(QStringLiteral("%1: exposure strength values must be finite").arg(name));
        }
    }

    if (!errors.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("配置文件包含非法数值:\n%1").arg(errors.join(QStringLiteral("\n")));
        }
        return false;
    }

    if (!lib.isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("配置文件中未找到任何机器 (device_name_list)。");
        }
        return false;
    }

    if (outLibrary) {
        *outLibrary = lib;
    }
    return true;
}
