#include "ui/PreviewGLWidget.h"

#include "app/AppState.h"

#include <QDebug>
#include <algorithm>
#include <cstddef>
#include <QMouseEvent>
#include <QVector2D>

namespace {
struct Vertex {
    QVector2D pos;
    QVector2D uv;
};

QString formatTimeText(qint64 positionMs)
{
    const qint64 totalSec = std::max<qint64>(0, positionMs) / 1000;
    const qint64 hh = totalSec / 3600;
    const qint64 mm = (totalSec / 60) % 60;
    const qint64 ss = totalSec % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hh, 2, 10, QLatin1Char('0'))
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 2, 10, QLatin1Char('0'));
}
}

PreviewGLWidget::PreviewGLWidget(AppState* state, QWidget* parent)
    : QOpenGLWidget(parent)
    , m_state(state)
{
    setMinimumSize(640, 360);
    setAutoFillBackground(false);

    m_refreshTimer.setInterval(16);
    connect(&m_refreshTimer, &QTimer::timeout, this, QOverload<>::of(&PreviewGLWidget::update));
    m_refreshTimer.start();

    if (m_state) {
        connect(m_state, &AppState::stateChanged, this, [this]() {
            updateTimecodeOverlay();
            update();
        });
    }

    m_timecodeLabel = new QLabel(this);
    m_timecodeLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_timecodeLabel->hide();
}

PreviewGLWidget::~PreviewGLWidget()
{
    makeCurrent();
    if (m_textures[0] || m_textures[1]) {
        glDeleteTextures(2, m_textures);
        m_textures[0] = 0;
        m_textures[1] = 0;
    }
    doneCurrent();
}

void PreviewGLWidget::setFrame(const QImage& image)
{
    if (image.isNull()) {
        return;
    }
    QMutexLocker lock(&m_frameMutex);
    m_pendingFrame = image;
    m_hasPendingFrame = true;
    m_frameSize = image.size();
    update();
}

void PreviewGLWidget::setPlaybackPositionMs(qint64 positionMs)
{
    m_playbackPositionMs = qMax<qint64>(0, positionMs);
    updateTimecodeOverlay();
    update();
}

void PreviewGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glGenTextures(2, m_textures);
    for (GLuint texture : m_textures) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    initShaders();
    initGeometry();
}

void PreviewGLWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    updateTimecodeOverlay();
}

void PreviewGLWidget::paintGL()
{
    glClearColor(0.03f, 0.03f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    uploadPendingFrame();
    if (!m_hasTexture) {
        return;
    }

    const QSize frameSize = m_frameSize.isEmpty() ? QSize(16, 9) : m_frameSize;
    const float widgetAspect = width() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    const float frameAspect = static_cast<float>(frameSize.width()) / static_cast<float>(frameSize.height());
    QVector2D scale(1.0f, 1.0f);
    if (frameAspect > widgetAspect) {
        scale.setY(widgetAspect / frameAspect);
    } else {
        scale.setX(frameAspect / widgetAspect);
    }

    const AppState::EffectSettings fx = m_state ? m_state->effectSettings() : AppState::EffectSettings{};
    const float timeSec = static_cast<float>(m_playbackPositionMs) / 1000.0f;
    const float headGlitch = static_cast<float>(fx.headGlitch) / 100.0f;
    const float interlaceFlicker = (static_cast<float>(fx.interlace) / 100.0f) * (static_cast<float>(fx.flickerAmount) / 100.0f);
    const float pixelSort = static_cast<float>(fx.pixelSort) / 100.0f;
    const float glitchBlocks = static_cast<float>(fx.glitch) / 100.0f;
    const float trackingError = static_cast<float>(fx.tracking) / 100.0f;
    const float grainAmount = static_cast<float>(fx.grain) / 100.0f;
    const float grainSize = 1.0f + static_cast<float>(fx.grainSize) / 100.0f * 12.0f;
    const float chromaShift = static_cast<float>(fx.chromaShift) / 1000.0f;
    const float sineWarp = static_cast<float>(fx.sineWarp) / 100.0f;
    const float colorBleed = static_cast<float>(fx.colorBleed) / 120.0f;

    m_program.bind();
    m_program.setUniformValue("uTexture", 0);
    m_program.setUniformValue("uScale", scale);
    m_program.setUniformValue("uResolution", QVector2D(static_cast<float>(frameSize.width()), static_cast<float>(frameSize.height())));
    m_program.setUniformValue("uTime", timeSec);
    m_program.setUniformValue("uHeadGlitch", headGlitch);
    m_program.setUniformValue("uInterlaceFlicker", interlaceFlicker);
    m_program.setUniformValue("uPixelSort", pixelSort);
    m_program.setUniformValue("uGlitchBlocks", glitchBlocks);
    m_program.setUniformValue("uTrackingError", trackingError);
    m_program.setUniformValue("uGrainAmount", grainAmount);
    m_program.setUniformValue("uGrainSize", grainSize);
    m_program.setUniformValue("uChromaShift", chromaShift);
    m_program.setUniformValue("uSineWarp", sineWarp);
    m_program.setUniformValue("uColorBleed", colorBleed);

    qDebug().nospace()
        << "preview uniforms t=" << timeSec
        << " hg=" << headGlitch
        << " if=" << interlaceFlicker
        << " ps=" << pixelSort
        << " gb=" << glitchBlocks
        << " tr=" << trackingError
        << " gr=" << grainAmount
        << " gs=" << grainSize
        << " cs=" << chromaShift
        << " sw=" << sineWarp;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textures[m_frontTexture]);

    m_vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    m_vao.release();

    glBindTexture(GL_TEXTURE_2D, 0);
    m_program.release();
}

void PreviewGLWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit browseRequested();
    }
}

void PreviewGLWidget::initShaders()
{
    static constexpr const char* kVertexShader = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec2 aUv;
        out vec2 vUv;
        uniform vec2 uScale;
        void main() {
            vec2 pos = aPos * uScale;
            gl_Position = vec4(pos, 0.0, 1.0);
            vUv = aUv;
        }
    )";

    static constexpr const char* kFragmentShader = R"(
        #version 330 core
        in vec2 vUv;
        out vec4 fragColor;

        uniform sampler2D uTexture;
        uniform vec2 uResolution;
        uniform float uTime;
        uniform float uHeadGlitch;
        uniform float uInterlaceFlicker;
        uniform float uPixelSort;
        uniform float uGlitchBlocks;
        uniform float uTrackingError;
        uniform float uGrainAmount;
        uniform float uGrainSize;
        uniform float uChromaShift;
        uniform float uSineWarp;
        uniform float uColorBleed;

        float hash(vec2 p) {
            return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
        }

        vec3 sortChannels(vec3 c) {
            if (c.r > c.g) { float t = c.r; c.r = c.g; c.g = t; }
            if (c.g > c.b) { float t = c.g; c.g = c.b; c.b = t; }
            if (c.r > c.g) { float t = c.r; c.r = c.g; c.g = t; }
            return c;
        }

        void main() {
            vec2 uv = vUv;
            vec2 pixel = 1.0 / max(uResolution, vec2(1.0));

            float trackLine = step(0.82, hash(vec2(floor(uv.y * 220.0), floor(uTime * 30.0))));
            uv.x += (hash(vec2(floor(uv.y * 200.0), floor(uTime * 45.0))) - 0.5) * 0.12 * uTrackingError * trackLine;

            float blockRand = hash(floor(uv * vec2(24.0, 14.0) + floor(uTime * 20.0)));
            if (blockRand > 1.0 - uGlitchBlocks * 0.75) {
                uv.x = fract(uv.x + (hash(vec2(blockRand, uTime)) - 0.5) * 0.22 * uGlitchBlocks);
            }

            float headZone = step(uv.y, 0.22 + 0.18 * uHeadGlitch);
            float headCell = hash(floor(uv * vec2(38.0, 20.0) + vec2(uTime * 10.0, uTime * 6.0)));
            if (headZone > 0.5 && headCell > 0.75) {
                if (headCell > 0.9) {
                    fragColor = vec4(vec3(0.0), 1.0);
                    return;
                }
                uv.x = fract(uv.x + (headCell - 0.5) * 0.15 * uHeadGlitch);
            }

            float warp = sin((uv.y * 24.0) + (uTime * 4.0)) * (0.006 * uSineWarp);
            uv.x = fract(uv.x + warp);

            vec2 chroma = vec2(uChromaShift, 0.0);
            vec3 color;
            color.r = texture(uTexture, clamp(uv + chroma, vec2(0.0), vec2(1.0))).r;
            color.g = texture(uTexture, uv).g;
            color.b = texture(uTexture, clamp(uv - chroma, vec2(0.0), vec2(1.0))).b;

            if (uColorBleed > 0.0) {
                vec3 bleedL = texture(uTexture, clamp(uv - vec2(pixel.x * 2.0, 0.0), vec2(0.0), vec2(1.0))).rgb;
                vec3 bleedR = texture(uTexture, clamp(uv + vec2(pixel.x * 2.0, 0.0), vec2(0.0), vec2(1.0))).rgb;
                color = mix(color, (bleedL + color + bleedR) / 3.0, clamp(uColorBleed, 0.0, 1.0));
            }

            float psLine = step(0.72, hash(vec2(floor(uv.y * 170.0), floor(uTime * 14.0))));
            if (psLine > 0.5 && uPixelSort > 0.0) {
                color = mix(color, sortChannels(color), uPixelSort * 0.9);
            }

            float line = step(0.5, fract((gl_FragCoord.y + uTime * 20.0) * 0.5));
            float flickerWave = 0.94 + 0.06 * sin(uTime * 24.0 + gl_FragCoord.y * 0.12);
            float interlace = mix(1.0, line * flickerWave + (1.0 - line), clamp(uInterlaceFlicker, 0.0, 1.0));
            color *= interlace;

            float noise = hash(floor((uv + vec2(uTime)) * uResolution / max(uGrainSize, 1.0)));
            color += (noise - 0.5) * (0.25 * uGrainAmount);

            vec3 modifiedColor = clamp(color, 0.0, 1.0);
            fragColor = vec4(modifiedColor, 1.0);
        }
    )";

    m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    m_program.link();
}

