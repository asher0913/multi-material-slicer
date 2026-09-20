#pragma once

#include "PresetLibrary.h"
#include "StlMesh.h"

#include <QColor>
#include <QMap>
#include <QMatrix4x4>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>

struct Transform {
    QVector3D translation = QVector3D(0.0f, 0.0f, 0.0f);
    QVector3D rotationDeg = QVector3D(0.0f, 0.0f, 0.0f);
    float scale = 1.0f;

    QMatrix4x4 matrix() const
    {
        QMatrix4x4 m;
        m.translate(translation);
        m.rotate(rotationDeg.z(), 0.0f, 0.0f, 1.0f);
        m.rotate(rotationDeg.y(), 0.0f, 1.0f, 0.0f);
        m.rotate(rotationDeg.x(), 1.0f, 0.0f, 0.0f);
        m.scale(scale);
        return m;
    }
};

struct ModelInstance {
    QString id;
    QString filePath;
    QString name;
    QSharedPointer<StlMesh> mesh;
    Transform transform;
    QMatrix4x4 effectiveMatrixOverride;
    int materialIndex = 0;
    QColor color = QColor(44, 160, 220);
    bool visible = true;
    bool hasEffectiveMatrixOverride = false;

    // STEP assemblies are represented as one tree parent plus several leaf
    // models. Each leaf keeps its own material; nested assembly transforms are
    // composed into effectiveMatrixOverride for rendering and slicing.
    bool isAssemblyChild = false;
    QString assemblyId;
    QString assemblyName;

    QMatrix4x4 effectiveMatrix() const
    {
        return hasEffectiveMatrixOverride ? effectiveMatrixOverride : transform.matrix();
    }
};

// Exposure parameters that can differ per material slot.
// The UI works in "light strength"; the machine current is derived from strength
// via the selected machine's strength/current map at export time.
struct MaterialExposure {
    QString presetId;  // chosen material preset for this slot (optional)
    double bottomExposureTime = 7.0;
    double bottomExposureStrength = 0.0;
    double standardExposureTime = 2.5;
    double standardExposureStrength = 0.0;
};

struct AdvancedConfigParam {
    QString key;
    QString defaultValue;
    bool quoteValue = false;
};

inline QVector<AdvancedConfigParam> defaultAdvancedConfigParams()
{
    return {
        { QStringLiteral("back_distance"), QStringLiteral("5"), false },
        { QStringLiteral("slider_height"), QStringLiteral("5"), false },
        { QStringLiteral("max_height"), QStringLiteral("165"), false },
        { QStringLiteral("min_height"), QStringLiteral("-50"), false },
        { QStringLiteral("z_acc_h"), QStringLiteral("100.0"), false },
        { QStringLiteral("z_dec_h"), QStringLiteral("-100.0"), false },
        { QStringLiteral("z_speed_h"), QStringLiteral("450.0"), false },
        { QStringLiteral("z_acc_l"), QStringLiteral("50.0"), false },
        { QStringLiteral("z_dec_l"), QStringLiteral("-50.0"), false },
        { QStringLiteral("z_speed_l"), QStringLiteral("100.0"), false },
        { QStringLiteral("r_acc"), QStringLiteral("60.0"), false },
        { QStringLiteral("r_dec"), QStringLiteral("-60.0"), false },
        { QStringLiteral("r_speed"), QStringLiteral("120.0"), false },
        { QStringLiteral("clean_tank"), QStringLiteral("3"), false },
        { QStringLiteral("clean_tank_height"), QStringLiteral("20"), false },
        { QStringLiteral("clean_height"), QStringLiteral("25"), false },
        { QStringLiteral("clean_height_coefficient"), QStringLiteral("0.95"), false },
        { QStringLiteral("clean_times"), QStringLiteral("3"), false },
        { QStringLiteral("clean_time"), QStringLiteral("5.0"), false },
        { QStringLiteral("clean_distance"), QStringLiteral("10"), false },
        { QStringLiteral("dry_tank"), QStringLiteral("4"), false },
        { QStringLiteral("dry_height"), QStringLiteral("60"), false },
        { QStringLiteral("dry_time_bottom"), QStringLiteral("210"), false },
        { QStringLiteral("dry_time_standard"), QStringLiteral("200"), false },
        { QStringLiteral("pre_z_height"), QStringLiteral("10"), false },
        { QStringLiteral("drop_time_bottom"), QStringLiteral("20"), false },
        { QStringLiteral("drop_layers_bottom"), QStringLiteral("20"), false },
        { QStringLiteral("drop_time_standard"), QStringLiteral("10"), false },
        { QStringLiteral("ASS"), QStringLiteral("false"), false },
        { QStringLiteral("ASS_times"), QStringLiteral("100"), false },
        { QStringLiteral("ASS_volume"), QStringLiteral("470"), false },
        { QStringLiteral("AMS"), QStringLiteral("false"), false },
        { QStringLiteral("AMS0_layers"), QStringLiteral("-1 -1"), true },
        { QStringLiteral("AMS1_layers"), QStringLiteral("-1 -1"), true },
        { QStringLiteral("AMS2_layers"), QStringLiteral("-1 -1"), true },
        { QStringLiteral("AMS_tank_offset"), QStringLiteral("2.5"), false },
        { QStringLiteral("AMS_volume"), QStringLiteral("30"), false },
        { QStringLiteral("plate_rotate_height"), QStringLiteral("140"), false },
        { QStringLiteral("glass_rotate_height"), QStringLiteral("-50"), false },
        { QStringLiteral("lapse"), QStringLiteral("false"), false },
        { QStringLiteral("lapse_height"), QStringLiteral("160"), false },
        { QStringLiteral("lapse_tank"), QStringLiteral("0"), false },
        { QStringLiteral("alpha"), QStringLiteral("false"), false },
        { QStringLiteral("beta"), QStringLiteral("false"), false },
        { QStringLiteral("block_size"), QStringLiteral("16"), false },
        { QStringLiteral("wait_after_exposure"), QStringLiteral("0.5"), false },
        { QStringLiteral("wait_before_exposure"), QStringLiteral("0.5"), false },
        { QStringLiteral("finished_clean"), QStringLiteral("false"), false },
        { QStringLiteral("material_transition_enabled"), QStringLiteral("true"), false },
        { QStringLiteral("material_transition_current"), QStringLiteral("18"), false },
        { QStringLiteral("material_transition_time"), QStringLiteral("2.0"), false },
    };
}

struct SliceSettings {
    int outputWidth = 3840;
    int outputHeight = 2160;
    double pixelSizeMm = 0.05;
    double layerHeightMm = 0.05;
    int materialCount = 3;

    // Selected machine; selectedMachine carries the strength->current map so the
    // background worker can convert without touching the UI.
    QString selectedMachineName;
    MachinePreset selectedMachine;

    int bottomLayers = 3;
    // One entry per material slot; size is kept in sync with materialCount.
    QVector<MaterialExposure> materials;

    // Advanced options (written verbatim into config.yaml for the backend).
    bool groove = false;
    bool slide0 = false;
    bool slide1 = false;
    bool slide2 = false;

    // Machine/GCode advanced parameters. Values are kept as strings so manually
    // imported config.yaml files can round-trip uncommon numeric/bool formats.
    QMap<QString, QString> advancedParams;

    // Backend command-line tool. In production this is an executable; in dev mode
    // it may be a `.py` script, in which case pythonPath is used to run it.
    QString mergeToolPath;
    QString pythonPath = QStringLiteral("python3");
    QString outputDir;
};
