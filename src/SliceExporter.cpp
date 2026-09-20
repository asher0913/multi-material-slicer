#include "SliceExporter.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMatrix4x4>
#include <QPointF>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace {

struct Triangle {
    QVector3D a;
    QVector3D b;
    QVector3D c;
};

struct Segment {
    QPointF a;
    QPointF b;
};

bool isFiniteVector(const QVector3D& v)
{
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
}

bool isNumericPng(const QFileInfo& info)
{
    if (info.suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0) {
        return false;
    }
    bool ok = false;
    info.completeBaseName().toInt(&ok);
    return ok;
}

bool cleanMaterialDir(const QString& path, QString* errorMessage)
{
    QDir dir(path);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create folder: %1").arg(path);
        }
        return false;
    }

    const QFileInfoList files = dir.entryInfoList(QDir::Files);
    for (const QFileInfo& file : files) {
        if (isNumericPng(file) && !QFile::remove(file.absoluteFilePath())) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Cannot remove old slice: %1").arg(file.absoluteFilePath());
            }
            return false;
        }
    }
    return true;
}

void addUniquePoint(QVector<QVector3D>& points, const QVector3D& p)
{
    constexpr float eps = 1.0e-5f;
    for (const QVector3D& existing : points) {
        if ((existing - p).lengthSquared() < eps * eps) {
            return;
        }
    }
    points.append(p);
}

bool trianglePlaneSegment(const Triangle& tri, float z, QVector3D* outA, QVector3D* outB)
{
    constexpr float eps = 1.0e-5f;
    const QVector3D vertices[3] = {tri.a, tri.b, tri.c};
    QVector<QVector3D> points;

    for (int i = 0; i < 3; ++i) {
        const QVector3D& a = vertices[i];
        const QVector3D& b = vertices[(i + 1) % 3];
        const float da = a.z() - z;
        const float db = b.z() - z;

        if (std::abs(da) <= eps && std::abs(db) <= eps) {
            continue;
        }
        if (std::abs(da) <= eps) {
            addUniquePoint(points, a);
        }
        if ((da < -eps && db > eps) || (da > eps && db < -eps)) {
            const float t = da / (da - db);
            addUniquePoint(points, a + (b - a) * t);
        }
        if (std::abs(db) <= eps) {
            addUniquePoint(points, b);
        }
    }

    if (points.size() < 2) {
        return false;
    }

    int bestA = 0;
    int bestB = 1;
    float bestDistance = 0.0f;
    for (int i = 0; i < points.size(); ++i) {
        for (int j = i + 1; j < points.size(); ++j) {
            const float d = (points[i] - points[j]).lengthSquared();
            if (d > bestDistance) {
                bestDistance = d;
                bestA = i;
                bestB = j;
            }
        }
    }

    if (bestDistance <= eps * eps) {
        return false;
    }
    *outA = points[bestA];
    *outB = points[bestB];
    return true;
}

QPointF toPixel(const QVector3D& p, const SliceSettings& settings)
{
    return QPointF(
        p.x() / settings.pixelSizeMm + settings.outputWidth * 0.5,
        settings.outputHeight * 0.5 - p.y() / settings.pixelSizeMm);
}

void rasterizeSegments(QImage* image, const QVector<Segment>& segments)
{
    QVector<QVector<double>> intersections(image->height());

    for (const Segment& segment : segments) {
        double x0 = segment.a.x();
        double y0 = segment.a.y();
        double x1 = segment.b.x();
        double y1 = segment.b.y();
        if (std::abs(y1 - y0) < 1.0e-9) {
            continue;
        }
        if (y0 > y1) {
            std::swap(x0, x1);
            std::swap(y0, y1);
        }

        const int startY = std::max(0, static_cast<int>(std::floor(y0)));
        const int endY = std::min(image->height() - 1, static_cast<int>(std::ceil(y1)));
        for (int y = startY; y <= endY; ++y) {
            const double scanY = static_cast<double>(y) + 0.5;
            if (scanY < y0 || scanY >= y1) {
                continue;
            }
            const double t = (scanY - y0) / (y1 - y0);
            const double x = x0 + (x1 - x0) * t;
            if (x >= -1.0 && x <= image->width() + 1.0) {
                intersections[y].append(x);
            }
        }
    }

    for (int y = 0; y < image->height(); ++y) {
        QVector<double>& xs = intersections[y];
        if (xs.size() < 2) {
            continue;
        }
        std::sort(xs.begin(), xs.end());
        uchar* line = image->scanLine(y);
        for (int i = 0; i + 1 < xs.size(); i += 2) {
            int x0 = std::max(0, static_cast<int>(std::ceil(xs[i])));
            int x1 = std::min(image->width() - 1, static_cast<int>(std::floor(xs[i + 1])));
            if (x1 >= x0) {
                std::memset(line + x0, 255, static_cast<size_t>(x1 - x0 + 1));
            }
        }
    }
}

} // namespace

