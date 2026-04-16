#include "media/MediaEngine.h"

#include "app/AppState.h"

#include <QFileInfo>
#include <QElapsedTimer>
#include <QLinearGradient>
#include <QPainter>
#include <QUrl>
#include <algorithm>

#ifdef AKERA_HAS_GSTREAMER
#include <gst/video/video.h>
#endif

namespace {
QString gstMessageToQString(const char* msg)
{
    return msg ? QString::fromUtf8(msg) : QStringLiteral("Unknown error");
}
}

MediaEngine::MediaEngine(AppState* state, QObject* parent)
    : QObject(parent)
    , m_state(state)
{
#ifdef AKERA_HAS_GSTREAMER
    static bool gstReady = false;
    if (!gstReady) {
        gst_init(nullptr, nullptr);
        gstReady = true;
    }
    m_backendSummary = QStringLiteral("Backend: Qt shell + GStreamer appsink playback");
#else
    m_backendSummary = QStringLiteral("Backend: Qt shell + stub media engine");
#endif

    connect(&m_pollTimer, &QTimer::timeout, this, &MediaEngine::pollBus);
    m_pollTimer.setInterval(8); // 8ms polling (~125Hz) so Ultra (60fps) has headroom
    connect(&m_renderTimer, &QTimer::timeout, this, &MediaEngine::renderTick);
    m_renderTimer.setInterval(16); // ~60Hz render pacing independent from decode cadence
    m_wallClock.start();
    m_perfWindowStartMs = 0;
    if (m_state) {
        connect(m_state, &AppState::stateChanged, this, &MediaEngine::onAppStateChanged);
    }
}

MediaEngine::~MediaEngine()
{
#ifdef AKERA_HAS_GSTREAMER
    teardownPipeline();
#endif
}

bool MediaEngine::loadFile(const QString& path)
{
    if (path.isEmpty()) {
        emit statusChanged(QStringLiteral("No file selected"));
        return false;
    }

    m_currentFrame = makePlaceholderFrame(path);
    emit frameReady(m_currentFrame);
    m_latestRawFrame = m_currentFrame;
    m_latestRawPtsMs = 0;

#ifdef AKERA_HAS_GSTREAMER
    if (!setupPipeline()) {
        emit statusChanged(QStringLiteral("Could not create GStreamer pipeline"));
        return false;
    }

    const QByteArray uri = QUrl::fromLocalFile(path).toString().toUtf8();
    g_object_set(G_OBJECT(m_gst->playbin), "uri", uri.constData(), nullptr);

    m_hasPlayableMedia = true;
    m_isPlaying = false;
    m_positionMs = 0;
    m_durationMs = 0;
    m_frameDropCount = 0;
    m_renderSkipCount = 0;
    m_latestRawFrame = QImage{};
    m_latestRawPtsMs = 0;
    {
        QMutexLocker lock(&m_pendingMutex);
        m_pendingFrames.clear();
    }
    m_decodedFrames.clear();
    m_displayedFrameCount = 0;
    m_displayFps = 0.0;
    m_perfWindowStartMs = m_wallClock.elapsed();

    gst_element_set_state(m_gst->playbin, GST_STATE_PAUSED);
    m_pollTimer.start();
    m_renderTimer.start();

    emit statusChanged(QStringLiteral("Loaded %1").arg(QFileInfo(path).fileName()));
    emitPlaybackSnapshot();
    return true;
#else
    m_hasPlayableMedia = true;
    emit statusChanged(QStringLiteral("Loaded %1").arg(QFileInfo(path).fileName()));
    emitPlaybackSnapshot();
    return true;
#endif
}

bool MediaEngine::hasMedia() const
{
    return !m_currentFrame.isNull();
}

QImage MediaEngine::currentFrame() const
{
    return m_currentFrame;
}

QString MediaEngine::backendSummary() const
{
    return m_backendSummary;
}

bool MediaEngine::canPlay() const
{
    return m_hasPlayableMedia;
}

bool MediaEngine::isPlaying() const
{
    return m_isPlaying;
}

qint64 MediaEngine::durationMs() const
{
    return m_durationMs;
}

