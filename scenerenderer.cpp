﻿#include "scenerenderer.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QQuickWindow>
#include <QtMath>

#include "vertexfileloader.h"
#include "kdtree.h"
#include "utils.h"

SceneRenderer::SceneRenderer()
{
    VertexFileLoader::cubePointCloudVertices(50, 0.1f, m_vertices);
    generatePointIndices(m_vertices, m_indices);

    createSelectionWithKdTree();

    setupModelView();
}

void SceneRenderer::createSelectionWithKdTree()
{
    KdTreeNode* tree = buildKdTree(m_vertices.begin(), m_vertices.end(), 0);

    QVector3D min, max;
    pointCloudBounds(m_vertices, min, max);

    m_highlightedIndices.clear();
    generatePointIndices(m_highlightedVertices, m_highlightedIndices);

    QVector3D center(0.0008, 0.0666, 0.0699);
    QVector3D offset(0.01, 0.01, 0.01);

    m_vertices.push_back( center );
    m_targetPointIndices.clear();
    m_targetPointIndices.push_back(m_vertices.length() - 1);

    rangeQuery(center - offset, center + offset, m_highlightedIndices, tree, 0);

    // index of pointers in array is pointeradress - pointeradress of first pointer since memory is linear
    for(int& index : m_highlightedIndices)
    {
        index = (int) ((Vertex*) index -  m_vertices.data());
    }
    //qDebug() << "size of highlighted:" << m_highlightedIndices.size();

    int idx = 0;
    for(auto& vertex : m_vertices)
    {
        vertex.color = colorFromGradientHSV( (double) idx / m_vertices.length() );
        ++idx;
    }

    delete tree;
}

void SceneRenderer::setGeometryFilePath(const QString& geometryFilePath)
{
    m_geometryFilePath = geometryFilePath;

    char* stringByteData = m_geometryFilePath.toLatin1().data();
    VertexFileLoader::loadVerticesFromFile(stringByteData, m_vertices);

    generatePointIndices(m_vertices, m_indices);

    createSelectionWithKdTree();

    // reset rotation
    m_rotation = QMatrix4x4();
    setupModelView();

    // geometry changed, hence make paint function recreate VAO
    m_isGeometryInvalidated = true;

    // schedule repaint for next frame when loading is done
    m_window->update();
}

void SceneRenderer::generatePointIndices(const QVector<Vertex>& vertices,
                                         QVector<int>& indices)
{
    indices.clear();
    int index = 0;
    for(auto vertex : vertices) indices.push_back( index++ );
}

void SceneRenderer::setupModelView()
{
    m_modelview = m_rotation;

    QVector3D cog = centerOfGravity(m_vertices);
    m_modelview.translate(-cog);

    QMatrix4x4 view;

    QVector3D eye(0, 0, 0.3f);
    QVector3D center(0, 0, 0);
    QVector3D up(0, 1.f, 0);

    view.lookAt(eye, center, up);

    m_modelview = view * m_modelview;
}

void SceneRenderer::setupProjection()
{
    m_projection = QMatrix4x4();
    float aspect = m_viewportSize.width() / (float) m_viewportSize.height();
    m_projection.perspective(50, aspect, 0.1f, 10.0f);
}

