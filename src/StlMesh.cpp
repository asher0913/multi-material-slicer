#include "StlMesh.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtEndian>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

bool isFiniteVector(const QVector3D& v)
{
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
}

float readFloatLE(const char* p)
{
    quint32 raw = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(p));
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(raw), "float must be 32-bit");
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

quint32 readUInt32LE(const char* p)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(p));
}

bool startsWithAsciiSolid(const QByteArray& bytes)
{
    int i = 0;
    while (i < bytes.size() && std::isspace(static_cast<unsigned char>(bytes.at(i)))) {
        ++i;
    }
    if (i + 5 > bytes.size()) {
        return false;
    }
    const QByteArray head = bytes.mid(i, 5).toLower();
    if (head != QByteArrayLiteral("solid")) {
        return false;
    }
    return i + 5 == bytes.size()
        || std::isspace(static_cast<unsigned char>(bytes.at(i + 5)));
}

bool isPlausibleBinaryStl(const QByteArray& bytes)
{
    if (bytes.size() < 84) {
        return false;
    }
    const quint32 triangleCount = readUInt32LE(bytes.constData() + 80);
    const quint64 expectedSize = 84ull + static_cast<quint64>(triangleCount) * 50ull;
    return triangleCount > 0
        && expectedSize <= static_cast<quint64>(bytes.size())
        && triangleCount <= static_cast<quint32>(std::numeric_limits<int>::max() / 3);
}

QVector3D safeNormal(const QVector3D& a, const QVector3D& b, const QVector3D& c, const QVector3D& supplied)
{
    if (isFiniteVector(supplied) && supplied.lengthSquared() > 1.0e-10f) {
        return supplied.normalized();
    }
    QVector3D n = QVector3D::crossProduct(b - a, c - a);
    if (!isFiniteVector(n) || n.lengthSquared() <= 1.0e-10f) {
        return QVector3D(0.0f, 0.0f, 1.0f);
    }
    return n.normalized();
}

} // namespace

bool StlMesh::load(const QString& filePath, QString* errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    constexpr qint64 maxStlBytes = 512ll * 1024ll * 1024ll;
    const qint64 fileSize = file.size();
    if (fileSize > maxStlBytes) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("STL file is too large for the preview loader (%1 MB > %2 MB)")
                .arg(fileSize / (1024 * 1024))
                .arg(maxStlBytes / (1024 * 1024));
        }
        return false;
    }

    const QByteArray bytes = file.readAll();
    if (bytes.size() != fileSize) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not read the whole STL file");
        }
        return false;
    }
    m_vertices.clear();
    m_normals.clear();

    // Prefer the binary parser for files whose 80-byte STL header happens to
    // contain "facet" or "vertex". ASCII STL usually starts with "solid"; if it
    // does, try ASCII first and fall back to binary for binary-STL exporters that
    // also use a "solid ..." header.
    const bool binaryCandidate = isPlausibleBinaryStl(bytes);
    const bool asciiSolid = startsWithAsciiSolid(bytes);
    const bool asciiKeywords = bytes.contains(QByteArrayLiteral("facet"))
        && bytes.contains(QByteArrayLiteral("vertex"));

    bool loaded = false;
    if (binaryCandidate && !asciiSolid) {
        loaded = loadBinary(bytes, errorMessage);
    }

    if (!loaded && (asciiSolid || (!binaryCandidate && asciiKeywords))) {
        loaded = loadAscii(bytes, errorMessage);
    }
    if (!loaded && binaryCandidate) {
        loaded = loadBinary(bytes, errorMessage);
    }
    if (!loaded) {
        loaded = loadAscii(bytes, errorMessage);
    }

    if (loaded && m_vertices.isEmpty()) {
        loaded = false;
        if (errorMessage) {
            *errorMessage = QStringLiteral("STL contains no triangles");
        }
    }

    if (loaded) {
        updateBounds();
    }
    return loaded;
}

