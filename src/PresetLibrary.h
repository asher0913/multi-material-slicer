#pragma once

#include <QColor>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <utility>

// One printer/machine preset loaded from the YAML preset library.
struct MachinePreset {
    QString id;
    QString displayName;
    int projectorWidth = 3840;
    int projectorHeight = 2160;
    double pixelWidthMm = 0.05;
    double pixelHeightMm = 0.05;
    int materialNumLimit = 3;
    QVector<int> currentMap;      // machine current points
    QVector<double> strengthMap;  // light-strength points, parallel to currentMap

    bool hasMap() const
    {
        return !currentMap.isEmpty() && currentMap.size() == strengthMap.size();
    }

    // Light strength -> machine current. Linear interpolation between the two
    // bracketing strength points; clamped to the end points outside the range.
    // Result is rounded to an integer (the backend / GCode uses integer current).
    int currentFromStrength(double strength, bool* outOfRange = nullptr) const
    {
        if (outOfRange) {
            *outOfRange = false;
        }
        if (!std::isfinite(strength)) {
            if (outOfRange) {
                *outOfRange = true;
            }
            return hasMap() ? currentMap.first() : 0;
        }
        if (!hasMap()) {
            return static_cast<int>(std::lround(strength));
        }
        QVector<std::pair<double, int>> points;
        points.reserve(strengthMap.size());
        for (int i = 0; i < strengthMap.size(); ++i) {
            if (std::isfinite(strengthMap[i])) {
                points.append(std::make_pair(strengthMap[i], currentMap[i]));
            }
        }
        if (points.isEmpty()) {
            return static_cast<int>(std::lround(strength));
        }
        std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

        const int n = points.size();
        if (strength <= points.first().first) {
            if (outOfRange && strength < points.first().first) {
                *outOfRange = true;
            }
            return points.first().second;
        }
        if (strength >= points.last().first) {
            if (outOfRange && strength > points.last().first) {
                *outOfRange = true;
            }
            return points.last().second;
        }
        for (int i = 0; i + 1 < n; ++i) {
            const double s0 = points[i].first;
            const double s1 = points[i + 1].first;
            if (strength >= s0 && strength <= s1) {
                const double t = (s1 == s0) ? 0.0 : (strength - s0) / (s1 - s0);
                const double cur = points[i].second + t * (points[i + 1].second - points[i].second);
                return static_cast<int>(std::lround(cur));
            }
        }
        return points.last().second;
    }

    // Machine current -> light strength. Used to show a strength value for legacy
    // materials that only provide a current.
    double strengthFromCurrent(double current) const
    {
        if (!hasMap()) {
            return current;
        }
        if (!std::isfinite(current)) {
            return strengthMap.isEmpty() ? 0.0 : strengthMap.first();
        }
        QVector<std::pair<int, double>> points;
        points.reserve(currentMap.size());
        for (int i = 0; i < currentMap.size(); ++i) {
            if (std::isfinite(strengthMap[i])) {
                points.append(std::make_pair(currentMap[i], strengthMap[i]));
            }
        }
        if (points.isEmpty()) {
            return current;
        }
        std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

        const int n = points.size();
        if (current <= points.first().first) {
            return points.first().second;
        }
        if (current >= points.last().first) {
            return points.last().second;
        }
        for (int i = 0; i + 1 < n; ++i) {
            const double c0 = points[i].first;
            const double c1 = points[i + 1].first;
            if (current >= c0 && current <= c1) {
                const double t = (c1 == c0) ? 0.0 : (current - c0) / (c1 - c0);
                return points[i].second + t * (points[i + 1].second - points[i].second);
            }
        }
        return points.last().second;
    }
};

// One material preset loaded from the YAML preset library.
struct MaterialPreset {
    QString id;
    QString displayName;
    QColor color;  // invalid QColor() if the config does not specify one
    double bottomExposureTime = 7.0;
    double standardExposureTime = 2.5;

    // If the config gave strength directly, hasStrength is true and these hold it.
    bool hasStrength = false;
    double bottomExposureStrength = 0.0;
    double standardExposureStrength = 0.0;

    // Legacy: config that only gives current.
    int legacyBottomExposureCurrent = 15;
    int legacyStandardExposureCurrent = 15;
};

struct PresetLibrary {
    QString sourcePath;
    QVector<MachinePreset> machines;
    QVector<MaterialPreset> materials;

    bool isValid() const { return !machines.isEmpty(); }

    const MachinePreset* machineById(const QString& id) const
    {
        for (const MachinePreset& m : machines) {
            if (m.id == id) {
                return &m;
            }
        }
        return nullptr;
    }

    const MaterialPreset* materialById(const QString& id) const
    {
        for (const MaterialPreset& m : materials) {
            if (m.id == id) {
                return &m;
            }
        }
        return nullptr;
    }
};
