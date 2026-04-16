#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

class TransportBar final : public QWidget
{
    Q_OBJECT
public:
    explicit TransportBar(QWidget* parent = nullptr);

    void setFrameText(const QString& text);
    void setPerfText(const QString& text);
    void setPlaying(bool playing);
    void setDurationAndPosition(qint64 durationMs, qint64 positionMs);

signals:
    void playClicked();
    void seekRequested(qint64 positionMs);

private:
    QPushButton* m_playButton = nullptr;
    QSlider* m_slider = nullptr;
    QLabel* m_frameLabel = nullptr;
    QLabel* m_perfLabel = nullptr;
    qint64 m_durationMs = 0;
    bool m_ignoreSliderSignal = false;
};