qint64 MediaEngine::positionMs() const
{
    return m_positionMs;
}

void MediaEngine::togglePlayPause()
{
#ifdef AKERA_HAS_GSTREAMER
    if (!m_hasPlayableMedia || !m_gst || !m_gst->playbin) {
        emit statusChanged(QStringLiteral("Load a video first"));
        return;
    }

    gst_element_set_state(m_gst->playbin, m_isPlaying ? GST_STATE_PAUSED : GST_STATE_PLAYING);
#else
    emit statusChanged(QStringLiteral("Stub backend: playback not available in this build"));
#endif
}

void MediaEngine::setPositionMs(qint64 value)
{
#ifdef AKERA_HAS_GSTREAMER
    if (!m_hasPlayableMedia || !m_gst || !m_gst->playbin) {
        return;
    }
    const gint64 target = static_cast<gint64>(qMax<qint64>(0, value)) * GST_MSECOND;
    gst_element_seek_simple(
        m_gst->playbin,
        GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_ACCURATE),
        target);
    {
        QMutexLocker lock(&m_pendingMutex);
        m_pendingFrames.clear();
    }
    m_decodedFrames.clear();
    m_renderSkipCount = 0;

    if (!m_isPlaying && m_gst->appsink) {
        GstSample* sample = gst_app_sink_try_pull_preroll(GST_APP_SINK(m_gst->appsink), 50000);
        if (sample) {
            GstCaps* caps = gst_sample_get_caps(sample);
            GstBuffer* buffer = gst_sample_get_buffer(sample);
            if (caps && buffer) {
                GstStructure* s = gst_caps_get_structure(caps, 0);
                int width = 0;
                int height = 0;
                const char* format = gst_structure_get_string(s, "format");
                gst_structure_get_int(s, "width", &width);
                gst_structure_get_int(s, "height", &height);
                GstMapInfo map{};
                if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
                    const int bytesPerLine = width * 4;
                    QImage frame;
                    if (format && QByteArray(format) == "BGRA") {
                        frame = QImage(map.data, width, height, bytesPerLine, QImage::Format_ARGB32).copy();
                    } else {
                        frame = QImage(map.data, width, height, bytesPerLine, QImage::Format_RGB32).copy();
                    }
                    qint64 ptsMs = qMax<qint64>(0, value);
                    if (GST_BUFFER_PTS_IS_VALID(buffer)) {
                        ptsMs = static_cast<qint64>(GST_BUFFER_PTS(buffer) / GST_MSECOND);
                    }
                    gst_buffer_unmap(buffer, &map);
                    if (!frame.isNull()) {
                        processAndEmitFrame(frame, ptsMs);
                    }
                }
            }
            gst_sample_unref(sample);
        }
    }
#else
    m_positionMs = value;
    emit positionChanged(m_positionMs, m_durationMs);
#endif
}

void MediaEngine::pollBus()
{
#ifdef AKERA_HAS_GSTREAMER
    if (!m_gst || !m_gst->bus) {
        return;
    }

    while (GstMessage* msg = gst_bus_pop(m_gst->bus)) {
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            emit statusChanged(QStringLiteral("GStreamer error: %1").arg(gstMessageToQString(err ? err->message : nullptr)));
            if (err) g_error_free(err);
            if (dbg) g_free(dbg);
            m_isPlaying = false;
            emit playbackStateChanged(false);
            break;
        }
        case GST_MESSAGE_EOS:
            gst_element_seek_simple(
                m_gst->playbin,
                GST_FORMAT_TIME,
                static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                0);
            gst_element_set_state(m_gst->playbin, GST_STATE_PAUSED);
            m_isPlaying = false;
            emit playbackStateChanged(false);
            emit statusChanged(QStringLiteral("Reached end of file"));
            break;
        case GST_MESSAGE_STATE_CHANGED:
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(m_gst->playbin)) {
                handleStateChanged();
            }
            break;
        case GST_MESSAGE_DURATION_CHANGED:
            updatePosition();
            break;
        default:
            break;
        }
        gst_message_unref(msg);
    }

    updatePosition();
    std::deque<DecodedFrame> drained;
    {
        QMutexLocker lock(&m_pendingMutex);
        if (!m_pendingFrames.empty()) {
            drained.swap(m_pendingFrames);
        }
    }
    if (!drained.empty()) {
        for (auto& frame : drained) {
            if (!frame.image.isNull()) {
                m_decodedFrames.emplace_back(std::move(frame));
            }
        }
        constexpr size_t kMaxBufferedFrames = 180;
        while (m_decodedFrames.size() > kMaxBufferedFrames) {
            m_decodedFrames.pop_front();
            ++m_frameDropCount;
        }
    }
