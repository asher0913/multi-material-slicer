#include "OpenGLView.h"

#include <QMouseEvent>
#include <QtMath>

#include <algorithm>
#include <limits>

OpenGLView::OpenGLView(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
}

OpenGLView::~OpenGLView()
{
    if (context()) {
        makeCurrent();
    }
    qDeleteAll(m_meshBuffers);
    m_meshBuffers.clear();
    if (context()) {
        doneCurrent();
    }
}

void OpenGLView::setModels(const QVector<ModelInstance>* models)
{
    m_models = models;
    update();
}

void OpenGLView::setSelectedIndex(int index)
{
    m_selectedIndex = index;
    update();
}

void OpenGLView::setBuildPlateSize(double widthMm, double depthMm)
{
    if (m_plateWidthMm != widthMm || m_plateDepthMm != depthMm) {
        m_plateWidthMm = widthMm;
        m_plateDepthMm = depthMm;
        m_plateGeometryDirty = true;
    }
    update();
}

void OpenGLView::initializeGL()
{
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    m_program = new QOpenGLShaderProgram(this);
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,
        "attribute vec3 position;\n"
        "attribute vec3 normal;\n"
        "uniform mat4 mvp;\n"
        "uniform mat3 normalMatrix;\n"
        "varying vec3 vNormal;\n"
        "void main() {\n"
        "    gl_Position = mvp * vec4(position, 1.0);\n"
        "    vNormal = normalMatrix * normal;\n"
        "}\n");
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
        "uniform vec3 baseColor;\n"
        "varying vec3 vNormal;\n"
        "void main() {\n"
        "    vec3 n = normalize(vNormal);\n"
        "    vec3 keyDir = normalize(vec3(0.35, 0.45, 0.82));\n"
        "    vec3 fillDir = normalize(vec3(-0.5, -0.3, 0.4));\n"
        "    float key = max(dot(n, keyDir), 0.0);\n"
        "    float fill = max(dot(n, fillDir), 0.0) * 0.25;\n"
        "    float ambient = 0.22;\n"
        "    float lighting = ambient + key * 0.85 + fill;\n"
        "    vec3 color = baseColor * lighting;\n"
        "    // subtle rim light for a more premium look\n"
        "    float rim = pow(1.0 - max(n.z, 0.0), 3.0) * 0.25;\n"
        "    color += vec3(0.17, 0.63, 0.86) * rim;\n"
        "    gl_FragColor = vec4(color, 1.0);\n"
        "}\n");
    m_program->link();

    // Fullscreen vertical gradient drawn behind the scene.
    m_bgProgram = new QOpenGLShaderProgram(this);
    m_bgProgram->addShaderFromSourceCode(QOpenGLShader::Vertex,
        "attribute vec2 position;\n"
        "attribute vec3 color;\n"
        "varying vec3 vColor;\n"
        "void main() {\n"
        "    vColor = color;\n"
        "    gl_Position = vec4(position, 0.0, 1.0);\n"
        "}\n");
    m_bgProgram->addShaderFromSourceCode(QOpenGLShader::Fragment,
        "varying vec3 vColor;\n"
        "void main() {\n"
        "    gl_FragColor = vec4(vColor, 1.0);\n"
        "}\n");
    m_bgProgram->link();
}