void SceneRenderer::paint()
{
    if( m_isGeometryInvalidated )
    {
        initVertexData();
        m_isGeometryInvalidated = false;
    }

    //qDebug() << "SceneRenderer: repaint scene";
    glViewport(0, 0, m_viewportSize.width(), m_viewportSize.height());
    glClearColor(0, 0.18f, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_program->bind();

    m_program->setUniformValue("modelview", m_modelview);
    m_program->setUniformValue("projection", m_projection);

    glPointSize(1);
    // this color acts as "switch" to enable vertex coloring
    m_program->setUniformValue("color", QVector4D(0, 0, 0, 0));
    m_defaultVAO.draw(GL_POINTS);

    glPointSize(4);
    m_program->setUniformValue("color", QVector4D(1, 1, 1, 1));
    m_highlightedVAO.draw(GL_POINTS);

    glPointSize(10);
    m_program->setUniformValue("color", QVector4D(1, 0, 0, 1));
    m_targetPointVAO.draw(GL_POINTS);

    // flush and swap buffers
    glFlush();

    // restore OpenGL state in order to not mess up QML rendering
    // (which is also done with OpenGL)
    m_window->resetOpenGLState();
}

void SceneRenderer::init()
{
    if( !m_program )
    {
        qDebug() << "SceneRenderer::init(): setup shader and VAO";

        // needed before Qt OpenGL wrapper functions can be called
        // essentially makes use of GLUT headers obsolete
        initializeOpenGLFunctions();
        initShader();

        m_isGeometryInvalidated = true;
    }
}

void SceneRenderer::initShader()
{
    m_program = new QOpenGLShaderProgram();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                       "#version 430\n"

                                       "layout(location=0) in highp vec3 position;"
                                       "layout(location=1) in highp vec3 color;"

                                       "uniform highp mat4 modelview;"
                                       "uniform highp mat4 projection;"

                                       "out highp vec3 vertexColor;"

                                       "void main()"
                                       "{"
                                       "    gl_Position = projection* modelview * vec4(position, 1.0);"
                                       "    vertexColor = color;"
                                       "}");
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                       "#version 430\n"

                                       "uniform highp vec4 color;"
                                       "in highp vec3 vertexColor;"

                                       "void main()"
                                       "{"
                                       "    gl_FragColor = (color != vec4(0.0)) ? color : vec4(vertexColor, 1.0);"
                                       "}");

    m_program->bindAttributeLocation("position", 0);
    m_program->bindAttributeLocation("color", 1);
    if( !m_program->link() ) qWarning() << "linking of shader failed!";
}

void SceneRenderer::initVertexData()
{
    m_defaultVAO.init(m_vertices, m_indices);

    m_highlightedVAO.init(m_vertices, m_highlightedIndices);
    m_targetPointVAO.init(m_vertices, m_targetPointIndices);
}

void SceneRenderer::rotate(float lastposX, float lastposY, float currposX, float currposY)
{
    //The center of our virtual rotation ball is in the center of the screen
    const float x0 = m_viewportSize.width() / 2.0f;
    const float y0 = m_viewportSize.height() / 2.0f;
    //We set the radius of rotation ball to half of the screen height
    const float r = m_viewportSize.height() / 2.0f; //ball radius is half the window height
    //const float r  = sqrt(sqr(m_winWidth) + sqr(m_winHeight)) / 2; //ball radius is half the window diagonal;
    float lastPosZ = (sqr(r) - sqr(lastposX - x0) - sqr(lastposY - y0));
    float currPosZ = (sqr(r) - sqr(currposX - x0) - sqr(currposY - y0));

    //if z is negative then we are outside the virtual ball and the rotation is just around the current view direction/z-axis
    lastPosZ = lastPosZ < 0 ? 0 : sqrt(lastPosZ);
    currPosZ = currPosZ < 0 ? 0 : sqrt(currPosZ);

    //take into account that the screen origin is in the top left corner (and not bottom left) -> x'=x-x0 and y'=y0-y
    QVector3D lastPos3d(lastposX - x0, y0 - lastposY, lastPosZ);
    QVector3D currPos3d(currposX - x0, y0 - currposY, currPosZ);
    lastPos3d.normalize(); //make unit normal vector
    currPos3d.normalize(); //make unit normal vector

    //the current mouse interaction results in this 3d rotation in camera space (unit sphere)
    QVector3D axisCS = QVector3D::crossProduct(lastPos3d, currPos3d);
    float angle = acos( QVector3D::dotProduct(lastPos3d, currPos3d) );
    QVector3D axisWS = m_rotation.transposed() * axisCS;

    m_rotation.rotate( qRadiansToDegrees(angle), axisWS);

    setupModelView();
    // schedule repaint for next frame
    m_window->update();
}