#endif
}

void MediaEngine::renderTick()
{
#ifdef AKERA_HAS_GSTREAMER
    if (!m_hasPlayableMedia) {
        return;
    }
    if (!m_isPlaying) {
        // Paused: keep current frame visible, no skip accumulation and no playback-clock advancement by render loop.
        emitPerfUpdate();
        return;
    }

    const qint64 clockMs = std::max<qint64>(0, m_positionMs);
    DecodedFrame selected;
    bool found = false;
    while (!m_decodedFrames.empty() && m_decodedFrames.front().ptsMs >= 0 && m_decodedFrames.front().ptsMs <= clockMs) {
        selected = std::move(m_decodedFrames.front());
        m_decodedFrames.pop_front();
        found = true;
    }

    if (!found) {
        // If decode is behind, keep showing the previous frame without blocking.
        ++m_renderSkipCount;
        emitPerfUpdate();
        return;
    }

    processAndEmitFrame(selected.image, selected.ptsMs);
#endif
}

void MediaEngine::onAppStateChanged()
{
    updatePreviewAudioState();
}

void MediaEngine::emitPlaybackSnapshot()
{
    emit playbackStateChanged(m_isPlaying);
    emit positionChanged(m_positionMs, m_durationMs);
    emitPerfUpdate();
}

void MediaEngine::updatePreviewAudioState()
{
#ifdef AKERA_HAS_GSTREAMER
    if (m_gst && m_gst->playbin && m_state) {
        g_object_set(G_OBJECT(m_gst->playbin), "volume", m_state->previewAudioEnabled() ? 1.0 : 0.0, nullptr);
    }
#endif
    emitPerfUpdate();
}

void MediaEngine::processAndEmitFrame(const QImage& rawFrame, qint64 ptsMs)
{
    if (rawFrame.isNull()) {
        return;
    }
    m_latestRawFrame = rawFrame;
    if (ptsMs >= 0) {
        m_latestRawPtsMs = ptsMs;
        m_positionMs = ptsMs;
        emit positionChanged(m_positionMs, m_durationMs);
    }

    m_currentFrame = rawFrame;

    const qint64 nowMs = m_wallClock.elapsed();
    ++m_displayedFrameCount;
    if (m_perfWindowStartMs <= 0) {
        m_perfWindowStartMs = nowMs;
    }
    const qint64 perfWindowMs = std::max<qint64>(1, nowMs - m_perfWindowStartMs);
    if (perfWindowMs >= 500) {
        m_displayFps = (1000.0 * static_cast<double>(m_displayedFrameCount)) / static_cast<double>(perfWindowMs);
        m_displayedFrameCount = 0;
        m_perfWindowStartMs = nowMs;
    }

    emit frameReady(m_currentFrame);
    emitPerfUpdate();
}

void MediaEngine::emitPerfUpdate()
{
    QString mode = QStringLiteral("Balanced");
    if (m_state) {
        switch (m_state->previewMode()) {
        case AppState::PreviewMode::Draft: mode = QStringLiteral("Draft"); break;
        case AppState::PreviewMode::Balanced: mode = QStringLiteral("Balanced"); break;
        case AppState::PreviewMode::Ultra: mode = QStringLiteral("Ultra"); break;
        }
    }
    const QString audio = (m_state && m_state->previewAudioEnabled()) ? QStringLiteral("audio:on") : QStringLiteral("audio:off");
    const QString fpsText = m_isPlaying ? QStringLiteral("%1").arg(m_displayFps, 0, 'f', 1) : QStringLiteral("paused");
    emit perfTextChanged(QStringLiteral("%1 • %2 fps • qdrop:%3 • rskip:%4 • %5")
                             .arg(mode)
                             .arg(fpsText)
                             .arg(m_frameDropCount)
                             .arg(m_renderSkipCount)
                             .arg(audio));
}