void OpenGLView::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void OpenGLView::paintGL()
{
    glClearColor(0.075f, 0.082f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    drawBackground();

    if (!m_program || !m_program->isLinked()) {
        return;
    }

    const float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    m_projection.setToIdentity();
    m_projection.perspective(45.0f, aspect, 0.1f, 5000.0f);

    m_view.setToIdentity();
    m_view.translate(0.0f, 0.0f, -m_distance);
    m_view.rotate(m_pitch, 1.0f, 0.0f, 0.0f);
    m_view.rotate(m_yaw, 0.0f, 0.0f, 1.0f);
    m_view.translate(m_pan);

    drawBuildPlate();

    if (!m_models) {
        return;
    }

    QSet<const StlMesh*> liveMeshes;
    for (int i = 0; i < m_models->size(); ++i) {
        const ModelInstance& model = m_models->at(i);
        if (model.visible && model.mesh && !model.mesh->isEmpty()) {
            liveMeshes.insert(model.mesh.data());
        }
    }
    releaseUnusedBuffers(liveMeshes);

    for (int i = 0; i < m_models->size(); ++i) {
        const ModelInstance& model = m_models->at(i);
        if (!model.visible || !model.mesh || model.mesh->isEmpty()) {
            continue;
        }
        QColor color = model.color;
        if (i == m_selectedIndex) {
            color = color.lighter(145);
        }
        drawModelMesh(*model.mesh, model.effectiveMatrix(), color);
    }
}

void OpenGLView::drawBackground()
{
    if (!m_bgProgram || !m_bgProgram->isLinked()) {
        return;
    }

    glDisable(GL_DEPTH_TEST);

    // Two triangles covering NDC, darker at the top fading to a slightly warmer/
    // bluer charcoal at the bottom; a subtle studio-style gradient.
    const QVector3D top(0.066f, 0.074f, 0.090f);
    const QVector3D bottom(0.110f, 0.130f, 0.160f);
    const float positions[6][2] = {
        {-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f},
        {-1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f},
    };
    const QVector3D colors[6] = {bottom, bottom, top, bottom, top, top};

    m_bgProgram->bind();
    const int posLoc = m_bgProgram->attributeLocation("position");
    const int colLoc = m_bgProgram->attributeLocation("color");
    m_bgProgram->enableAttributeArray(posLoc);
    m_bgProgram->enableAttributeArray(colLoc);
    m_bgProgram->setAttributeArray(posLoc, GL_FLOAT, positions, 2, sizeof(float) * 2);
    m_bgProgram->setAttributeArray(colLoc, GL_FLOAT, colors, 3, sizeof(QVector3D));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    m_bgProgram->disableAttributeArray(posLoc);
    m_bgProgram->disableAttributeArray(colLoc);
    m_bgProgram->release();

    glEnable(GL_DEPTH_TEST);
}

void OpenGLView::drawBuildPlate()
{
    updateBuildPlateGeometry();

    drawMesh(m_plateVertices, m_plateNormals, QMatrix4x4(), QColor(38, 44, 54), GL_TRIANGLES);
    drawMesh(m_gridVertices, m_gridNormals, QMatrix4x4(), QColor(58, 66, 80), GL_LINES);
    drawMesh(m_borderVertices, m_borderNormals, QMatrix4x4(), QColor(90, 100, 116), GL_LINES);
    drawMesh(m_axisXVertices, m_axisNormals, QMatrix4x4(), QColor(220, 96, 110), GL_LINES);
    drawMesh(m_axisYVertices, m_axisNormals, QMatrix4x4(), QColor(110, 200, 130), GL_LINES);
    drawMesh(m_axisZVertices, m_axisNormals, QMatrix4x4(), QColor(44, 160, 220), GL_LINES);
}

void OpenGLView::updateBuildPlateGeometry()
{
    if (!m_plateGeometryDirty) {
        return;
    }
    m_plateGeometryDirty = false;

    const float w = static_cast<float>(m_plateWidthMm * 0.5);
    const float d = static_cast<float>(m_plateDepthMm * 0.5);
    const float z = -0.02f;

    // Plate surface.
    m_plateVertices = {
        QVector3D(-w, -d, z), QVector3D(w, -d, z), QVector3D(w, d, z),
        QVector3D(-w, -d, z), QVector3D(w, d, z), QVector3D(-w, d, z),
    };
    m_plateNormals = QVector<QVector3D>(m_plateVertices.size(), QVector3D(0.0f, 0.0f, 1.0f));

    // Grid lines every 10mm, drawn just above the surface.
    const float gz = z + 0.01f;
    const float step = 10.0f;
    m_gridVertices.clear();
    for (float x = 0.0f; x <= w + 0.001f; x += step) {
        m_gridVertices << QVector3D(x, -d, gz) << QVector3D(x, d, gz);
        if (x > 0.0f) {
            m_gridVertices << QVector3D(-x, -d, gz) << QVector3D(-x, d, gz);
        }
    }
    for (float y = 0.0f; y <= d + 0.001f; y += step) {
        m_gridVertices << QVector3D(-w, y, gz) << QVector3D(w, y, gz);
        if (y > 0.0f) {
            m_gridVertices << QVector3D(-w, -y, gz) << QVector3D(w, -y, gz);
        }
    }
    m_gridNormals = QVector<QVector3D>(m_gridVertices.size(), QVector3D(0.0f, 0.0f, 1.0f));

    // Plate border.
    m_borderVertices = {
        QVector3D(-w, -d, gz), QVector3D(w, -d, gz),
        QVector3D(w, -d, gz), QVector3D(w, d, gz),
        QVector3D(w, d, gz), QVector3D(-w, d, gz),
        QVector3D(-w, d, gz), QVector3D(-w, -d, gz),
    };
    m_borderNormals = QVector<QVector3D>(m_borderVertices.size(), QVector3D(0.0f, 0.0f, 1.0f));

    // Origin axes: X red-ish, Y green-ish, Z accent blue.
    m_axisXVertices = { QVector3D(0, 0, 0), QVector3D(step * 1.6f, 0, 0) };
    m_axisYVertices = { QVector3D(0, 0, 0), QVector3D(0, step * 1.6f, 0) };
    m_axisZVertices = { QVector3D(0, 0, 0), QVector3D(0, 0, step * 2.4f) };
    m_axisNormals = QVector<QVector3D>(2, QVector3D(0.0f, 0.0f, 1.0f));
}

void OpenGLView::drawMesh(const QVector<QVector3D>& vertices,
                          const QVector<QVector3D>& normals,
                          const QMatrix4x4& model,
                          const QColor& color,
                          GLenum primitive)
{
    if (vertices.isEmpty() || normals.size() != vertices.size()) {
        return;
    }

    m_program->bind();
    const QMatrix4x4 mvp = m_projection * m_view * model;
    const QMatrix3x3 normalMatrix = (m_view * model).normalMatrix();
    m_program->setUniformValue("mvp", mvp);
    m_program->setUniformValue("normalMatrix", normalMatrix);
    m_program->setUniformValue("baseColor",
        QVector3D(color.redF(), color.greenF(), color.blueF()));

    const int positionLocation = m_program->attributeLocation("position");
    const int normalLocation = m_program->attributeLocation("normal");
    m_program->enableAttributeArray(positionLocation);
    m_program->enableAttributeArray(normalLocation);
    m_program->setAttributeArray(positionLocation, GL_FLOAT, vertices.constData(), 3, sizeof(QVector3D));
    m_program->setAttributeArray(normalLocation, GL_FLOAT, normals.constData(), 3, sizeof(QVector3D));

    glDrawArrays(primitive, 0, vertices.size());

    m_program->disableAttributeArray(positionLocation);
    m_program->disableAttributeArray(normalLocation);
    m_program->release();
}

void OpenGLView::drawModelMesh(const StlMesh& mesh,
                               const QMatrix4x4& model,
                               const QColor& color)
{
    MeshBuffers* buffers = buffersForMesh(mesh);
    if (!buffers || buffers->vertexCount <= 0) {
        return;
    }

    m_program->bind();
    const QMatrix4x4 mvp = m_projection * m_view * model;
    const QMatrix3x3 normalMatrix = (m_view * model).normalMatrix();
    m_program->setUniformValue("mvp", mvp);
    m_program->setUniformValue("normalMatrix", normalMatrix);
    m_program->setUniformValue("baseColor",
        QVector3D(color.redF(), color.greenF(), color.blueF()));

    const int positionLocation = m_program->attributeLocation("position");
    const int normalLocation = m_program->attributeLocation("normal");
    m_program->enableAttributeArray(positionLocation);
    m_program->enableAttributeArray(normalLocation);

    buffers->vertexBuffer.bind();
    m_program->setAttributeBuffer(positionLocation, GL_FLOAT, 0, 3, sizeof(QVector3D));
    buffers->normalBuffer.bind();
    m_program->setAttributeBuffer(normalLocation, GL_FLOAT, 0, 3, sizeof(QVector3D));

    glDrawArrays(GL_TRIANGLES, 0, buffers->vertexCount);

    buffers->normalBuffer.release();
    buffers->vertexBuffer.release();
    m_program->disableAttributeArray(positionLocation);
    m_program->disableAttributeArray(normalLocation);
    m_program->release();
}

OpenGLView::MeshBuffers* OpenGLView::buffersForMesh(const StlMesh& mesh)
{
    const StlMesh* key = &mesh;
    if (m_meshBuffers.contains(key)) {
        return m_meshBuffers.value(key);
    }

    if (mesh.vertices().isEmpty() || mesh.normals().size() != mesh.vertices().size()) {
        return nullptr;
    }
    const qsizetype vertexBytes = static_cast<qsizetype>(mesh.vertices().size()) * static_cast<qsizetype>(sizeof(QVector3D));
    const qsizetype normalBytes = static_cast<qsizetype>(mesh.normals().size()) * static_cast<qsizetype>(sizeof(QVector3D));
    if (vertexBytes > std::numeric_limits<int>::max() || normalBytes > std::numeric_limits<int>::max()) {
        return nullptr;
    }

    auto* buffers = new MeshBuffers();
    buffers->vertexCount = mesh.vertices().size();
    buffers->vertexBuffer.create();
    buffers->vertexBuffer.bind();
    buffers->vertexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    buffers->vertexBuffer.allocate(mesh.vertices().constData(), static_cast<int>(vertexBytes));
    buffers->vertexBuffer.release();

    buffers->normalBuffer.create();
    buffers->normalBuffer.bind();
    buffers->normalBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    buffers->normalBuffer.allocate(mesh.normals().constData(), static_cast<int>(normalBytes));
    buffers->normalBuffer.release();

    m_meshBuffers.insert(key, buffers);
    return buffers;
}

void OpenGLView::releaseUnusedBuffers(const QSet<const StlMesh*>& liveMeshes)
{
    auto it = m_meshBuffers.begin();
    while (it != m_meshBuffers.end()) {
        if (liveMeshes.contains(it.key())) {
            ++it;
            continue;
        }
        delete it.value();
        it = m_meshBuffers.erase(it);
    }
}

void OpenGLView::mousePressEvent(QMouseEvent* event)
{
    m_lastMousePos = event->pos();
}

void OpenGLView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    if (event->buttons() & Qt::LeftButton) {
        m_yaw += static_cast<float>(delta.x()) * 0.45f;
        m_pitch += static_cast<float>(delta.y()) * 0.45f;
        m_pitch = std::max(-89.0f, std::min(89.0f, m_pitch));
        update();
    } else if (event->buttons() & Qt::RightButton) {
        const float scale = m_distance * 0.0015f;
        m_pan += QVector3D(static_cast<float>(delta.x()) * scale, -static_cast<float>(delta.y()) * scale, 0.0f);
        update();
    }
}

void OpenGLView::wheelEvent(QWheelEvent* event)
{
    const float steps = static_cast<float>(event->angleDelta().y()) / 120.0f;
    m_distance *= std::pow(0.88f, steps);
    m_distance = std::max(20.0f, std::min(2000.0f, m_distance));
    update();
}
