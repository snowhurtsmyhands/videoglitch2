#include "ui/TransportBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

TransportBar::TransportBar(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    m_playButton = new QPushButton(QStringLiteral("▶  PLAY"), this);
    m_playButton->setFixedWidth(96);
    m_playButton->setCursor(Qt::PointingHandCursor);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 1000);
    m_slider->setValue(0);
    m_slider->setSingleStep(1);
    m_slider->setPageStep(10);

    m_frameLabel = new QLabel(QStringLiteral("0:00 / 0:00"), this);
    m_perfLabel = new QLabel(QStringLiteral("Balanced • 0.0 fps • GPU:pending"), this);

    layout->addWidget(m_playButton);
    layout->addWidget(m_slider, 1);
    layout->addWidget(m_frameLabel);
    layout->addSpacing(10);
    layout->addWidget(m_perfLabel);

    connect(m_playButton, &QPushButton::clicked, this, &TransportBar::playClicked);
    connect(m_slider, &QSlider::sliderMoved, this, [this](int value) {
        if (m_ignoreSliderSignal || m_durationMs <= 0) {
            return;
        }
        emit seekRequested(static_cast<qint64>(value));
    });
}

void TransportBar::setFrameText(const QString& text)
{
    m_frameLabel->setText(text);
}

void TransportBar::setPerfText(const QString& text)
{
    m_perfLabel->setText(text);
}

void TransportBar::setPlaying(bool playing)
{
    m_playButton->setText(playing ? QStringLiteral("⏸  PAUSE") : QStringLiteral("▶  PLAY"));
}

void TransportBar::setDurationAndPosition(qint64 durationMs, qint64 positionMs)
{
    m_durationMs = durationMs;
    m_ignoreSliderSignal = true;
    m_slider->setRange(0, static_cast<int>(durationMs > 0 ? durationMs : 1000));
    m_slider->setValue(static_cast<int>(positionMs));
    m_ignoreSliderSignal = false;
}
