#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QTimer>

class AppState;

class PreviewGLWidget final : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit PreviewGLWidget(AppState* state, QWidget* parent = nullptr);
    ~PreviewGLWidget() override;

    void setFrame(const QImage& image);
    void setPlaybackPositionMs(qint64 positionMs);

signals:
    void browseRequested();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void initShaders();
    void initGeometry();
    void ensureTextureStorage(const QImage& image);
    void uploadPendingFrame();

    AppState* m_state = nullptr;
    QTimer m_refreshTimer;
    QElapsedTimer m_elapsed;

    QImage m_pendingFrame;
    QSize m_frameSize;
    qint64 m_playbackPositionMs = 0;
    bool m_hasPendingFrame = false;
    bool m_hasTexture = false;

    QMutex m_frameMutex;
    QOpenGLShaderProgram m_program;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
    GLuint m_textures[2] = {0, 0};
    int m_frontTexture = 0;
};