void PreviewGLWidget::initGeometry()
{
    static constexpr Vertex kVertices[6] = {
        {{-1.0f, -1.0f}, {0.0f, 1.0f}},
        {{1.0f, -1.0f}, {1.0f, 1.0f}},
        {{1.0f, 1.0f}, {1.0f, 0.0f}},
        {{-1.0f, -1.0f}, {0.0f, 1.0f}},
        {{1.0f, 1.0f}, {1.0f, 0.0f}},
        {{-1.0f, 1.0f}, {0.0f, 0.0f}},
    };

    m_vao.create();
    m_vao.bind();

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(kVertices, static_cast<int>(sizeof(kVertices)));

    m_program.bind();
    m_program.enableAttributeArray(0);
    m_program.enableAttributeArray(1);
    m_program.setAttributeBuffer(0, GL_FLOAT, offsetof(Vertex, pos), 2, sizeof(Vertex));
    m_program.setAttributeBuffer(1, GL_FLOAT, offsetof(Vertex, uv), 2, sizeof(Vertex));
    m_program.release();

    m_vbo.release();
    m_vao.release();
}

void PreviewGLWidget::ensureTextureStorage(const QImage& image)
{
    if (m_hasTexture && image.size() == m_frameSize) {
        return;
    }

    m_frameSize = image.size();
    for (GLuint texture : m_textures) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_frameSize.width(), m_frameSize.height(), 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    m_hasTexture = true;
}

void PreviewGLWidget::uploadPendingFrame()
{
    QImage frame;
    {
        QMutexLocker lock(&m_frameMutex);
        if (!m_hasPendingFrame) {
            return;
        }
        frame = m_pendingFrame;
        m_hasPendingFrame = false;
    }

    if (frame.isNull()) {
        return;
    }

    if (frame.format() != QImage::Format_ARGB32 && frame.format() != QImage::Format_RGB32) {
        frame = frame.convertToFormat(QImage::Format_ARGB32);
    }

    ensureTextureStorage(frame);

    m_frontTexture = 1 - m_frontTexture;
    glBindTexture(GL_TEXTURE_2D, m_textures[m_frontTexture]);
    glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    0,
                    0,
                    frame.width(),
                    frame.height(),
                    GL_BGRA,
                    GL_UNSIGNED_BYTE,
                    frame.constBits());
    glBindTexture(GL_TEXTURE_2D, 0);
    m_hasTexture = true;
}

void PreviewGLWidget::updateTimecodeOverlay()
{
    if (!m_timecodeLabel || !m_state) {
        return;
    }

    if (!m_state->timecodeEnabled()) {
        m_timecodeLabel->hide();
        return;
    }

    const QString text = m_state->timecodeTemplate().replace(QStringLiteral("{time}"), formatTimeText(m_playbackPositionMs));
    QFont f(QStringLiteral("Consolas"));
    f.setPixelSize(m_state->timecodeSize());
    m_timecodeLabel->setFont(f);
    const QColor color = m_state->timecodeColor();
    m_timecodeLabel->setStyleSheet(QStringLiteral("color: rgba(%1,%2,%3,%4); background: transparent;")
                                       .arg(color.red())
                                       .arg(color.green())
                                       .arg(color.blue())
                                       .arg(color.alpha()));
    m_timecodeLabel->setText(text);
    m_timecodeLabel->adjustSize();

    const int x = (width() * m_state->timecodeX()) / 100;
    const int y = (height() * m_state->timecodeY()) / 100;
    m_timecodeLabel->move(x, y);
    m_timecodeLabel->show();
}