bool StlMesh::loadBinary(const QByteArray& bytes, QString* errorMessage)
{
    const quint32 triangleCount = readUInt32LE(bytes.constData() + 80);
    const quint64 expectedSize = 84ull + static_cast<quint64>(triangleCount) * 50ull;
    // Trailing padding is tolerated (expectedSize <= file size); a file that is too
    // short for the declared triangle count is rejected.
    if (expectedSize > static_cast<quint64>(bytes.size())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Binary STL size does not match triangle count");
        }
        return false;
    }
    if (triangleCount > static_cast<quint32>(std::numeric_limits<int>::max() / 3)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Binary STL triangle count is too large");
        }
        return false;
    }

    const int vertexReserve = static_cast<int>(triangleCount * 3u);
    m_vertices.reserve(vertexReserve);
    m_normals.reserve(vertexReserve);

    const char* p = bytes.constData() + 84;
    for (quint32 i = 0; i < triangleCount; ++i, p += 50) {
        QVector3D normal(readFloatLE(p), readFloatLE(p + 4), readFloatLE(p + 8));
        QVector3D a(readFloatLE(p + 12), readFloatLE(p + 16), readFloatLE(p + 20));
        QVector3D b(readFloatLE(p + 24), readFloatLE(p + 28), readFloatLE(p + 32));
        QVector3D c(readFloatLE(p + 36), readFloatLE(p + 40), readFloatLE(p + 44));
        if (!appendTriangle(normal, a, b, c)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("STL contains non-finite vertex coordinates");
            }
            return false;
        }
    }
    return true;
}

bool StlMesh::loadAscii(const QByteArray& bytes, QString* errorMessage)
{
    const QString text = QString::fromUtf8(bytes);
    const QRegularExpression vertexRe(
        QStringLiteral(R"(vertex\s+([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s+([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s+([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?))"));
    QRegularExpressionMatchIterator it = vertexRe.globalMatch(text);

    QVector<QVector3D> temp;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        temp.append(QVector3D(
            match.captured(1).toFloat(),
            match.captured(2).toFloat(),
            match.captured(3).toFloat()));
    }

    if (temp.size() < 3 || temp.size() % 3 != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("ASCII STL vertex list is incomplete");
        }
        return false;
    }

    m_vertices.reserve(temp.size());
    m_normals.reserve(temp.size());
    for (int i = 0; i < temp.size(); i += 3) {
        if (!appendTriangle(QVector3D(), temp[i], temp[i + 1], temp[i + 2])) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("STL contains non-finite vertex coordinates");
            }
            return false;
        }
    }
    return true;
}

bool StlMesh::appendTriangle(const QVector3D& normal, const QVector3D& a, const QVector3D& b, const QVector3D& c)
{
    if (!isFiniteVector(a) || !isFiniteVector(b) || !isFiniteVector(c)) {
        return false;
    }
    const QVector3D n = safeNormal(a, b, c, normal);
    m_vertices.append(a);
    m_vertices.append(b);
    m_vertices.append(c);
    m_normals.append(n);
    m_normals.append(n);
    m_normals.append(n);
    return true;
}

void StlMesh::updateBounds()
{
    if (m_vertices.isEmpty()) {
        m_minBounds = QVector3D();
        m_maxBounds = QVector3D();
        return;
    }

    QVector3D mn(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    QVector3D mx(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
    for (const QVector3D& v : m_vertices) {
        mn.setX(std::min(mn.x(), v.x()));
        mn.setY(std::min(mn.y(), v.y()));
        mn.setZ(std::min(mn.z(), v.z()));
        mx.setX(std::max(mx.x(), v.x()));
        mx.setY(std::max(mx.y(), v.y()));
        mx.setZ(std::max(mx.z(), v.z()));
    }
    m_minBounds = mn;
    m_maxBounds = mx;
}

void StlMesh::normalizeToBuildPlateOrigin()
{
    updateBounds();
    const QVector3D offset(
        (m_minBounds.x() + m_maxBounds.x()) * 0.5f,
        (m_minBounds.y() + m_maxBounds.y()) * 0.5f,
        m_minBounds.z());
    for (QVector3D& v : m_vertices) {
        v -= offset;
    }
    updateBounds();
}

void StlMesh::translate(const QVector3D& offset)
{
    for (QVector3D& v : m_vertices) {
        v += offset;
    }
    updateBounds();
}
