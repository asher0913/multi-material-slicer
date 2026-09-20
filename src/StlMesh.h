#pragma once

#include <QString>
#include <QVector>
#include <QVector3D>

class StlMesh {
public:
    bool load(const QString& filePath, QString* errorMessage = nullptr);
    bool isEmpty() const { return m_vertices.isEmpty(); }
    void normalizeToBuildPlateOrigin();
    void translate(const QVector3D& offset);

    const QVector<QVector3D>& vertices() const { return m_vertices; }
    const QVector<QVector3D>& normals() const { return m_normals; }
    QVector3D minBounds() const { return m_minBounds; }
    QVector3D maxBounds() const { return m_maxBounds; }

private:
    bool loadBinary(const QByteArray& bytes, QString* errorMessage);
    bool loadAscii(const QByteArray& bytes, QString* errorMessage);
    void updateBounds();
    bool appendTriangle(const QVector3D& normal, const QVector3D& a, const QVector3D& b, const QVector3D& c);

    QVector<QVector3D> m_vertices;
    QVector<QVector3D> m_normals;
    QVector3D m_minBounds = QVector3D(0.0f, 0.0f, 0.0f);
    QVector3D m_maxBounds = QVector3D(0.0f, 0.0f, 0.0f);
};
