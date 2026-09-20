#pragma once

#include "ModelTypes.h"

#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QHash>
#include <QPoint>
#include <QSet>
#include <QVector>

class StlMesh;

class OpenGLView : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit OpenGLView(QWidget* parent = nullptr);
    ~OpenGLView() override;

    void setModels(const QVector<ModelInstance>* models);
    void setSelectedIndex(int index);
    void setBuildPlateSize(double widthMm, double depthMm);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void drawMesh(const QVector<QVector3D>& vertices,
                  const QVector<QVector3D>& normals,
                  const QMatrix4x4& model,
                  const QColor& color,
                  GLenum primitive = GL_TRIANGLES);
    void drawModelMesh(const StlMesh& mesh,
                       const QMatrix4x4& model,
                       const QColor& color);
    void drawBackground();
    void drawBuildPlate();
    void updateBuildPlateGeometry();
    void releaseUnusedBuffers(const QSet<const StlMesh*>& liveMeshes);

    struct MeshBuffers {
        MeshBuffers()
            : vertexBuffer(QOpenGLBuffer::VertexBuffer)
            , normalBuffer(QOpenGLBuffer::VertexBuffer)
        {
        }

        QOpenGLBuffer vertexBuffer;
        QOpenGLBuffer normalBuffer;
        int vertexCount = 0;
    };

    MeshBuffers* buffersForMesh(const StlMesh& mesh);

    const QVector<ModelInstance>* m_models = nullptr;
    int m_selectedIndex = -1;
    QOpenGLShaderProgram* m_program = nullptr;
    QOpenGLShaderProgram* m_bgProgram = nullptr;
    QPoint m_lastMousePos;
    float m_yaw = -35.0f;
    float m_pitch = 58.0f;
    float m_distance = 220.0f;
    QVector3D m_pan = QVector3D(0.0f, 0.0f, 0.0f);
    double m_plateWidthMm = 96.0;
    double m_plateDepthMm = 54.0;
    QMatrix4x4 m_projection;
    QMatrix4x4 m_view;
    QHash<const StlMesh*, MeshBuffers*> m_meshBuffers;

    bool m_plateGeometryDirty = true;
    QVector<QVector3D> m_plateVertices;
    QVector<QVector3D> m_plateNormals;
    QVector<QVector3D> m_gridVertices;
    QVector<QVector3D> m_gridNormals;
    QVector<QVector3D> m_borderVertices;
    QVector<QVector3D> m_borderNormals;
    QVector<QVector3D> m_axisXVertices;
    QVector<QVector3D> m_axisYVertices;
    QVector<QVector3D> m_axisZVertices;
    QVector<QVector3D> m_axisNormals;
};
