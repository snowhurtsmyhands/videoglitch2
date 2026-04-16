#pragma once

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <deque>

class AppState;

#ifdef AKERA_HAS_GSTREAMER
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#endif

class MediaEngine final : public QObject
{
    Q_OBJECT
public:
    explicit MediaEngine(AppState* state, QObject* parent = nullptr);
    ~MediaEngine() override;

    bool loadFile(const QString& path);
    bool hasMedia() const;
    QImage currentFrame() const;
    QString backendSummary() const;

    bool canPlay() const;
    bool isPlaying() const;
    qint64 durationMs() const;
    qint64 positionMs() const;

public slots:
    void togglePlayPause();
    void setPositionMs(qint64 value);

signals:
    void frameReady(const QImage& image);
    void statusChanged(const QString& text);
    void playbackStateChanged(bool playing);
    void positionChanged(qint64 positionMs, qint64 durationMs);
    void perfTextChanged(const QString& text);

private slots:
    void pollBus();
    void renderTick();
    void onAppStateChanged();

private:
    QImage makePlaceholderFrame(const QString& path) const;
    void emitPlaybackSnapshot();
    void updatePreviewAudioState();
    void refreshPreviewFromCachedRaw();
    void processAndEmitFrame(const QImage& rawFrame, qint64 ptsMs);
    void emitPerfUpdate();

    struct DecodedFrame {
        QImage image;
        qint64 ptsMs = -1;
    };

#ifdef AKERA_HAS_GSTREAMER
    struct GstHandles {
        GstElement* playbin = nullptr;
        GstElement* appsink = nullptr;
        GstBus* bus = nullptr;
    };

    bool setupPipeline();
    void teardownPipeline();
    void handleStateChanged();
    void updatePosition();
    static int onNewSampleThunk(void* sink, void* userData);
    bool handleSample();
#endif

    AppState* m_state = nullptr;
    QImage m_currentFrame;
    QString m_backendSummary;
    bool m_hasPlayableMedia = false;
    bool m_isPlaying = false;
    qint64 m_durationMs = 0;
    qint64 m_positionMs = 0;
    qint64 m_frameIndex = 0;
    qint64 m_rawFrameIndex = 0;
    qint64 m_frameDropCount = 0;
    qint64 m_effectSkipCount = 0;
    double m_effectCostMs = 0.0;
    bool m_lastDegraded = false;
    QString m_lastDegradeText;
    QTimer m_pollTimer;
    QTimer m_renderTimer;

    // Thread-safe pending frame: written by GStreamer thread, consumed by poll timer on main thread
    QMutex m_pendingMutex;
    std::deque<DecodedFrame> m_pendingFrames;
    std::deque<DecodedFrame> m_decodedFrames;
    QImage m_latestRawFrame;
    qint64 m_latestRawPtsMs = 0;
    bool m_refreshQueued = false;
    QElapsedTimer m_wallClock;
    qint64 m_perfWindowStartMs = 0;
    int m_displayedFrameCount = 0;
    double m_displayFps = 0.0;

#ifdef AKERA_HAS_GSTREAMER
    GstHandles* m_gst = nullptr;
#endif
};