bool SliceExporter::exportSlices(const QVector<ModelInstance>& models,
                                 const SliceSettings& settings,
                                 const QString& outputRoot,
                                 QStringList* materialDirs,
                                 SliceExportStats* stats,
                                 QString* errorMessage,
                                 const SliceProgressCallback& progressCallback)
{
    if (models.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No STL models loaded");
        }
        return false;
    }
    if (settings.materialCount <= 0 || settings.outputWidth <= 0 || settings.outputHeight <= 0
        || settings.layerHeightMm <= 0.0 || settings.pixelSizeMm <= 0.0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid slice settings");
        }
        return false;
    }

    QDir root(outputRoot);
    if (!root.exists() && !root.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create output folder: %1").arg(outputRoot);
        }
        return false;
    }

    QStringList dirs;
    dirs.reserve(settings.materialCount);
    for (int i = 0; i < settings.materialCount; ++i) {
        const QString dirPath = root.absoluteFilePath(QStringLiteral("material_%1").arg(i));
        if (!cleanMaterialDir(dirPath, errorMessage)) {
            return false;
        }
        dirs.append(QDir::toNativeSeparators(dirPath));
    }

    QVector<QVector<Triangle>> trianglesByMaterial(settings.materialCount);
    double maxZ = 0.0;

    for (const ModelInstance& model : models) {
        if (!model.visible || !model.mesh || model.mesh->isEmpty()) {
            continue;
        }
        const int material = std::max(0, std::min(settings.materialCount - 1, model.materialIndex));
        const QMatrix4x4 transform = model.effectiveMatrix();
        const QVector<QVector3D>& vertices = model.mesh->vertices();
        for (int i = 0; i + 2 < vertices.size(); i += 3) {
            Triangle tri {
                transform.map(vertices[i]),
                transform.map(vertices[i + 1]),
                transform.map(vertices[i + 2])
            };
            if (!isFiniteVector(tri.a) || !isFiniteVector(tri.b) || !isFiniteVector(tri.c)) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Model contains non-finite transformed coordinates");
                }
                return false;
            }
            maxZ = std::max(maxZ, static_cast<double>(tri.a.z()));
            maxZ = std::max(maxZ, static_cast<double>(tri.b.z()));
            maxZ = std::max(maxZ, static_cast<double>(tri.c.z()));
            trianglesByMaterial[material].append(tri);
        }
    }

    if (!std::isfinite(maxZ) || !std::isfinite(settings.layerHeightMm)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid model height or layer height");
        }
        return false;
    }
    const double rawLayerCount = std::ceil(maxZ / settings.layerHeightMm);
    if (!std::isfinite(rawLayerCount) || rawLayerCount > std::numeric_limits<int>::max() - 1) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Layer count is too large");
        }
        return false;
    }
    const int layerCount = static_cast<int>(rawLayerCount);
    if (layerCount <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Loaded models do not extend above Z=0");
        }
        return false;
    }

    // Bucket every triangle into the layers its Z-range spans so each layer only
    // tests the triangles that can actually intersect it (instead of all of them).
    // layerOf(z) maps a world Z to the 1-based layer index whose plane sits at
    // layer * layerHeight.
    const double layerHeight = settings.layerHeightMm;
    QVector<QVector<QVector<int>>> bucketsByMaterial(settings.materialCount);
    for (int material = 0; material < settings.materialCount; ++material) {
        QVector<QVector<int>> buckets(layerCount + 1);
        const QVector<Triangle>& tris = trianglesByMaterial[material];
        for (int t = 0; t < tris.size(); ++t) {
            const Triangle& tri = tris[t];
            const double zMin = std::min({tri.a.z(), tri.b.z(), tri.c.z()});
            const double zMax = std::max({tri.a.z(), tri.b.z(), tri.c.z()});
            if (!std::isfinite(zMin) || !std::isfinite(zMax)) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Model contains non-finite Z coordinates");
                }
                return false;
            }
            int firstLayer = static_cast<int>(std::ceil(zMin / layerHeight));
            int lastLayer = static_cast<int>(std::floor(zMax / layerHeight));
            firstLayer = std::max(1, firstLayer);
            lastLayer = std::min(layerCount, lastLayer);
            for (int layer = firstLayer; layer <= lastLayer; ++layer) {
                buckets[layer].append(t);
            }
        }
        bucketsByMaterial[material] = std::move(buckets);
    }

    QVector<int> imageCounts(settings.materialCount, 0);
    QVector<QImage> reusableImages;
    reusableImages.reserve(settings.materialCount);
    for (int material = 0; material < settings.materialCount; ++material) {
        QImage image(settings.outputWidth, settings.outputHeight, QImage::Format_Grayscale8);
        if (image.isNull()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Cannot allocate slice image buffer");
            }
            return false;
        }
        reusableImages.append(std::move(image));
    }

    for (int layer = 1; layer <= layerCount; ++layer) {
        if (progressCallback && !progressCallback(layer - 1, layerCount)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Slice export canceled");
            }
            return false;
        }
        const float z = static_cast<float>(layer * layerHeight);
        for (int material = 0; material < settings.materialCount; ++material) {
            const QVector<Triangle>& tris = trianglesByMaterial[material];
            const QVector<int>& bucket = bucketsByMaterial[material][layer];
            QVector<Segment> segments;
            segments.reserve(bucket.size());
            for (int index : bucket) {
                QVector3D a;
                QVector3D b;
                if (trianglePlaneSegment(tris[index], z, &a, &b)) {
                    segments.append({toPixel(a, settings), toPixel(b, settings)});
                }
            }

            QImage& image = reusableImages[material];
            image.fill(0);
            rasterizeSegments(&image, segments);

            const QString path = QDir(dirs[material]).absoluteFilePath(QStringLiteral("%1.png").arg(layer));
            if (!image.save(path)) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Cannot write slice image: %1").arg(path);
                }
                return false;
            }
            imageCounts[material] += 1;
        }
        if (progressCallback && !progressCallback(layer, layerCount)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Slice export canceled");
            }
            return false;
        }
    }

    if (materialDirs) {
        *materialDirs = dirs;
    }
    if (stats) {
        stats->layerCount = layerCount;
        stats->materialImageCounts = imageCounts;
    }
    return true;
}