QImage MediaEngine::makePlaceholderFrame(const QString& path) const
{
    const bool isVertical = QFileInfo(path).suffix().compare(QStringLiteral("mov"), Qt::CaseInsensitive) == 0;
    const QSize frameSize = isVertical ? QSize(1080, 1920) : QSize(1280, 720);

    QImage image(frameSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#050505"));

    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient grad(0, 0, frameSize.width(), frameSize.height());
    grad.setColorAt(0.0, QColor("#070707"));
    grad.setColorAt(0.55, QColor("#04070c"));
    grad.setColorAt(1.0, QColor("#0b1218"));
    p.fillRect(image.rect(), grad);

    p.setPen(QPen(QColor("#101010"), 1.0));
    p.drawRect(image.rect().adjusted(0, 0, -1, -1));

    p.setPen(QColor("#2b2b2b"));
    p.setFont(QFont(QStringLiteral("Consolas"), isVertical ? 18 : 13));
    p.drawText(QRect(0, 0, frameSize.width(), 120), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("  // AKERA SKY  GLITCH STUDIO  PREMIUM"));

    p.setPen(QColor("#606060"));
    p.setFont(QFont(QStringLiteral("Consolas"), isVertical ? 20 : 14));
    p.drawText(QRect(0, frameSize.height() / 2 - 40, frameSize.width(), 80), Qt::AlignCenter,
               QStringLiteral("LOADING VIDEO"));
    p.drawText(QRect(0, frameSize.height() / 2 + 5, frameSize.width(), 50), Qt::AlignCenter,
               QStringLiteral("preparing GStreamer preview"));

    p.setPen(QColor("#7d7d7d"));
    p.setFont(QFont(QStringLiteral("Consolas"), isVertical ? 16 : 12));
    p.drawText(QRect(40, frameSize.height() - 70, frameSize.width() - 80, 30), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Loaded preview for: %1").arg(QFileInfo(path).fileName()));

    return image;
}

#ifdef AKERA_HAS_GSTREAMER
bool MediaEngine::setupPipeline()
{
    if (m_gst) {
        return true;
    }

    m_gst = new GstHandles{};
    m_gst->playbin = gst_element_factory_make("playbin", "player");
    m_gst->appsink = gst_element_factory_make("appsink", "video_sink");

    if (!m_gst->playbin || !m_gst->appsink) {
        teardownPipeline();
        return false;
    }

    GstCaps* caps = gst_caps_from_string("video/x-raw,format=BGRA;video/x-raw,format=BGRx");
    gst_app_sink_set_caps(GST_APP_SINK(m_gst->appsink), caps);
    gst_caps_unref(caps);

    gst_app_sink_set_emit_signals(GST_APP_SINK(m_gst->appsink), TRUE);
    gst_app_sink_set_drop(GST_APP_SINK(m_gst->appsink), TRUE);
    gst_app_sink_set_max_buffers(GST_APP_SINK(m_gst->appsink), 2);
    gst_base_sink_set_sync(GST_BASE_SINK(m_gst->appsink), TRUE);

    g_signal_connect(m_gst->appsink, "new-sample", G_CALLBACK(&MediaEngine::onNewSampleThunk), this);
    g_object_set(G_OBJECT(m_gst->playbin), "video-sink", m_gst->appsink, nullptr);

    m_gst->bus = gst_element_get_bus(m_gst->playbin);
    updatePreviewAudioState();
    return true;
}

void MediaEngine::teardownPipeline()
{
    m_pollTimer.stop();
    m_renderTimer.stop();

    if (!m_gst) {
        return;
    }

    if (m_gst->playbin) {
        gst_element_set_state(m_gst->playbin, GST_STATE_NULL);
    }
    if (m_gst->bus) {
        gst_object_unref(m_gst->bus);
    }
    if (m_gst->appsink) {
        gst_object_unref(m_gst->appsink);
    }
    if (m_gst->playbin) {
        gst_object_unref(m_gst->playbin);
    }
    delete m_gst;
    m_gst = nullptr;
}

void MediaEngine::handleStateChanged()
{
    if (!m_gst || !m_gst->playbin) {
        return;
    }

    GstState state = GST_STATE_NULL;
    GstState pending = GST_STATE_NULL;
    gst_element_get_state(m_gst->playbin, &state, &pending, 0);
    const bool nowPlaying = (state == GST_STATE_PLAYING);
    if (m_isPlaying != nowPlaying) {
        m_isPlaying = nowPlaying;
        if (!m_isPlaying) {
            m_displayFps = 0.0;
            m_displayedFrameCount = 0;
            m_renderSkipCount = 0;
            m_decodedFrames.clear();
        } else {
            m_perfWindowStartMs = m_wallClock.elapsed();
        }
        emit playbackStateChanged(m_isPlaying);
        emitPerfUpdate();
    }
}

void MediaEngine::updatePosition()
{
    if (!m_gst || !m_gst->playbin) {
        return;
    }

    gint64 pos = GST_CLOCK_TIME_NONE;
    gint64 dur = GST_CLOCK_TIME_NONE;
    if (gst_element_query_position(m_gst->playbin, GST_FORMAT_TIME, &pos) && pos != GST_CLOCK_TIME_NONE) {
        m_positionMs = static_cast<qint64>(pos / GST_MSECOND);
    }
    if (gst_element_query_duration(m_gst->playbin, GST_FORMAT_TIME, &dur) && dur != GST_CLOCK_TIME_NONE) {
        m_durationMs = static_cast<qint64>(dur / GST_MSECOND);
    }
    emit positionChanged(m_positionMs, m_durationMs);
}

int MediaEngine::onNewSampleThunk(void* sink, void* userData)
{
    Q_UNUSED(sink);
    auto* self = static_cast<MediaEngine*>(userData);
    return self->handleSample() ? GST_FLOW_OK : GST_FLOW_ERROR;
}

bool MediaEngine::handleSample()
{
    if (!m_gst || !m_gst->appsink) {
        return false;
    }

    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(m_gst->appsink));
    if (!sample) {
        return false;
    }

    GstCaps* caps = gst_sample_get_caps(sample);
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (!caps || !buffer) {
        gst_sample_unref(sample);
        return false;
    }

    GstStructure* s = gst_caps_get_structure(caps, 0);
    int width = 0;
    int height = 0;
    const char* format = gst_structure_get_string(s, "format");
    gst_structure_get_int(s, "width", &width);
    gst_structure_get_int(s, "height", &height);

    GstMapInfo map{};
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return false;
    }

    // Deep-copy the pixel data immediately so we can unmap the GStreamer buffer.
    // This is the ONLY Qt operation performed on this (non-main) thread.
    // Effect processing and signal emission happen in pollBus() on the main thread.
    QImage frame;
    const int bytesPerLine = width * 4;
    if (format && QByteArray(format) == "BGRA") {
        frame = QImage(map.data, width, height, bytesPerLine, QImage::Format_ARGB32).copy();
    } else {
        frame = QImage(map.data, width, height, bytesPerLine, QImage::Format_RGB32).copy();
    }

    qint64 ptsMs = -1;
    if (GST_BUFFER_PTS_IS_VALID(buffer)) {
        ptsMs = static_cast<qint64>(GST_BUFFER_PTS(buffer) / GST_MSECOND);
    }

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    // Stash for the main thread to pick up in pollBus().
    if (!frame.isNull()) {
        QMutexLocker lock(&m_pendingMutex);
        m_pendingFrames.push_back(DecodedFrame{std::move(frame), ptsMs});
        constexpr size_t kMaxPendingFrames = 32;
        while (m_pendingFrames.size() > kMaxPendingFrames) {
            m_pendingFrames.pop_front();
            ++m_frameDropCount;
        }
    }

    return true;
}
#endif
